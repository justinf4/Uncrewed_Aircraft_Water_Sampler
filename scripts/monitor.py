#!/usr/bin/env python3
"""
Serial monitor for WS_PIO project.
Usage:
    python monitor.py                        # auto-detect port, run forever
    python monitor.py COM11                  # specify port, run forever
    python monitor.py COM11 115200           # specify port + baud, run forever
    python monitor.py COM11 115200 --timeout 10   # stop after 10 seconds
    python monitor.py COM11 115200 --lines 20     # stop after 20 lines received
"""

import serial
import serial.tools.list_ports
import sys
import time
from datetime import datetime


def find_port():
    ports = serial.tools.list_ports.comports()
    for p in ports:
        desc = p.description.upper()
        if any(k in desc for k in ("USB", "CP210", "CH340", "UART", "ESP32", "NANO")):
            return p.device
    if ports:
        print(f"[WARN] Could not auto-identify board. Using first available: {ports[0].device}")
        return ports[0].device
    return None


def list_ports():
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("No serial ports found.")
    else:
        print("Available serial ports:")
        for p in ports:
            print(f"  {p.device:10s}  {p.description}")


def monitor(port=None, baud=115200, timeout_sec=None, max_lines=None):
    if port is None:
        port = find_port()
        if port is None:
            print("ERROR: No serial port found.")
            list_ports()
            sys.exit(1)

    print(f"Connecting to {port} @ {baud} baud...")
    try:
        ser = serial.Serial(port, baud, timeout=1)
        limit_str = f"timeout={timeout_sec}s" if timeout_sec else f"max_lines={max_lines}" if max_lines else "Ctrl+C to stop"
        print(f"Connected. Monitoring — {limit_str}\n")

        lines_received = 0
        start_time = time.time()

        while True:
            if timeout_sec and (time.time() - start_time) >= timeout_sec:
                print(f"\n[MONITOR] Timeout after {timeout_sec}s. Lines received: {lines_received}")
                break
            if max_lines and lines_received >= max_lines:
                print(f"\n[MONITOR] Reached {max_lines} lines.")
                break

            line = ser.readline()
            if line:
                ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
                decoded = line.decode("utf-8", errors="replace").rstrip()
                print(f"[{ts}]  {decoded}", flush=True)
                lines_received += 1

    except serial.SerialException as e:
        print(f"Serial error: {e}")
        list_ports()
        sys.exit(1)
    except KeyboardInterrupt:
        print("\nMonitoring stopped.")
    finally:
        try:
            ser.close()
        except Exception:
            pass


if __name__ == "__main__":
    args = sys.argv[1:]

    port_arg    = args[0] if len(args) > 0 and not args[0].startswith("--") else None
    baud_arg    = int(args[1]) if len(args) > 1 and not args[1].startswith("--") else 115200
    timeout_arg = None
    lines_arg   = None

    for i, a in enumerate(args):
        if a == "--timeout" and i + 1 < len(args):
            timeout_arg = float(args[i + 1])
        if a == "--lines" and i + 1 < len(args):
            lines_arg = int(args[i + 1])

    monitor(port_arg, baud_arg, timeout_arg, lines_arg)
