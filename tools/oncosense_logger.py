#!/usr/bin/env python3
"""Record OncoSense Band measurements from USB serial into CSV files.

The firmware prints a CSV header line (starting with ``t_ms``) whenever CSV
output is turned on or the mode changes. Each header starts a new file, so
every file has a single consistent set of columns.

Examples
--------
    # list serial ports
    python oncosense_logger.py --list

    # hydration mode, CSV output, label the trial, stop after 60 s
    python oncosense_logger.py --port /dev/cu.usbmodem1101 --send 1c \
        --label moisturized --seconds 60
"""

from __future__ import annotations

import argparse
import datetime as dt
import pathlib
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:  # pragma: no cover
    sys.exit("pyserial is required:  pip install -r tools/requirements.txt")


def list_serial_ports() -> None:
    ports = list(list_ports.comports())
    if not ports:
        print("No serial ports found. Is the band plugged in over USB?")
    for p in ports:
        print(f"{p.device:30s} {p.description}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port, e.g. /dev/cu.usbmodem1101 or COM5")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--out", default="data", help="output directory (default: data/)")
    ap.add_argument("--label", default="", help="trial label added as a column, e.g. dry / moisturized")
    ap.add_argument("--send", default="", help="command characters to send on connect, e.g. '1c'")
    ap.add_argument("--seconds", type=float, default=0, help="stop after N seconds (0 = until Ctrl-C)")
    ap.add_argument("--list", action="store_true", help="list serial ports and exit")
    args = ap.parse_args()

    if args.list:
        list_serial_ports()
        return 0
    if not args.port:
        ap.error("--port is required (use --list to find it)")

    out_dir = pathlib.Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    label = args.label.strip().replace(",", "_")

    current = None
    segment = 0
    rows = 0
    start = time.monotonic()

    with serial.Serial(args.port, args.baud, timeout=1) as ser:
        time.sleep(2.0)  # board resets / enumerates when the port opens
        ser.reset_input_buffer()
        for ch in args.send:
            ser.write(ch.encode())
            time.sleep(0.3)
        print(f"Logging from {args.port}. Ctrl-C to stop.")
        try:
            while not args.seconds or time.monotonic() - start < args.seconds:
                line = ser.readline().decode("utf-8", errors="replace").strip()
                if not line:
                    continue
                if line.startswith("#") or line.startswith("["):
                    print(line)  # status messages and readable output
                    continue
                if line.startswith("t_ms,"):
                    if current:
                        current.close()
                    segment += 1
                    path = out_dir / f"oncosense_{stamp}_{segment:02d}{'_' + label if label else ''}.csv"
                    current = open(path, "w", encoding="utf-8")
                    current.write(line + (",label" if label else "") + "\n")
                    print(f"-> writing {path}")
                    continue
                if current:
                    current.write(line + ("," + label if label else "") + "\n")
                    current.flush()
                    rows += 1
                    if rows % 10 == 0:
                        print(f"   {rows} rows", end="\r")
        except KeyboardInterrupt:
            pass
        finally:
            if current:
                current.close()

    if rows == 0:
        print("No CSV rows received. Send 'c' (or use --send c) to enable CSV output.")
    else:
        print(f"\nSaved {rows} rows.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
