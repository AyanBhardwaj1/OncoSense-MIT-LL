"""Firmware-in-the-loop tests for firmware v2.

The real firmware (firmware/OncoSense, unmodified) is compiled for the host
against simulated Arduino, AS7341 and flash hardware (this folder), then
driven through its serial interface exactly as a user would.

    python -m pytest tests/firmware_sim -q
"""

from __future__ import annotations

import math
import os
import pathlib
import shutil
import subprocess

import pytest

HERE = pathlib.Path(__file__).resolve().parent
FW = HERE.parents[1] / "firmware" / "OncoSense"
COLUMNS = ["t_ms", "mode", "led", "gain", "signal", "ambient", "metric", "trend", "state", "flags"]


@pytest.fixture(scope="session")
def harness(tmp_path_factory) -> pathlib.Path:
    cxx = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
    if not cxx:
        pytest.skip("no C++ compiler")
    out = tmp_path_factory.mktemp("build") / "harness"
    sources = [str(HERE / "harness.cpp")] + [str(p) for p in sorted(FW.glob("*.cpp"))]
    extra = os.environ.get("ONCOSENSE_CXXFLAGS", "").split()  # e.g. coverage flags
    res = subprocess.run([cxx, "-std=gnu++11", "-O1", "-Wall", "-Wextra", "-Werror", *extra,
                          f"-I{HERE}", f"-I{FW}", "-o", str(out), *sources],
                         capture_output=True, text=True)
    assert res.returncode == 0, res.stderr
    return out


class Run:
    def __init__(self, stdout: str):
        self.stdout = stdout
        self.lines = [ln.rstrip("\r") for ln in stdout.splitlines()]

    def messages(self) -> list[str]:
        return [ln[2:] for ln in self.lines if ln.startswith("# ")]

    def rows(self, mode: str | None = None) -> list[dict]:
        """CSV readings as dicts (hydration / scan header)."""
        out, header = [], None
        for ln in self.lines:
            if ln.startswith("t_ms,"):
                header = ln.split(",")
            elif header and ln[:1].isdigit():
                row = dict(zip(header, ln.split(",")))
                if mode is None or row.get("mode") == mode:
                    out.append(row)
        return out


def run(harness: pathlib.Path, script: list[str]) -> Run:
    res = subprocess.run([str(harness)], input="\n".join(script) + "\n", capture_output=True,
                         text=True, timeout=120)
    assert res.returncode == 0, res.stderr
    return Run(res.stdout)


def source(idx: int, red680=0.24, nir=0.30, white=0.2) -> str:
    """One light source's per-channel return (AS7341 channel order)."""
    ch = [0.0] * 12
    if idx == 0:
        ch[11] = ch[5] = nir
        ch[10] = ch[4] = nir * 0.6
    elif idx == 1:
        ch[9] = red680
        ch[8] = red680 * 0.4
        ch[10] = ch[4] = red680 * 1.2
    else:
        for c in (0, 1, 2, 3, 6, 7, 8, 9):
            ch[c] = white
        ch[10] = ch[4] = white * 8
        ch[11] = ch[5] = white * 0.1
    return f"scene src {idx} " + " ".join(f"{v:.6g}" for v in ch)


def skin(red680=0.24, nir=0.30, white=0.2) -> list[str]:
    return [source(0, nir=nir), source(1, red680=red680), source(2, white=white)]


BASE = ["boot", *skin(), "send c"]


def num(v: str) -> float:
    return float("nan") if v in ("nan", "inf", "ovf") else float(v)


def test_boot_banner(harness):
    r = run(harness, ["boot", "run 10"])
    assert "OncoSense Band firmware v2.0.0" in r.stdout


def test_missing_sensor_reports_error_and_halts(harness):
    r = run(harness, ["scene present 0", "boot"])
    assert "ERROR: AS7341 not found" in r.stdout and "@@ halted" in r.stdout


@pytest.mark.parametrize("mode", ["1", "2", "3"])
def test_csv_rows_match_their_header(harness, mode):
    r = run(harness, ["boot", *skin(), f"send {mode}", "send c", "run 8000"])
    header, rows = None, 0
    for ln in r.lines:
        if ln.startswith("t_ms,"):
            header = ln.split(",")
        elif header and ln[:1].isdigit():
            assert len(ln.split(",")) == len(header), (header, ln)
            rows += 1
    assert rows >= 3


def test_uncalibrated_hydration_uses_v1_thresholds(harness):
    rows = run(harness, BASE + ["run 4000"]).rows("hydration")
    assert rows and rows[-1]["state"] in {"Very Dry", "Dry", "Normal", "Occlusive Film"}


def test_two_point_calibration_scores_linearly(harness):
    r = run(harness, ["boot", *skin(red680=0.20), "send d", "run 100", *skin(red680=0.30),
                      "send m", "run 100", *skin(red680=0.25), "send c", "run 6000"])
    assert num(r.rows("hydration")[-1]["metric"]) == pytest.approx(50, abs=3)


def test_no_contact_flag(harness):
    last = run(harness, BASE + ["scene contact 0", "run 4000"]).rows("hydration")[-1]
    assert "NO_CONTACT" in last["flags"] and last["state"] == "No contact"


def test_ambient_light_is_subtracted(harness):
    clean = num(run(harness, BASE + ["run 8000"]).rows("hydration")[-1]["signal"])
    lit = num(run(harness, BASE + ["scene ambient 0.05", "run 8000"]).rows("hydration")[-1]["signal"])
    assert lit == pytest.approx(clean, rel=0.05)


def test_ambient_leak_flag(harness):
    last = run(harness, BASE + ["scene ambient 1.0", "run 8000"]).rows("hydration")[-1]
    assert "AMBIENT_LEAK" in last["flags"]


def test_auto_gain_reaches_low_gains_without_flagging_saturation(harness):
    """Regression: SATURATED was reported after 3 gain steps although lower gains existed."""
    first = run(harness, ["boot", *skin(red680=144.0), "send c", "run 3000"]).rows("hydration")[0]
    assert "SATURATED" not in first["flags"]


def test_auto_gain_raises_gain_for_weak_signals(harness):
    rows = run(harness, ["boot", *skin(red680=0.012), "send c", "run 15000"]).rows("hydration")
    assert num(rows[-1]["gain"]) > num(rows[0]["gain"])


def test_sensor_read_failure_recovers(harness):
    r = run(harness, BASE + ["scene fail 1", "run 3000", "scene fail 0", "run 3000"])
    assert "Sensor read error." in r.messages() and r.rows("hydration")


def test_surface_scan_ratio(harness):
    r = run(harness, BASE + ["send 2", "send b", "run 100", *skin(nir=0.36), "run 6000"])
    assert num(r.rows("scan")[-1]["metric"]) == pytest.approx(1.2, rel=0.03)


def test_trend_is_nan_not_zero_before_any_valid_sample(harness):
    """Regression: an unprimed trend printed 0."""
    rows = run(harness, BASE + ["send 2", "run 4000"]).rows("scan")
    assert math.isnan(num(rows[-1]["trend"]))


def test_white_reference_rejects_frames_without_a_target(harness):
    """Regression: 'w' with nothing in front of the sensor stored garbage."""
    r = run(harness, BASE + ["send 3", "scene contact 0", "send w", "run 100", "send s", "run 10"])
    assert any("White reference failed" in m for m in r.messages())
    assert "white reference: no" in r.stdout


def test_white_reference_and_melanin_index(harness):
    card = ["scene src 2 " + " ".join(["0.5"] * 12)]
    r = run(harness, ["boot", "send 3", *card, "send w", "run 100", *skin(white=0.2), "run 4000"])
    last = [ln for ln in r.lines if ln.startswith("[spectral R]")][-1]
    assert "f630=0.4000" in last
    assert f"MI={100 * math.log10(1 / 0.4):.1f}" in last


def test_references_too_close_together_are_reported(harness):
    """Regression: identical dry/moist references silently gave 'Unknown' forever."""
    r = run(harness, BASE + ["send d", "run 100", "send m", "run 100"])
    assert any("too close" in m for m in r.messages())


def test_unknown_command(harness):
    r = run(harness, BASE + ["send Q", "run 10"])
    assert any("Unknown command 'Q'" in m for m in r.messages())


def test_end_to_end_firmware_to_logger_to_analyzer(harness, tmp_path):
    """Real firmware CSV -> logger Session -> analyzer, with a calibrated session."""
    import sys

    sys.path.insert(0, str(HERE.parents[1] / "tools"))
    from oncosense_logger import Session

    files = []
    for label, red in (("dry", 0.20), ("well_hydrated", 0.30)):
        out = run(harness, ["boot", *skin(red680=red), "send c", "run 12000"])
        session = Session(tmp_path, label, label)
        for line in out.lines:
            session.handle(line)
        session.close()
        files += session.paths
    res = subprocess.run([sys.executable, str(HERE.parents[1] / "tools" / "analyze_session.py"),
                          *map(str, files), "--out", str(tmp_path / "figs")],
                         capture_output=True, text=True, env={**os.environ, "MPLBACKEND": "Agg"})
    assert res.returncode == 0, res.stderr
    assert "Cohen's d" in res.stdout
