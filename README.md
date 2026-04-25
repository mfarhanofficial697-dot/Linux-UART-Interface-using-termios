📌 Overview

This project is a production-quality UART validation tool written in C for Linux using the termios API. It was developed as part of the RISC-V ACT Framework enablement challenge and goes beyond a simple serial communication example by implementing a complete validation pipeline for UART-based hardware.

The tool is designed to be robust, automated, and practical for real-world embedded development and firmware testing scenarios.

⚙️ Core Functionality

The program begins by automatically detecting UART devices, probing common paths such as /dev/ttyACM0, /dev/ttyACM1, /dev/ttyUSB0, and /dev/ttyUSB1. This removes the need for manual configuration and improves usability.

Once a device is found, it configures the UART interface using termios with standard communication parameters: 115200 baud rate, 8 data bits, no parity, and 1 stop bit (8N1). The device operates in raw mode with flow control disabled, ensuring direct and predictable data handling.

📤 Transmission (TX)

For transmission, the tool ensures reliability by handling partial writes using a loop around write(). It guarantees that all bytes are transmitted successfully and uses tcdrain() to flush the output buffer before proceeding.

This approach ensures that data is not lost or truncated during communication.

📥 Reception (RX)

The receive mechanism is implemented using a non-blocking approach with select() and a timeout. This prevents the program from hanging indefinitely and allows controlled waiting for incoming data.

Data received is displayed in a structured hex dump format, including byte offsets, hexadecimal values, and ASCII representation. The RX logic performs up to five attempts, where successful receptions trigger retransmission, while timeouts are handled gracefully.

🛑 Signal & Error Handling

The program includes robust signal handling, capturing SIGINT (Ctrl+C) to ensure the terminal is restored properly before exit. This prevents terminal corruption issues commonly seen in serial applications.

Error handling covers real-world scenarios such as:

Missing devices (ENOENT)
Permission issues (EACCES)
Device busy errors (EBUSY)

Each case provides clear, actionable suggestions. Additionally, interrupted system calls (EINTR) are automatically handled, and device disconnection is detected during read operations.
🛠️ Build Instructions

To compile the program, use GCC on a Linux system:

gcc -Wall -Wextra -o uart uart_test.c

The code compiles cleanly with zero warnings, ensuring high code quality.

▶️ Run Instructions

You can run the program in two ways:

🔹 Auto-detect UART device
./uart
🔹 Specify device manually
./uart /dev/ttyACM0
./uart /dev/ttyUSB0
./uart /dev/pts/3

🧪 Hardware & Test Environment

The tool was tested using a Raspberry Pi Pico (RP2040) operating in USB CDC mode as a virtual serial device on Ubuntu Linux.

This setup provides a realistic environment for validating UART communication over USB-based serial interfaces.

✅ Verification Strategy

The validation process was divided into three layers to ensure correctness and reliability.

🔹 Layer 1 — UART Configuration & TX

The device was successfully auto-detected, configured, and used to transmit a 40-byte test message. This confirmed correct operation of open(), termios configuration, and transmission logic.

🔹 Layer 2 — Non-Blocking RX

The program consistently timed out after exactly three seconds per attempt when no data was received. This demonstrated correct behavior of the select()-based timeout mechanism without any blocking or hanging.

🔹 Layer 3 — Full TX/RX Validation

Using a virtual loopback created with socat, the tool successfully received injected responses, correctly distinguished between valid data and timeouts, and maintained accurate byte counts and attempt tracking.

📊 Results

The final execution produced a detailed summary including:

Number of RX attempts
Successful receptions
Timeouts
Total bytes received

The results confirmed data integrity, correct timing behavior, and stable execution, with no resource leaks and proper terminal restoration.

🧾 Conclusion

This project demonstrates strong expertise in Linux system programming and UART communication using termios. It highlights the ability to build robust, production-ready tools with proper error handling, non-blocking I/O, and hardware validation techniques.

Overall, it serves as a reliable UART validation tool suitable for embedded systems development, firmware testing, and debugging workflows.
