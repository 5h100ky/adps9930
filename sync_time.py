#!/usr/bin/env python3
"""
sync_time.py – PC local time auto-sync for the APDS-9930 / DS3231 monitor
===========================================================================

Run this script on the PC before (or just after) powering / re-plugging the
RP2040 board.  It opens the serial port, waits for the board to print "TIME?"
(sent automatically at the end of setup()), and immediately replies with the
current local wall-clock time in the format the firmware expects:

    T<YYYY-MM-DD HH:MM:SS>

After the initial sync the script stays connected and keeps listening so you
can also send manual commands (type the T… string and press Enter).
Press Ctrl+C to quit.

Usage
-----
    python sync_time.py [PORT] [BAUD]

Defaults
--------
    PORT  – auto-detected (first USB-serial device found), or
            Windows: COM3
            Linux  : /dev/ttyACM0
            macOS  : /dev/cu.usbmodem*
    BAUD  – 115200

Examples
--------
    python sync_time.py                        # auto-detect port
    python sync_time.py COM5                   # Windows, explicit port
    python sync_time.py /dev/ttyACM1 115200    # Linux, explicit port + baud
"""

import sys
import glob
import threading
import datetime
import time

try:
    import serial
except ImportError:
    print("pyserial is not installed.  Install it with:")
    print("    pip install pyserial")
    sys.exit(1)


# ---------------------------------------------------------------------------
# Port auto-detection
# ---------------------------------------------------------------------------
def _auto_detect_port() -> str:
    """Return the first plausible USB-serial port, or a sensible default."""
    candidates: list[str] = []

    # Linux / macOS
    for pattern in ("/dev/ttyACM*", "/dev/ttyUSB*", "/dev/cu.usbmodem*",
                    "/dev/cu.usbserial*"):
        candidates.extend(sorted(glob.glob(pattern)))

    # Windows – try COM3..COM20
    if sys.platform.startswith("win"):
        for n in range(3, 21):
            candidates.append(f"COM{n}")

    if candidates:
        return candidates[0]

    # Hard fallbacks
    if sys.platform.startswith("win"):
        return "COM3"
    return "/dev/ttyACM0"


# ---------------------------------------------------------------------------
# Time-sync helper
# ---------------------------------------------------------------------------
def _make_sync_cmd() -> bytes:
    """Return a T-command string with the current local time, UTF-8 encoded."""
    ts = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    return f"T{ts}\n".encode()


# ---------------------------------------------------------------------------
# Background reader – prints board output and reacts to "TIME?" automatically
# ---------------------------------------------------------------------------
def _reader(ser: serial.Serial) -> None:
    buf = b""
    while True:
        try:
            chunk = ser.read(ser.in_waiting or 1)
        except serial.SerialException:
            print("\n[sync_time] Serial connection lost.")
            break
        if not chunk:
            continue

        buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            text = line.decode(errors="replace").strip()
            if text:
                print(f"[board] {text}")
            if text == "TIME?":
                cmd = _make_sync_cmd()
                try:
                    ser.write(cmd)
                    ts = cmd.decode().strip()
                    print(f"[sync ] Sent → {ts}")
                except serial.SerialException:
                    print("[sync ] Failed to send time – port closed.")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main() -> None:
    port = sys.argv[1] if len(sys.argv) > 1 else _auto_detect_port()
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else 115200

    print(f"[sync_time] Connecting to {port} at {baud} baud …")
    try:
        ser = serial.Serial(port, baud, timeout=0.1)
    except serial.SerialException as exc:
        print(f"[sync_time] Cannot open {port}: {exc}")
        sys.exit(1)

    print("[sync_time] Connected.  Waiting for board to send TIME? …")
    print("[sync_time] You can also type  T<YYYY-MM-DD HH:MM:SS>  and press Enter.")
    print("[sync_time] Press Ctrl+C to quit.\n")

    # Start background reader thread
    t = threading.Thread(target=_reader, args=(ser,), daemon=True)
    t.start()

    # Allow manual input from the terminal as well
    try:
        while t.is_alive():
            line = input()          # blocks until user presses Enter
            if line:
                ser.write((line + "\n").encode())
    except (KeyboardInterrupt, EOFError):
        pass
    finally:
        ser.close()
        print("\n[sync_time] Disconnected.")


if __name__ == "__main__":
    main()
