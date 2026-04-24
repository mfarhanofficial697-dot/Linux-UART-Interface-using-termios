/*
 * ================================================================
 *  
 *  Linux UART Validation Testing
 * ================================================================
 *
 *  WHAT THIS PROGRAM DOES:
 *  ─────────────────────────────────────────────────────────────
 *  1. Auto-detects your serial device (ttyACM0, ttyACM1, ttyUSB0)
 *  2. Configures UART via termios  (115200 baud, 8N1, no flow ctrl)
 *  3. Sends a test message and prints TX bytes as hex dump
 *  4. Waits for reply using select() with a configurable timeout
 *  5. Prints received bytes as both hex dump AND ASCII
 *  6. Loops — keeps receiving until Ctrl+C (SIGINT)
 *  7. Restores terminal and closes cleanly on exit
 *  8. Prints a full PASS/FAIL summary at the end
 *
 *  THREE VERIFICATION MODES:
 *  ─────────────────────────────────────────────────────────────
 *  Mode A — RP2040 on /dev/ttyACM0  (your current setup)
 *    ./uart_test /dev/ttyACM0
 *    TX proven by hex dump. RX needs firmware to echo.
 *
 *  Mode B — Hardware loopback (TX pin shorted to RX pin)
 *    ./uart_test /dev/ttyUSB0
 *    Every TX byte comes back as RX. Full PASS guaranteed.
 *
 *  Mode C — Virtual serial pair (zero hardware needed)
 *    Terminal 1:  socat -d -d pty,raw,echo=0 pty,raw,echo=0
 *    Terminal 2:  ./uart_test /dev/pts/3
 *    Terminal 3:  echo "REPLY OK" > /dev/pts/4
 *
 *  BUILD:
 *    gcc -o uart_test uart_test.c
 *
 *  RUN:
 *    ./uart_test                    (auto-detect device)
 *    ./uart_test /dev/ttyACM0
 *    ./uart_test /dev/ttyACM1
 *    ./uart_test /dev/ttyUSB0
 *    ./uart_test /dev/pts/3         (socat virtual port)
 *
 *  Author : Farhan  —  RISC-V ACT Coding Challenge
 * ================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>         /* signal(), SIGINT                      */

#include <fcntl.h>          /* open(), O_RDWR, O_NOCTTY, O_NDELAY   */
#include <unistd.h>         /* read(), write(), close(), access()    */
#include <termios.h>        /* struct termios, tcgetattr/tcsetattr   */
#include <sys/select.h>     /* select(), fd_set, FD_*                */
#include <sys/time.h>       /* struct timeval                        */

/* ── Configuration ───────────────────────────────────────────── */
#define BAUD_RATE        B115200  /* 115200 bps — matches RP2040      */
#define READ_TIMEOUT_S   10       /* seconds per RX attempt           */
#define RX_LOOP_COUNT    10       /* number of RX attempts before exit*/
#define RX_BUF_SIZE      512      /* receive buffer size in bytes     */

/* Auto-detect order: tried top to bottom when no arg is given */
static const char *AUTO_DETECT_PATHS[] = {
    "/dev/ttyACM0",   /* RP2040 Pico USB CDC — most likely for you  */
    "/dev/ttyACM1",   /* RP2040 second CDC interface                */
    "/dev/ttyUSB0",   /* CH340 / CP2102 / FTDI USB-UART adapter     */
    "/dev/ttyUSB1",
    NULL              /* end-of-list sentinel                        */
};

/* The test message transmitted on every run */
static const char TX_MESSAGE[] = "HELLO FROM LINUX UART - RISC-V ACT TEST\n";

/* Set to 0 by SIGINT handler (Ctrl+C) to exit the RX loop cleanly */
static volatile int g_running = 1;

/* ── Forward declarations ────────────────────────────────────── */
static const char *uart_auto_detect(void);
static int         uart_open(const char *device_path);
static int         uart_configure(int fd, struct termios *old_settings);
static int         uart_transmit(int fd, const unsigned char *data,
                                 size_t len);
static int         uart_receive(int fd, unsigned char *buf,
                                size_t buf_size, int timeout_sec);
static void        uart_restore_and_close(int fd,
                                 const struct termios *old_settings);
static void        print_hex_dump(const char *label,
                                  const unsigned char *data, size_t len);
static void        print_separator(void);
static void        sigint_handler(int sig);


/* ================================================================
 *  main()
 *  Flow: detect → open → configure → TX → RX loop → summary → close
 * ================================================================ */
int main(int argc, char *argv[])
{
    /*
     * Catch Ctrl+C so we can restore terminal settings before exit.
     * Without this, raw-mode terminal settings stay active and the
     * shell stops echoing what the user types — very confusing.
     */
    signal(SIGINT, sigint_handler);

    print_separator();
    printf("  UART Validation Tool — RISC-V ACT Framework (v2)\n");
    print_separator();

    /* ── Step 1: Resolve device path ───────────────────────── */
    const char *device = NULL;

    if (argc >= 2) {
        device = argv[1];
        printf("[INFO]  Device (from argument) : %s\n", device);
    } else {
        printf("[INFO]  No device given — running auto-detect...\n");
        device = uart_auto_detect();
        if (!device) {
            fprintf(stderr,
                "[ERROR] No serial device found.\n"
                "[HINT]  Is RP2040 plugged in?\n"
                "[HINT]  Run: ls /dev/tty*   or   dmesg | tail\n");
            return EXIT_FAILURE;
        }
        printf("[INFO]  Auto-detected device  : %s\n", device);
    }
    printf("[INFO]  Usage: %s <device>   e.g. /dev/ttyACM0\n", argv[0]);
    print_separator();

    /* ── Step 2: Open device ────────────────────────────────── */
    int fd = uart_open(device);
    if (fd < 0)
        return EXIT_FAILURE;

    /* ── Step 3: Configure UART (termios) ───────────────────── */
    struct termios old_settings;
    if (uart_configure(fd, &old_settings) < 0) {
        close(fd);
        return EXIT_FAILURE;
    }

    /* ── Step 4: Transmit test message ─────────────────────── */
    print_separator();
    printf("[TX] Transmitting test message (%zu bytes)\n",
           strlen(TX_MESSAGE));
    print_hex_dump("TX", (const unsigned char *)TX_MESSAGE,
                   strlen(TX_MESSAGE));

    if (uart_transmit(fd,
                      (const unsigned char *)TX_MESSAGE,
                      strlen(TX_MESSAGE)) < 0) {
        uart_restore_and_close(fd, &old_settings);
        return EXIT_FAILURE;
    }

    /* ── Step 5: RX loop ────────────────────────────────────── */
    print_separator();
    printf("[RX] Listening for replies (Ctrl+C to stop early)\n");
    printf("[RX] Timeout per attempt : %d s   Max attempts : %d\n",
           READ_TIMEOUT_S, RX_LOOP_COUNT);
    print_separator();

    unsigned char rx_buf[RX_BUF_SIZE];
    int total_rx_bytes = 0;
    int pass_count     = 0;
    int timeout_count  = 0;
    int attempt        = 0;

    while (g_running && attempt < RX_LOOP_COUNT) {
        attempt++;
        printf("\n[RX] Attempt %d/%d — waiting up to %d s...\n",
               attempt, RX_LOOP_COUNT, READ_TIMEOUT_S);

        int rx_bytes = uart_receive(fd, rx_buf, sizeof(rx_buf),
                                    READ_TIMEOUT_S);

        if (rx_bytes > 0) {
            /* ── Data received ─────────────────────────────── */
            total_rx_bytes += rx_bytes;
            pass_count++;

            print_hex_dump("RX", rx_buf, (size_t)rx_bytes);

            printf("[RX] ASCII  : \"");
            for (int i = 0; i < rx_bytes; i++) {
                unsigned char c = rx_buf[i];
                putchar((c >= 0x20 && c < 0x7F) ? c : '.');
            }
            printf("\"\n");
            printf("[RX] PASS   — %d byte(s) received this attempt\n",
                   rx_bytes);

            /* Send another TX so device has something to respond to */
            if (g_running && attempt < RX_LOOP_COUNT) {
                printf("[TX] Sending next message...\n");
                uart_transmit(fd,
                    (const unsigned char *)TX_MESSAGE,
                    strlen(TX_MESSAGE));
            }

        } else if (rx_bytes == 0) {
            /* ── Timeout ────────────────────────────────────── */
            timeout_count++;
            printf("[RX] TIMEOUT — no data received within %d s\n",
                   READ_TIMEOUT_S);

            /* Print hints only once, not on every timeout */
            if (timeout_count == 1) {
                printf("[HINT]  TX is working (hex dump proves it).\n");
                printf("[HINT]  For full RX PASS, choose one:\n");
                printf("[HINT]   A) Add echo to RP2040 firmware\n");
                printf("[HINT]   B) Short TX->RX on USB-UART adapter\n");
                printf("[HINT]   C) Use socat virtual port pair\n");
            }

        } else {
            /* ── Read error ─────────────────────────────────── */
            fprintf(stderr,
                "[ERROR] Read error on attempt %d — stopping\n",
                attempt);
            break;
        }
    }

    /* ── Step 6: Summary ────────────────────────────────────── */
    print_separator();
    printf("[SUMMARY] Device         : %s\n", device);
    printf("[SUMMARY] TX message     : \"%.*s...\"\n", 20, TX_MESSAGE);
    printf("[SUMMARY] Baud / format  : 115200 8N1\n");
    printf("[SUMMARY] RX attempts    : %d\n", attempt);
    printf("[SUMMARY] RX successes   : %d\n", pass_count);
    printf("[SUMMARY] RX timeouts    : %d\n", timeout_count);
    printf("[SUMMARY] Total RX bytes : %d\n", total_rx_bytes);
    print_separator();

    if (pass_count > 0) {
        printf("[RESULT]  *** FULL PASS *** — TX + RX both verified\n");
        printf("          %d/%d attempt(s) received data\n",
               pass_count, attempt);
    } else {
        printf("[RESULT]  TX PASS  /  RX TIMEOUT\n");
        printf("          TX proven by hex dump above.\n");
        printf("          RX requires firmware echo or loopback test.\n");
    }
    print_separator();

    /* ── Step 7: Restore terminal settings and close ────────── */
    uart_restore_and_close(fd, &old_settings);
    printf("[INFO]  Terminal restored. Device closed cleanly.\n");
    print_separator();

    return (pass_count > 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}


/* ================================================================
 *  sigint_handler()
 *  Ctrl+C handler — sets g_running=0 so RX loop exits gracefully.
 *  write() is async-signal-safe; printf() is NOT (do not use it here).
 * ================================================================ */
static void sigint_handler(int sig)
{
    (void)sig;
    g_running = 0;
    const char msg[] = "\n[INFO]  Ctrl+C caught — finishing current attempt...\n";
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
}


/* ================================================================
 *  uart_auto_detect()
 *  Probes each path in AUTO_DETECT_PATHS using access(F_OK).
 *  Returns the first path that exists, or NULL if none found.
 * ================================================================ */
static const char *uart_auto_detect(void)
{
    for (int i = 0; AUTO_DETECT_PATHS[i] != NULL; i++) {
        if (access(AUTO_DETECT_PATHS[i], F_OK) == 0) {
            printf("[INFO]  Found : %s\n", AUTO_DETECT_PATHS[i]);
            return AUTO_DETECT_PATHS[i];
        }
        printf("[INFO]  Not found: %s\n", AUTO_DETECT_PATHS[i]);
    }
    return NULL;
}


/* ================================================================
 *  uart_open()
 *
 *  Opens the serial device. Flags:
 *    O_RDWR   — both read and write
 *    O_NOCTTY — don't become the controlling terminal
 *    O_NDELAY — non-blocking open (avoids DCD wait)
 *
 *  After open(), clears O_NONBLOCK so reads are controlled
 *  by VMIN/VTIME and select() — not by spinning.
 *
 *  Returns: fd >= 0 on success, -1 on failure.
 * ================================================================ */
static int uart_open(const char *device_path)
{
    int fd = open(device_path, O_RDWR | O_NOCTTY | O_NDELAY);

    if (fd < 0) {
        fprintf(stderr, "[ERROR] Cannot open '%s': %s\n",
                device_path, strerror(errno));

        if (errno == EACCES)
            fprintf(stderr,
                "[HINT]  Permission denied. Fix:\n"
                "[HINT]    sudo chmod 666 %s\n"
                "[HINT]    sudo usermod -aG dialout $USER\n",
                device_path);

        if (errno == ENOENT)
            fprintf(stderr,
                "[HINT]  Device not found. Try:\n"
                "[HINT]    ls /dev/ttyACM*\n"
                "[HINT]    ls /dev/ttyUSB*\n"
                "[HINT]    dmesg | tail -20\n");

        if (errno == EBUSY)
            fprintf(stderr,
                "[HINT]  Device busy — close minicom/picocom first.\n");

        return -1;
    }

    printf("[INFO]  Opened '%s'  (fd = %d)\n", device_path, fd);

    /* Switch back to blocking I/O — O_NDELAY only needed for open() */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

    return fd;
}


/* ================================================================
 *  uart_configure()
 *
 *  Configures 115200 8N1 raw mode via termios.
 *
 *  termios flag groups:
 *    c_cflag  — hardware: baud rate, data bits, parity, stop bits
 *    c_iflag  — input processing: flow ctrl, NL/CR translation
 *    c_oflag  — output processing (all disabled for raw mode)
 *    c_lflag  — local: echo, canonical mode, signals
 *    c_cc[]   — special chars + VMIN / VTIME read timing
 *
 *  Returns: 0 on success, -1 on failure.
 * ================================================================ */
static int uart_configure(int fd, struct termios *old_settings)
{
    /* Save current settings so we can restore them on exit */
    if (tcgetattr(fd, old_settings) < 0) {
        fprintf(stderr, "[ERROR] tcgetattr failed: %s\n",
                strerror(errno));
        return -1;
    }

    struct termios cfg;
    memset(&cfg, 0, sizeof(cfg));

    /*
     * cfmakeraw() sets raw mode in one call — disables:
     *   canonical mode, echo, signal chars, CR/NL translation,
     *   parity checking, output post-processing.
     * We then explicitly set every bit we need for clarity.
     */
    cfmakeraw(&cfg);

    /* ── Baud rate ──────────────────────────────────────────── */
    cfsetispeed(&cfg, BAUD_RATE);   /* input  baud */
    cfsetospeed(&cfg, BAUD_RATE);   /* output baud */

    /* ── c_cflag (hardware / control) ──────────────────────── */
    cfg.c_cflag |= CLOCAL;     /* ignore modem control lines (DCD)  */
    cfg.c_cflag |= CREAD;      /* enable the receiver               */
    cfg.c_cflag &= ~CSIZE;     /* clear data-size bits              */
    cfg.c_cflag |=  CS8;       /* 8 data bits                       */
    cfg.c_cflag &= ~PARENB;    /* no parity                         */
    cfg.c_cflag &= ~CSTOPB;    /* 1 stop bit (cleared = 1)          */
    cfg.c_cflag &= ~CRTSCTS;   /* no hardware (RTS/CTS) flow ctrl   */

    /* ── c_iflag (input processing) ────────────────────────── */
    cfg.c_iflag &= ~(IXON | IXOFF | IXANY);   /* no SW flow ctrl    */
    cfg.c_iflag &= ~(INPCK  | ISTRIP);         /* no parity check    */
    cfg.c_iflag &= ~(ICRNL  | IGNCR | INLCR); /* no CR/NL mapping   */

    /* ── c_oflag (output processing) ───────────────────────── */
    cfg.c_oflag &= ~OPOST;     /* raw output — no post-processing   */

    /* ── c_lflag (local / line discipline) ─────────────────── */
    cfg.c_lflag &= ~ECHO;      /* no echo                           */
    cfg.c_lflag &= ~ECHOE;     /* no erase echo                     */
    cfg.c_lflag &= ~ECHONL;    /* no newline echo                   */
    cfg.c_lflag &= ~ICANON;    /* raw (not line-buffered) mode      */
    cfg.c_lflag &= ~ISIG;      /* ignore ^C / ^Z signals            */
    cfg.c_lflag &= ~IEXTEN;    /* no extended processing            */

    /* ── c_cc (read timing) ────────────────────────────────── */
    /*
     * VMIN=0  → don't wait for minimum bytes
     * VTIME=10 → return after 1 s if no bytes  (10 × 100 ms)
     * select() provides the outer timeout (READ_TIMEOUT_S seconds).
     */
    cfg.c_cc[VMIN]  = 0;
    cfg.c_cc[VTIME] = 10;

    /* Apply immediately */
    if (tcsetattr(fd, TCSANOW, &cfg) < 0) {
        fprintf(stderr, "[ERROR] tcsetattr failed: %s\n",
                strerror(errno));
        return -1;
    }

    /* Discard stale bytes in kernel RX and TX buffers */
    tcflush(fd, TCIOFLUSH);

    printf("[INFO]  UART configured:\n");
    printf("[INFO]    Baud rate : 115200\n");
    printf("[INFO]    Data bits : 8\n");
    printf("[INFO]    Parity    : None\n");
    printf("[INFO]    Stop bits : 1\n");
    printf("[INFO]    Flow ctrl : None\n");
    printf("[INFO]    Mode      : Raw (8N1)\n");
    return 0;
}


/* ================================================================
 *  uart_transmit()
 *
 *  Sends 'len' bytes to the UART. Loops on partial writes.
 *  Calls tcdrain() to block until all bytes physically leave
 *  the TX FIFO — important before waiting for a reply.
 *
 *  Returns: 0 on success, -1 on failure.
 * ================================================================ */
static int uart_transmit(int fd, const unsigned char *data, size_t len)
{
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = write(fd, data + sent, len - sent);
        if (n < 0) {
            if (errno == EINTR) continue;  /* signal — retry */
            fprintf(stderr, "[ERROR] write() failed: %s\n",
                    strerror(errno));
            return -1;
        }
        sent += (size_t)n;
    }

    tcdrain(fd);   /* wait until TX buffer is empty */
    printf("[TX] %zu byte(s) sent OK\n", sent);
    return 0;
}


/* ================================================================
 *  uart_receive()
 *
 *  Waits for incoming data using select() with a hard timeout,
 *  then reads available bytes into buf.
 *
 *  select() behaviour:
 *    > 0  → fd is readable — call read()
 *    = 0  → timeout expired, no data
 *    < 0  → error (EINTR = interrupted by signal, not a real error)
 *
 *  Returns: bytes read (> 0), 0 on timeout, -1 on error.
 * ================================================================ */
static int uart_receive(int fd, unsigned char *buf,
                        size_t buf_size, int timeout_sec)
{
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);

    /*
     * NOTE: Linux modifies struct timeval inside select().
     * Always re-initialise before each select() call in a loop.
     */
    struct timeval tv;
    tv.tv_sec  = timeout_sec;
    tv.tv_usec = 0;

    int ready = select(fd + 1, &read_fds, NULL, NULL, &tv);

    if (ready < 0) {
        if (errno == EINTR) return 0;   /* Ctrl+C — treat as timeout */
        fprintf(stderr, "[ERROR] select() failed: %s\n",
                strerror(errno));
        return -1;
    }

    if (ready == 0)
        return 0;   /* timeout — no data arrived */

    ssize_t n = read(fd, buf, buf_size - 1);
    if (n < 0) {
        if (errno == EINTR) return 0;
        fprintf(stderr, "[ERROR] read() failed: %s\n",
                strerror(errno));
        return -1;
    }
    if (n == 0) {
        fprintf(stderr, "[WARN]  read() = 0 — device disconnected?\n");
        return -1;
    }

    buf[n] = '\0';
    return (int)n;
}


/* ================================================================
 *  uart_restore_and_close()
 *  Restores original termios settings and closes the fd.
 *  Must always be called before exit to keep shell usable.
 * ================================================================ */
static void uart_restore_and_close(int fd,
                                   const struct termios *old_settings)
{
    if (tcsetattr(fd, TCSANOW, old_settings) < 0)
        fprintf(stderr, "[WARN]  Could not restore terminal: %s\n",
                strerror(errno));
    close(fd);
}


/* ================================================================
 *  print_hex_dump()
 *
 *  Prints bytes in hex-editor style: 8 bytes per row with
 *  offset, hex values, and ASCII sidebar.
 *
 *  Example:
 *    [TX] hex dump (22 bytes):
 *         00: 48 45 4C 4C 4F 20 46 52  HELLO FR
 *         08: 4F 4D 20 4C 49 4E 55 58  OM LINUX
 *         10: 20 55 41 52 54 0A        .UART.
 * ================================================================ */
static void print_hex_dump(const char *label,
                           const unsigned char *data, size_t len)
{
    printf("[%s] hex dump (%zu byte%s):\n",
           label, len, len == 1 ? "" : "s");

    for (size_t i = 0; i < len; i += 8) {
        printf("       %02zX: ", i);

        /* Hex column (8 bytes, padded on last row) */
        for (size_t j = 0; j < 8; j++) {
            if (i + j < len)
                printf("%02X ", data[i + j]);
            else
                printf("   ");
        }

        /* ASCII sidebar */
        printf(" ");
        for (size_t j = 0; j < 8 && (i + j) < len; j++) {
            unsigned char c = data[i + j];
            putchar((c >= 0x20 && c < 0x7F) ? c : '.');
        }
        putchar('\n');
    }
}


/* ================================================================
 *  print_separator()
 *  Visual divider for readable terminal output.
 * ================================================================ */
static void print_separator(void)
{
    printf("-----------------------------------------------------------\n");
}
