"""Tests for tools/oncosense_logger.py and tools/analyze_session.py.

    python -m pytest tests -q
"""

from __future__ import annotations

import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from oncosense_logger import Session, safe_label  # noqa: E402

ANALYZER = ROOT / "tools" / "analyze_session.py"
HEADER = "t_ms,mode,led,gain,signal,ambient,metric,trend,state,flags"


def analyze(*files, out) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(ANALYZER), *map(str, files), "--out", str(out)],
                          capture_output=True, text=True, env={**os.environ, "MPLBACKEND": "Agg"})


# ---------------------------------------------------------------- logger --

def test_safe_label_keeps_column_and_cleans_file_name():
    column, fname = safe_label(" dry/left arm, trial 2 ")
    assert column == "dry/left arm_ trial 2"
    assert fname == "dry_left_arm_trial_2"


def test_session_splits_files_per_header_and_appends_label(tmp_path):
    s = Session(tmp_path, "20250801-120000", label="dry/left")
    lines = ["# banner", "", HEADER, "100,hydration,660nm,64.0,0.02,0.0,nan,nan,Dry,OK",
             "t_ms,mode,led,gain,f415", "200,spectral,white,16.0,0.1"]
    kinds = [s.handle(ln) for ln in lines]
    s.close()
    assert kinds == ["message", "empty", "header", "row", "header", "row"]
    assert len(s.paths) == 2 and all(p.name.endswith("_dry_left.csv") for p in s.paths)
    first = s.paths[0].read_text().splitlines()
    assert first[0].endswith(",label") and first[1].endswith(",dry/left")
    assert s.rows == 2


def test_session_counts_rows_that_arrive_before_any_header(tmp_path):
    s = Session(tmp_path, "x")
    assert s.handle("100,hydration,660nm,64.0,0.02") == "orphan"
    assert s.orphan_rows == 1 and s.rows == 0 and not s.paths


class FakeSerial:
    """Stands in for serial.Serial: replays device lines, records writes."""

    written: list[bytes] = []

    def __init__(self, lines: list[str]):
        self.lines = [ln.encode() + b"\r\n" for ln in lines]

    def __call__(self, *args, **kwargs):
        return self

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False

    def reset_input_buffer(self):
        pass

    def write(self, data: bytes):
        FakeSerial.written.append(data)

    def readline(self) -> bytes:
        if not self.lines:
            raise KeyboardInterrupt  # end of the recorded session
        return self.lines.pop(0)


def run_logger(monkeypatch, tmp_path, lines, *args):
    import types

    import oncosense_logger

    fake = types.ModuleType("serial")
    fake.Serial = FakeSerial(lines)
    monkeypatch.setitem(sys.modules, "serial", fake)
    monkeypatch.setattr(oncosense_logger.time, "sleep", lambda s: None)
    monkeypatch.setattr(sys, "argv", ["logger", "--port", "FAKE", "--out", str(tmp_path), *args])
    FakeSerial.written = []
    return oncosense_logger.main()


def test_logger_main_records_a_labelled_session(monkeypatch, tmp_path, capsys):
    lines = ["# OncoSense Band firmware v2.0.0", HEADER,
             "1000,hydration,660nm,64.0,0.02,0.0,nan,nan,Dry,OK",
             "2000,hydration,660nm,64.0,0.02,0.0,nan,nan,Dry,OK"]
    assert run_logger(monkeypatch, tmp_path, lines, "--label", "dry", "--send", "1c") == 0
    files = list(tmp_path.glob("*_dry.csv"))
    assert len(files) == 1 and len(files[0].read_text().splitlines()) == 3
    assert FakeSerial.written == [b"1", b"c"]
    assert "Saved 2 rows" in capsys.readouterr().out


def test_logger_asks_for_a_header_when_data_arrives_without_one(monkeypatch, tmp_path, capsys):
    lines = ["1000,hydration,660nm,64.0,0.02,0.0,nan,nan,Dry,OK", HEADER,
             "2000,hydration,660nm,64.0,0.02,0.0,nan,nan,Dry,OK"]
    assert run_logger(monkeypatch, tmp_path, lines) == 0
    assert b"cc" in FakeSerial.written
    assert "without a CSV header" in capsys.readouterr().out


def test_logger_exits_non_zero_when_nothing_was_recorded(monkeypatch, tmp_path, capsys):
    assert run_logger(monkeypatch, tmp_path, ["# banner only"]) == 1
    assert "No CSV rows received" in capsys.readouterr().err


def test_logger_without_pyserial_fails_cleanly():
    code = ("import sys; sys.modules['serial'] = None; sys.argv = ['x', '--list'];"
            f"sys.path.insert(0, {str(ROOT / 'tools')!r});"
            "import oncosense_logger as lg; sys.exit(lg.main())")
    res = subprocess.run([sys.executable, "-c", code], capture_output=True, text=True)
    assert res.returncode == 2 and "pyserial is required" in res.stderr


# -------------------------------------------------------------- analyzer --

def write(path: pathlib.Path, rows: list[str], header: str = HEADER) -> pathlib.Path:
    path.write_text("\n".join([header, *rows]) + "\n")
    return path


def test_analyzer_rejects_empty_and_header_only_files(tmp_path):
    empty = tmp_path / "empty.csv"
    empty.write_text("")
    header_only = write(tmp_path / "h.csv", [])
    res = analyze(empty, header_only, out=tmp_path / "figs")
    assert res.returncode == 1
    assert "skipping" in res.stderr and "no data rows" in res.stderr


def test_analyzer_handles_arduino_nan_and_ovf_tokens(tmp_path):
    f = write(tmp_path / "a.csv", ["0,hydration,660nm,64.0,0.020,0.0,nan,nan,Dry,OK",
                                   "1000,hydration,660nm,64.0,0.021,0.0,ovf,inf,Dry,OK",
                                   "2000,hydration,660nm,64.0,0.022,0.0,40.0,40.0,Dry,OK"],
              header=HEADER + ",label")
    res = analyze(f, out=tmp_path / "figs")
    assert res.returncode == 0, res.stderr
    assert (tmp_path / "figs" / "session_timeseries.png").exists()


def test_analyzer_never_mixes_light_sources_in_one_statistic(tmp_path):
    a = write(tmp_path / "a.csv", [f"{i*1000},hydration,660nm,64.0,0.20,0,0,0,Dry,OK,dry" for i in range(3)],
              header=HEADER + ",label")
    b = write(tmp_path / "b.csv", [f"{i*1000},scan,850nm,64.0,0.70,0,1,1,Normal skin,OK,dry" for i in range(3)],
              header=HEADER + ",label")
    res = analyze(a, b, out=tmp_path / "figs")
    assert res.returncode == 0, res.stderr
    assert "dry (hydration, 660nm)" in res.stdout and "dry (scan, 850nm)" in res.stdout


def test_analyzer_reports_separation_between_conditions(tmp_path):
    rows = [f"{i*1000},hydration,660nm,64.0,{0.20 + 0.001 * (i % 3)},0,0,0,Dry,OK,dry" for i in range(6)]
    rows += [f"{i*1000},hydration,660nm,64.0,{0.30 + 0.001 * (i % 3)},0,0,0,Normal,OK,moist" for i in range(6)]
    res = analyze(write(tmp_path / "s.csv", rows, header=HEADER + ",label"), out=tmp_path / "figs")
    assert res.returncode == 0, res.stderr
    assert "Cohen's d" in res.stdout


def test_analyzer_excludes_flagged_rows(tmp_path):
    rows = ["0,hydration,660nm,64.0,0.2,0,0,0,Dry,OK,x", "1000,hydration,660nm,64.0,9.9,0,0,0,No contact,NO_CONTACT,x"]
    res = analyze(write(tmp_path / "f.csv", rows, header=HEADER + ",label"), out=tmp_path / "figs")
    assert res.returncode == 0 and "1 flagged rows excluded" in res.stdout


def test_analyzer_spectral_summary(tmp_path):
    header = "t_ms,mode,led,gain,f415,f445,f480,f515,f555,f590,f630,f680,clear,nir,melanin_idx,erythema_idx,flags"
    rows = [f"{i},spectral,white,16.0," + ",".join(["0.3"] * 10) + ",52.3,0.0,OK" for i in range(3)]
    res = analyze(write(tmp_path / "sp.csv", rows, header=header), out=tmp_path / "figs")
    assert res.returncode == 0, res.stderr
    assert (tmp_path / "figs" / "session_spectrum.png").exists()
