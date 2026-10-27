"""Firmware-in-the-loop tests.

The real firmware (firmware/OncoSense, unmodified) is compiled for the host
against simulated Arduino, AS7341 and flash hardware (this folder), then
driven through its serial interface exactly as a user or the web app would.

    python -m pytest tests/firmware_sim -q
"""

from __future__ import annotations

import json
import math
import os
import pathlib
import shutil
import subprocess
import sys

import numpy as np
import pytest

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
FW = ROOT / "firmware" / "OncoSense"
sys.path.insert(0, str(ROOT / "ml"))

import skin_optics_sim as sim  # noqa: E402
from features import extract  # noqa: E402

SOURCES = {"nir850": 0, "red660": 1, "white": 2}
# Firmware band order (F1..F8, Clear, NIR) -> AS7341 channel index.
BAND_TO_CHANNEL = [0, 1, 2, 3, 6, 7, 8, 9, 10, 11]


# ------------------------------------------------------------- harness --

@pytest.fixture(scope="session")
def harness(tmp_path_factory) -> pathlib.Path:
    cxx = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
    if not cxx:
        pytest.skip("no C++ compiler")
    out = tmp_path_factory.mktemp("build") / "harness"
    sources = [str(HERE / "harness.cpp")] + [str(p) for p in sorted(FW.glob("*.cpp"))]
    extra = os.environ.get("ONCOSENSE_CXXFLAGS", "").split()  # e.g. coverage flags
    cmd = [cxx, "-std=gnu++11", "-O1", "-Wall", "-Wextra", "-Werror", "-DARDUINO_ARCH_SAMD", *extra,
           f"-I{HERE}", f"-I{FW}", "-o", str(out), *sources]
    res = subprocess.run(cmd, capture_output=True, text=True)
    assert res.returncode == 0, res.stderr
    return out


class Run:
    def __init__(self, stdout: str):
        self.stdout = stdout
        self.lines = [ln.rstrip("\r") for ln in stdout.splitlines()]

    @property
    def json(self) -> list[dict]:
        return [json.loads(ln) for ln in self.lines if ln.startswith("{")]

    def of(self, kind: str) -> list[dict]:
        return [o for o in self.json if o.get("type") == kind]

    @property
    def status(self) -> dict:
        return self.of("status")[-1]

    def messages(self) -> list[str]:
        return [ln[2:] for ln in self.lines if ln.startswith("# ")]

    def csv_blocks(self) -> list[list[list[str]]]:
        blocks, cur = [], None
        for ln in self.lines:
            if ln.startswith("t_ms,"):
                cur = [ln.split(",")]
                blocks.append(cur)
            elif cur is not None and ln and ln[0].isdigit():
                cur.append(ln.split(","))
        return blocks


def run(harness: pathlib.Path, script: list[str], flash: pathlib.Path | None = None) -> Run:
    env = dict(os.environ)
    if flash is not None:
        env["ONCOSENSE_FLASH"] = str(flash)
    else:
        env.pop("ONCOSENSE_FLASH", None)
    res = subprocess.run([str(harness)], input="\n".join(script) + "\n", capture_output=True,
                         text=True, env=env, timeout=120)
    assert res.returncode == 0, res.stderr
    return Run(res.stdout)


# --------------------------------------------------------------- scenes --

def channels(bands: np.ndarray) -> list[float]:
    ch = [0.0] * 12
    for b, c in enumerate(BAND_TO_CHANNEL):
        ch[c] = float(bands[b])
    ch[4], ch[5] = ch[10], ch[11]  # first-SMUX Clear / NIR duplicates
    return ch


PERSON = sim.Person(melanin=0.06, blood=0.015, scatter_a=40, scatter_b=1.35)


def skin(hydration=0.55, occlusive=0.0, scale=1.0) -> dict[str, np.ndarray]:
    state = sim.SkinState(hydration=hydration, occlusive=occlusive, erythema=0.0, coupling=1.0)
    r = sim.reflectance(PERSON, state)
    return {src: r * sim.ILLUMINATION[src] * scale for src in sim.SOURCES}


def scene(s: dict[str, np.ndarray]) -> list[str]:
    return [f"scene src {SOURCES[k]} " + " ".join(f"{v:.6g}" for v in channels(v)) for k, v in s.items()]


BASE = ["boot", "send j", *scene(skin())]


# ---------------------------------------------------------------- tests --

def test_boot_prints_banner_and_valid_status(harness):
    r = run(harness, BASE + ["send s", "run 10"])
    assert "OncoSense Band firmware v3.0.0" in r.stdout
    st = r.status
    assert st["mode"] == "hydration" and st["streaming"] is True
    assert st["model"]["classes"] == sim.CLASSES
    assert st["calibration"]["ai_baseline"] is False


def test_missing_sensor_reports_error_and_halts(harness):
    r = run(harness, ["scene present 0", "boot"])
    assert "ERROR: AS7341 not found" in r.stdout
    assert "@@ halted" in r.stdout  # stays in its fault-blink loop


@pytest.mark.parametrize("mode", ["1", "2", "3", "4"])
def test_every_json_line_is_valid_in_every_mode(harness, mode):
    captures = {"1": "send d", "2": "send b", "3": "send w", "4": "send a"}[mode]
    r = run(harness, BASE + [f"send {mode}", "run 1500", captures, "run 15000", "send s", "run 10"])
    for line in r.lines:
        if line.startswith("{"):
            obj = json.loads(line)  # raises on NaN tokens or broken syntax
            assert "nan" not in line and "inf" not in line
            assert "type" in obj
    assert r.json, "no JSON produced"


def test_hydration_uses_v1_thresholds_until_calibrated(harness):
    r = run(harness, BASE + ["run 4000"])
    readings = r.of("hydration")
    assert readings and all(x["calibrated"] is False for x in readings)
    assert readings[-1]["state"] in {"Very Dry", "Dry", "Normal", "Occlusive Film"}


def test_two_point_calibration_scores_between_references(harness):
    dry, moist, mid = skin(hydration=0.05), skin(hydration=0.95), skin(hydration=0.5)
    r = run(harness, ["boot", "send j", *scene(dry), "run 3000", "send d", "run 100",
                      *scene(moist), "send m", "run 100", *scene(mid), "run 6000"])
    st = r.status["calibration"]
    assert st["dry"] is not None and st["moisturized"] is not None
    last = r.of("hydration")[-1]
    assert last["calibrated"] is True
    # The score is linear in signal between the two references.
    expected = 100 * (mid["red660"][7] - dry["red660"][7]) / (moist["red660"][7] - dry["red660"][7])
    assert last["metric"] == pytest.approx(expected, abs=3)


def test_no_contact_is_flagged_and_not_classified(harness):
    r = run(harness, BASE + ["scene contact 0", "run 4000"])
    last = r.of("hydration")[-1]
    assert "NO_CONTACT" in last["flags"]
    assert last["state"] == "No contact"


def test_ambient_light_is_subtracted(harness):
    clean = run(harness, BASE + ["run 8000"]).of("hydration")[-1]["signal"]
    lit = run(harness, BASE + ["scene ambient 0.01", "run 8000"]).of("hydration")[-1]
    assert lit["signal"] == pytest.approx(clean, rel=0.05)
    assert lit["ambient"] > 0.005


def test_room_light_leak_is_flagged(harness):
    r = run(harness, BASE + ["scene ambient 1.0", "run 8000"])
    assert "AMBIENT_LEAK" in r.of("hydration")[-1]["flags"]


def test_auto_gain_recovers_from_saturation(harness):
    bright = skin(scale=40.0)
    r = run(harness, ["boot", "send j", *scene(bright), "run 12000"])
    readings = r.of("hydration")
    assert "SATURATED" in readings[0]["flags"] or readings[0]["gain"] < 64
    assert "SATURATED" not in readings[-1]["flags"]
    assert readings[-1]["gain"] < 64


def test_auto_gain_raises_gain_for_weak_signals(harness):
    weak = skin(scale=0.05)
    r = run(harness, ["boot", "send j", *scene(weak), "run 15000"])
    gains = [x["gain"] for x in r.of("hydration")]
    assert gains[-1] > gains[0]


def test_sensor_read_failure_is_reported_not_fatal(harness):
    r = run(harness, BASE + ["scene fail 1", "run 3000", "scene fail 0", "run 3000"])
    assert "Sensor read error." in r.messages()
    assert r.of("hydration"), "firmware should recover after read errors"


def test_surface_scan_ratio_against_baseline(harness):
    film = skin(occlusive=1.0)
    r = run(harness, BASE + ["send 2", "run 100", "send b", "run 100", *scene(film), "run 6000"])
    last = r.of("scan")[-1]
    expected = film["nir850"][9] / skin()["nir850"][9]
    assert last["metric"] == pytest.approx(expected, rel=0.03)


def test_spectral_reflectance_and_indices_with_white_card(harness):
    white_card = {src: sim.ILLUMINATION[src] * 0.9 for src in sim.SOURCES}
    r = run(harness, BASE + ["send 3", "run 100", *scene(white_card), "send w", "run 100",
                             *scene(skin()), "run 6000"])
    last = r.of("spectral")[-1]
    assert last["reflectance"] is True
    refl = sim.reflectance(PERSON, sim.SkinState(0.55, 0.0, 0.0, 1.0)) / 0.9
    assert last["bands"]["f630"] == pytest.approx(refl[6], rel=0.03)
    assert last["melanin_idx"] == pytest.approx(100 * math.log10(1 / refl[6]), abs=1.5)


def test_ai_mode_requires_a_baseline(harness):
    r = run(harness, BASE + ["send 4", "run 6000"])
    assert not r.of("ai")
    assert any("needs a baseline" in m for m in r.messages())


def test_ai_prediction_matches_python_model(harness):
    """Full pipeline: sensor -> ambient correction -> features -> MLP, vs numpy."""
    model = json.loads((ROOT / "ml" / "model.json").read_text())
    base, now = skin(), skin(hydration=0.15)
    r = run(harness, BASE + ["send 4", "run 100", "send a", "run 100", *scene(now), "run 8000"])
    got = r.of("ai")[-1]

    x = extract({k: v.copy() for k, v in now.items()}, base)
    np.testing.assert_allclose(got["features"], x, atol=0.02)
    z = (np.array(got["features"]) - model["mean"]) / model["std"]
    W1, b1, W2, b2, W3, b3 = (np.array(w) for w in model["weights"])
    h = np.maximum(W2 @ np.maximum(W1 @ z + b1, 0) + b2, 0)
    logits = W3 @ h + b3
    p = np.exp(logits - logits.max())
    p /= p.sum()
    for i, c in enumerate(model["classes"]):
        assert got["probs"][c] == pytest.approx(p[i], abs=2e-3)
    assert got["label"] == model["classes"][int(p.argmax())]
    assert sum(got["probs"].values()) == pytest.approx(1.0, abs=1e-3)


def test_ai_baseline_scene_reads_normal_and_novelty_warms_up(harness):
    r = run(harness, BASE + ["scene noise 0.01", "send 4", "run 100", "send a", "run 60000"])
    ai = r.of("ai")
    assert len(ai) >= 16
    assert ai[0]["novelty"] is None and ai[-1]["novelty"] is not None
    assert ai[-1]["ood"] < 3
    assert "NOVEL" not in ai[-1]["flags"]


def test_teach_personalizes_and_survives_reboot(harness, tmp_path):
    flash = tmp_path / "flash.bin"
    film = skin(occlusive=0.8)
    first = run(harness, BASE + ["send 4", "run 100", "send a", "run 100", *scene(film),
                                 "run 4000", "send t", "send 2", "run 4000"], flash)
    assert first.status["model"]["personalized"] is True
    assert first.of("ai")[-1]["label"] == "normal"
    assert any(m.startswith("Learned") for m in first.messages())

    second = run(harness, ["boot", "send j", *scene(film), "send 4", "send s", "run 4000"], flash)
    assert "Restored calibration from flash" in second.stdout
    assert second.status["calibration"]["ai_baseline"] is True
    assert second.status["model"]["personalized"] is True
    assert second.of("ai")[-1]["label"] == "normal"


def test_clear_erases_saved_state(harness, tmp_path):
    flash = tmp_path / "flash.bin"
    run(harness, BASE + ["send 4", "run 100", "send a", "run 100", "send x", "run 10"], flash)
    again = run(harness, ["boot", "send j", "send s", "run 10"], flash)
    assert "Restored calibration" not in again.stdout
    assert again.status["calibration"]["ai_baseline"] is False


def test_flash_is_written_only_on_user_actions(harness, tmp_path):
    flash = tmp_path / "flash.bin"
    r = run(harness, BASE + ["send d", "run 60000", "stats"], flash)
    writes = int(r.stdout.split("flashWrites=")[1].split()[0])
    assert writes == 1  # the one capture; streaming must not wear the flash


def test_teach_cancel_and_unknown_commands(harness):
    r = run(harness, BASE + ["send t", "send z", "run 10", "send Q", "run 10"])
    msgs = r.messages()
    assert any("Teach cancelled" in m for m in msgs)
    assert any("Unknown command 'Q'" in m for m in msgs)


def test_pause_stops_and_resume_restarts_readings(harness):
    r = run(harness, BASE + ["run 3000", "send p", "run 100", "stats", "run 6000", "stats"])
    text = r.stdout
    paused_part = text.split("@@ stats")[1]
    assert '"type":"hydration"' not in paused_part.split("\n", 1)[1]
    assert r.status["streaming"] is False


@pytest.mark.parametrize("mode", ["1", "2", "3", "4"])
def test_csv_rows_match_their_header(harness, mode):
    captures = {"1": "send d", "2": "send b", "3": "send w", "4": "send a"}[mode]
    r = run(harness, ["boot", *scene(skin()), f"send {mode}", "run 100", captures, "run 100",
                      "send c", "run 12000"])
    blocks = r.csv_blocks()
    assert blocks and len(blocks[-1]) > 1
    header, *rows = blocks[-1]
    for row in rows:
        assert len(row) == len(header), (header, row)


# ------------------------------------------- regressions from code review --

def test_hydration_led_choice_and_references_survive_reboot(harness, tmp_path):
    """HIGH: references captured on 850 nm were unusable after a power cycle."""
    flash = tmp_path / "flash.bin"
    dry, moist = skin(hydration=0.05), skin(hydration=0.95)
    run(harness, ["boot", "send j", "send l", *scene(dry), "send d", "run 100",
                  *scene(moist), "send m", "run 100"], flash)
    r = run(harness, ["boot", "send j", *scene(skin()), "send s", "run 4000"], flash)
    assert r.status["hydration_led"] == "850nm"
    last = r.of("hydration")[-1]
    assert last["led"] == "850nm" and last["calibrated"] is True


def test_switching_led_warns_before_discarding_references(harness):
    r = run(harness, BASE + ["send d", "run 100", "send l", "run 10", "send m", "run 100"])
    assert any("discarded" in m.lower() for m in r.messages())


def test_white_reference_rejects_frames_without_a_target(harness):
    """MEDIUM: pressing w with nothing in front of the sensor stored garbage."""
    r = run(harness, BASE + ["send 3", "scene contact 0", "send w", "run 100", "send s", "run 10"])
    assert any("White reference failed" in m for m in r.messages())
    assert r.status["calibration"]["white"] is False


def test_pending_teach_expires_instead_of_eating_a_mode_key(harness):
    """MEDIUM: 't' then (much later) '4' taught 'occlusive' instead of switching mode."""
    r = run(harness, BASE + ["send 4", "run 100", "send a", "run 100", "send t", "run 15000",
                             "send 4", "run 100", "send s", "run 10"])
    assert r.status["model"]["personalized"] is False
    assert any("Teach timed out" in m for m in r.messages())


def test_personalization_is_dropped_when_the_model_changes(harness, tmp_path):
    """MEDIUM: a head fine-tuned on old weights was applied to a retrained model."""
    flash = tmp_path / "flash.bin"
    run(harness, BASE + ["send 4", "run 100", "send a", "run 100", "send t", "send 2", "run 4000"], flash)
    other = harness.parent / "harness_other_model"
    if not other.exists():
        cxx = shutil.which("c++") or shutil.which("clang++")
        sources = [str(HERE / "harness.cpp")] + [str(p) for p in sorted(FW.glob("*.cpp"))]
        res = subprocess.run([cxx, "-std=gnu++11", "-O1", "-DARDUINO_ARCH_SAMD",
                              "-DONCOSENSE_MODEL_ID=0xBADC0DEu", f"-I{HERE}", f"-I{FW}",
                              "-o", str(other), *sources], capture_output=True, text=True)
        assert res.returncode == 0, res.stderr
    r = run(other, ["boot", "send j", "send s", "run 10"], flash)
    assert r.status["calibration"]["ai_baseline"] is True   # calibration kept
    assert r.status["model"]["personalized"] is False        # stale head dropped


def test_saturation_drops_gain_as_far_as_needed(harness):
    """LOW: SATURATED was reported after 3 gain steps even when lower gains existed."""
    r = run(harness, ["boot", "send j", *scene(skin(scale=600.0)), "run 3000"])
    first = r.of("hydration")[0]
    assert "SATURATED" not in first["flags"]


def test_trend_is_null_before_any_valid_sample(harness):
    """LOW: an unprimed trend printed 0 instead of null."""
    r = run(harness, BASE + ["send 2", "run 4000"])
    assert r.of("scan")[-1]["trend"] is None


def test_novelty_does_not_learn_from_light_leak_frames(harness):
    """LOW: AMBIENT_LEAK / SATURATED frames were folded into the wearer's normal."""
    r = run(harness, BASE + ["send 4", "run 100", "send a", "run 100", "scene ambient 5",
                             "run 60000", "send s", "run 10"])
    assert all("AMBIENT_LEAK" in a["flags"] for a in r.of("ai")[1:])
    assert r.status["model"]["novelty_ready"] is False


def test_references_too_close_together_are_reported(harness):
    """LOW: identical dry/moist references silently produced 'Unknown' forever."""
    r = run(harness, BASE + ["send d", "run 100", "send m", "run 100"])
    assert any("too close" in m for m in r.messages())


def test_end_to_end_ai_session_to_logger_analyzer_and_training(harness, tmp_path):
    """Firmware AI CSV -> logger Session -> analyzer and train.py --real."""
    sys.path.insert(0, str(ROOT / "tools"))
    from oncosense_logger import Session

    files = []
    for label, state in (("normal", skin()), ("very_dry", skin(hydration=0.05))):
        out = run(harness, BASE + ["send 4", "run 100", "send a", "run 100", *scene(state),
                                   "send c", "run 15000"])
        session = Session(tmp_path, label, label)
        for ln in out.lines:
            session.handle(ln)
        session.close()
        files += session.paths
    env = {**os.environ, "MPLBACKEND": "Agg"}
    res = subprocess.run([sys.executable, str(ROOT / "tools" / "analyze_session.py"), *map(str, files),
                          "--out", str(tmp_path / "figs")], capture_output=True, text=True, env=env)
    assert res.returncode == 0, res.stderr
    assert "AI predictions by label" in res.stdout
    res = subprocess.run([sys.executable, str(ROOT / "ml" / "train.py"), "--quick", "--no-export",
                          "--real", *map(str, files)], capture_output=True, text=True)
    assert res.returncode == 0, res.stderr
    assert "Fine-tuning on" in res.stdout


def test_class_digit_typed_during_a_measurement_is_still_a_teach_label(harness):
    """Second review: the timeout fired before reading input that arrived while a
    blocking AI measurement ran, so a digit typed 3 s after 't' switched mode."""
    r = run(harness, BASE + ["send 4", "run 100", "send a", "run 100", "send t", "run 3000",
                             "send 2", "run 6000", "send s", "run 10"])
    assert r.status["mode"] == "ai"
    assert r.status["model"]["personalized"] is True
    assert not any("Teach timed out" in m for m in r.messages())
