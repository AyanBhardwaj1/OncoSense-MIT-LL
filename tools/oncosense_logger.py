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
        --label dry --seconds 60
"""

from __future__ import annotations

import argparse
import datetime as dt
import pathlib
import re
import sys
import time
from dataclasses import dataclass, field
from typing import Callable, TextIO

# If data rows arrive with no header (the band was already in CSV mode), this
# command sequence makes the firmware print a fresh header: on v2 'c' toggles
# CSV off and back on; on v3 'c' (re)selects CSV, so it works on both.
REQUEST_HEADER = "cc"


def safe_label(label: str) -> tuple[str, str]:
    """(value for the label column, value safe to put in a file name)."""
    column = label.strip().replace(",", "_")
    filename = re.sub(r"[^\w.-]+", "_", column)
    filename = re.sub(r"_+", "_", filename).strip("_")
    return column, filename


@dataclass
class Session:
    """Routes device lines into per-header CSV files. Pure apart from `opener`."""

    out_dir: pathlib.Path
    stamp: str
    label: str = ""
    opener: Callable[[pathlib.Path], TextIO] = lambda p: open(p, "w", encoding="utf-8")
    current: TextIO | None = None
    paths: list[pathlib.Path] = field(default_factory=list)
    rows: int = 0
    orphan_rows: int = 0

    def handle(self, line: str) -> str:
        """Process one line. Returns 'message', 'header', 'row', 'orphan' or 'empty'."""
        line = line.strip()
        if not line:
            return "empty"
        if line.startswith("#") or line.startswith("["):
            return "message"
        column, fname = safe_label(self.label)
        if line.startswith("t_ms,"):
            self.close()
            path = self.out_dir / f"oncosense_{self.stamp}_{len(self.paths) + 1:02d}{'_' + fname if fname else ''}.csv"
            self.current = self.opener(path)
            self.paths.append(path)
            self.current.write(line + (",label" if column else "") + "\n")
            return "header"
        if self.current is None:
            self.orphan_rows += 1
            return "orphan"
        self.current.write(line + ("," + column if column else "") + "\n")
        self.current.flush()
        self.rows += 1
        return "row"

    def close(self) -> None:
        if self.current is not None:
            self.current.close()
            self.current = None


def list_serial_ports() -> None:
    from serial.tools import list_ports

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
    ap.add_argument("--label", default="", help="trial label added as a column, e.g. dry / well_hydrated")
    ap.add_argument("--send", default="", help="command characters to send on connect, e.g. '1c'")
    ap.add_argument("--seconds", type=float, default=0, help="stop after N seconds (0 = until Ctrl-C)")
    ap.add_argument("--list", action="store_true", help="list serial ports and exit")
    args = ap.parse_args()

    try:
        import serial
    except ImportError:
        print("pyserial is required:  pip install -r tools/requirements.txt", file=sys.stderr)
        return 2

    if args.list:
        list_serial_ports()
        return 0
    if not args.port:
        ap.error("--port is required (use --list to find it)")

    out_dir = pathlib.Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    session = Session(out_dir, dt.datetime.now().strftime("%Y%m%d-%H%M%S"), args.label)
    start = time.monotonic()
    asked_for_header = False

    with serial.Serial(args.port, args.baud, timeout=1) as ser:
        time.sleep(2.0)  # let the board finish enumerating
        ser.reset_input_buffer()
        for ch in args.send:
            ser.write(ch.encode())
            time.sleep(0.3)
        print(f"Logging from {args.port}. Ctrl-C to stop.")
        try:
            while not args.seconds or time.monotonic() - start < args.seconds:
                line = ser.readline().decode("utf-8", errors="replace").strip()
                kind = session.handle(line)
                if kind == "message":
                    print(line)
                elif kind == "header":
                    print(f"-> writing {session.paths[-1]}")
                elif kind == "orphan" and not asked_for_header:
                    print("Data arrived without a CSV header; asking the band for a new one.")
                    ser.write(REQUEST_HEADER.encode())
                    asked_for_header = True
                elif kind == "row" and session.rows % 10 == 0:
                    print(f"   {session.rows} rows", end="\r")
        except KeyboardInterrupt:
            pass
        finally:
            session.close()

    if session.rows == 0:
        print("No CSV rows received. Send 'c' (or use --send c) to enable CSV output.", file=sys.stderr)
        return 1
    print(f"\nSaved {session.rows} rows.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
