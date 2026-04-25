# Linux-UART-Interface-using-termios
Linux C program using termios to configure and use UART (/dev/ttyS0 or /dev/ttyUSB0). It sets baud rate, parity, data bits, and stop bits, sends a test message, and receives data using select() for non-blocking I/O. Includes proper error handling and clean, well-commented code for serial communication.

Linux UART Validation Tool
RISC-V ACT Framework — M-Mode Firmware Validation Challenge

What This Project Is
A production-quality C program that initializes and validates a UART interface on Linux using the termios API. Built for the RISC-V ACT Framework Enablement coding challenge, this tool goes beyond a simple serial Hello World — it implements a complete hardware validation pipeline with protocol-level verification.

What Was Built
FeatureImplementationAuto device detectionProbes ttyACM0 → ttyACM1 → ttyUSB0 → ttyUSB1 automaticallyUART configurationtermios: 115200 baud, 8 data bits, no parity, 1 stop bit (8N1)TX transmissionwrite() loop handles partial writes + tcdrain() for flushNon-blocking RXselect() with struct timeval timeout — no infinite blockingHex dump output8-byte rows with offset, hex values, and ASCII sidebarRX loop5 attempts, re-transmits after each successful receiveSignal handlingSIGINT (Ctrl+C) caught — terminal restored before exitError handlingENOENT, EACCES, EBUSY — each prints actionable fix hintSummary reportAttempts / successes / timeouts / total bytes at end

Hardware Used
Board    : Raspberry Pi Pico (RP2040)
Mode     : USB CDC (virtual serial port)
Device   : /dev/ttyACM0  (auto-detected)
OS       : Ubuntu Linux — PC192
Format   : 115200 baud, 8N1, no flow control

Build
bashgcc -Wall -Wextra -o uart uart_test.c
Zero warnings. Confirmed on Ubuntu with GCC.

Run
bash# Auto-detect device (tries ttyACM0 first)
./uart

# Explicit device
./uart /dev/ttyACM0
./uart /dev/ttyACM1
./uart /dev/ttyUSB0
./uart /dev/pts/3      # socat virtual port
Permission fix if needed:
bashsudo chmod 666 /dev/ttyACM0
# or permanently:
sudo usermod -aG dialout $USER

Verification
What Was Verified and How
Three layers of verification were performed:

Layer 1 — UART Configuration + TX  ✅
Device: RP2040 on /dev/ttyACM0
How: Run ./uart and observe output
Output (actual terminal):
-----------------------------------------------------------
  UART Validation Tool — RISC-V ACT Framework (v2)
-----------------------------------------------------------
[INFO]  No device given — running auto-detect...
[INFO]  Found : /dev/ttyACM0
[INFO]  Auto-detected device  : /dev/ttyACM0
-----------------------------------------------------------
[INFO]  Opened '/dev/ttyACM0'  (fd = 3)
[INFO]  UART configured:
[INFO]    Baud rate : 115200
[INFO]    Data bits : 8
[INFO]    Parity    : None
[INFO]    Stop bits : 1
[INFO]    Flow ctrl : None
[INFO]    Mode      : Raw (8N1)
-----------------------------------------------------------
[TX] Transmitting test message (40 bytes)
[TX] hex dump (40 bytes):
       00: 48 45 4C 4C 4F 20 46 52  HELLO FR
       08: 4F 4D 20 4C 49 4E 55 58  OM LINUX
       10: 20 55 41 52 54 20 2D 20   UART -
       18: 52 49 53 43 2D 56 20 41  RISC-V A
       20: 43 54 20 54 45 53 54 0A  CT TEST.
[TX] 40 byte(s) sent OK
Why this is correct:

Found : /dev/ttyACM0 — auto-detect worked without any argument
fd = 3 — open() succeeded, file descriptor is live
All 5 termios parameters printed — configuration applied correctly
Hex 48 45 4C 4C 4F = ASCII HELLO — bytes are correct
40 byte(s) sent OK — write() loop completed without error


Layer 2 — Non-Blocking RX with select()  ✅
How: RX times out cleanly after exactly 3 seconds per attempt (firmware not echoing)
Output:
[RX] Attempt 1/5 — waiting up to 3 s...
[RX] TIMEOUT — no data received within 3 s
[RX] Attempt 2/5 — waiting up to 3 s...
[RX] TIMEOUT — no data received within 3 s
...
[INFO]  Terminal restored. Device closed cleanly.
Why this is correct:

Program did NOT hang — select() returned after exactly 3 seconds each time
All 5 attempts completed — the loop ran correctly
Terminal was restored — shell remained usable after exit
TIMEOUT is the correct result when firmware does not echo — it is not a failure of the code


Layer 3 — Full TX + RX PASS via socat Virtual Loopback  ✅
How: socat creates a virtual serial pair. Program writes to one end, reply is injected from the other.
Setup:
bash# Terminal 1
socat -d -d pty,raw,echo=0 pty,raw,echo=0
# Output: /dev/pts/3  <-->  /dev/pts/4

# Terminal 2
./uart /dev/pts/3

# Terminal 3 (while program is waiting)
echo "REPLY FROM DEVICE OK" > /dev/pts/4
Full output (actual terminal):
-----------------------------------------------------------
  UART Validation Tool — RISC-V ACT Framework (v2)
-----------------------------------------------------------
[INFO]  Device (from argument) : /dev/pts/3
[INFO]  Opened '/dev/pts/3'  (fd = 3)
[INFO]  UART configured:
[INFO]    Baud rate : 115200
[INFO]    Data bits : 8
[INFO]    Parity    : None
[INFO]    Stop bits : 1
[INFO]    Flow ctrl : None
[INFO]    Mode      : Raw (8N1)
-----------------------------------------------------------
[TX] Transmitting test message (40 bytes)
[TX] hex dump (40 bytes):
       00: 48 45 4C 4C 4F 20 46 52  HELLO FR
       08: 4F 4D 20 4C 49 4E 55 58  OM LINUX
       10: 20 55 41 52 54 20 2D 20   UART -
       18: 52 49 53 43 2D 56 20 41  RISC-V A
       20: 43 54 20 54 45 53 54 0A  CT TEST.
[TX] 40 byte(s) sent OK
-----------------------------------------------------------
[RX] Listening for replies (Ctrl+C to stop early)
[RX] Timeout per attempt : 3 s   Max attempts : 5
-----------------------------------------------------------

[RX] Attempt 1/5 — waiting up to 3 s...
[RX] hex dump (21 bytes):
       00: 52 45 50 4C 59 20 46 52  REPLY FR
       08: 4F 4D 20 44 45 56 49 43  OM DEVIC
       10: 45 20 4F 4B 0A           E OK.
[RX] ASCII  : "REPLY FROM DEVICE OK."
[RX] PASS   — 21 byte(s) received this attempt
[TX] Sending next message...
[TX] 40 byte(s) sent OK

[RX] Attempt 2/5 — waiting up to 3 s...
[RX] TIMEOUT — no data received within 3 s

[RX] Attempt 3/5 — waiting up to 3 s...
[RX] TIMEOUT — no data received within 3 s

[RX] Attempt 4/5 — waiting up to 3 s...
[RX] hex dump (21 bytes):
       00: 52 45 50 4C 59 20 46 52  REPLY FR
       08: 4F 4D 20 44 45 56 49 43  OM DEVIC
       10: 45 20 4F 4B 0A           E OK.
[RX] ASCII  : "REPLY FROM DEVICE OK."
[RX] PASS   — 21 byte(s) received this attempt
[TX] Sending next message...
[TX] 40 byte(s) sent OK

[RX] Attempt 5/5 — waiting up to 3 s...
[RX] TIMEOUT — no data received within 3 s
-----------------------------------------------------------
[SUMMARY] Device         : /dev/pts/3
[SUMMARY] TX message     : "HELLO FROM LINUX UAR..."
[SUMMARY] Baud / format  : 115200 8N1
[SUMMARY] RX attempts    : 5
[SUMMARY] RX successes   : 2
[SUMMARY] RX timeouts    : 3
[SUMMARY] Total RX bytes : 42
-----------------------------------------------------------
[RESULT]  *** FULL PASS *** — TX + RX both verified
          2/5 attempt(s) received data
-----------------------------------------------------------
[INFO]  Terminal restored. Device closed cleanly.
-----------------------------------------------------------
socat log confirming data flow:
write(7, ..., 40) completed    ← TX from Linux reached virtual port
write(5, ..., 21) completed    ← Reply traveled back to program
Why this is the right verification:

RX hex 52 45 50 4C 59 = ASCII REPLY — bytes received without corruption
select() detected data in both attempt 1 and attempt 4 — not a coincidence, timing was real
Attempts 2, 3, 5 timed out correctly — select() distinguishes data-ready from no-data perfectly
Total RX bytes: 42 = 2 × 21 bytes — math is exact
Terminal restored. Device closed cleanly. — no resource leak, no raw-mode shell


