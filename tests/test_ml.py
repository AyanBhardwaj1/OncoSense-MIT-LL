"""Tests for the training pipeline in ml/.

    python -m pytest tests/test_ml.py -q
"""

from __future__ import annotations

import json
import math
import pathlib
import shutil
import subprocess
import sys

import numpy as np
import pytest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "ml"))

import skin_optics_sim as sim  # noqa: E402
import train  # noqa: E402
from features import FEATURE_NAMES, extract  # noqa: E402

AI_HEADER = ("t_ms,mode,prediction,confidence," + ",".join(f"p_{c}" for c in sim.CLASSES)
             + ",novelty,ood," + ",".join(FEATURE_NAMES) + ",flags")


def ai_row(label: str, feats: list[float], flags="OK", prediction="normal") -> str:
    probs = ",".join("0.2" for _ in sim.CLASSES)
    return f"1000,ai,{prediction},0.2,{probs},nan,0.5," + ",".join(map(str, feats)) + f",{flags},{label}"


# ------------------------------------------------------------- simulator --

def test_simulated_reflectance_is_physical():
    rng = np.random.default_rng(0)
    for _ in range(50):
        r = sim.reflectance(sim.random_person(rng), sim.random_state(rng))
        assert np.all((r > 0) & (r < 1))


def test_drier_skin_reflects_more_and_film_raises_return():
    p = sim.Person(0.06, 0.015, 40, 1.35)
    dry = sim.reflectance(p, sim.SkinState(0.1, 0, 0, 1))
    wet = sim.reflectance(p, sim.SkinState(0.9, 0, 0, 1))
    film = sim.reflectance(p, sim.SkinState(0.5, 1.0, 0, 1))
    mid = sim.reflectance(p, sim.SkinState(0.5, 0, 0, 1))
    assert np.all(dry[:8] > wet[:8])          # Jiang et al.: dehydration brightens DRS
    assert np.all(film[:8] > mid[:8])         # v1 observation: petrolatum raised the reading


@pytest.mark.parametrize("h,occ,label", [(0.1, 0, "very_dry"), (0.3, 0, "dry"), (0.5, 0, "normal"),
                                         (0.9, 0, "well_hydrated"), (0.5, 0.7, "occlusive")])
def test_labels(h, occ, label):
    assert sim.CLASSES[sim.label_of(sim.SkinState(h, occ, 0, 1))] == label


def test_features_are_zero_against_own_baseline():
    rng = np.random.default_rng(1)
    s = sim.scan(sim.random_person(rng), sim.BASELINE_STATE, rng)
    assert np.allclose(extract(s, s)[:10], 0)


# --------------------------------------------------------------- training --

def test_backprop_matches_numerical_gradient():
    rng = np.random.default_rng(3)
    X = rng.standard_normal((8, train.LAYERS[0]))
    y = rng.integers(0, train.LAYERS[-1], 8)
    params = train.init(rng)

    def loss(ps):
        _, probs = train.forward(ps, X)
        return -np.mean(np.log(probs[np.arange(len(y)), y]))

    grads = train.gradients(params, X, y, l2=0.0)
    for k in (0, 1, 4, 5):
        p = params[k]
        idx = tuple(rng.integers(0, s) for s in p.shape)
        old = p[idx]
        p[idx] = old + 1e-5
        up = loss(params)
        p[idx] = old - 1e-5
        down = loss(params)
        p[idx] = old
        assert grads[k][idx] == pytest.approx((up - down) / 2e-5, rel=1e-4, abs=1e-7)


def test_training_learns_the_simulator():
    X, y, groups = train.simulate(60, 20, seed=5)
    mean, std = X.mean(0), X.std(0) + 1e-6
    params = train.train((X - mean) / std, y, train.init(np.random.default_rng(5)), epochs=40, seed=5)
    _, probs = train.forward(params, (X - mean) / std)
    assert (probs.argmax(1) == y).mean() > 0.6


# ----------------------------------------------------------------- export --

def test_c_array_literals_always_compile(tmp_path):
    values = [0.0, 1.0, -2.0, 1 - 1e-12, 1e-30, -3.4e38, 0.1]
    src = tmp_path / "a.cpp"
    src.write_text(train.c_array("kA", values) + "int main() { return kA[0] == 0.0f ? 0 : 1; }\n")
    cxx = shutil.which("c++") or shutil.which("clang++")
    res = subprocess.run([cxx, "-std=c++11", "-Werror", "-o", str(tmp_path / "a"), str(src)],
                         capture_output=True, text=True)
    assert res.returncode == 0, res.stderr


@pytest.mark.parametrize("bad", [math.nan, math.inf, -math.inf])
def test_c_array_rejects_non_finite_values(bad):
    with pytest.raises(ValueError):
        train.c_array("kA", [0.5, bad])


def test_export_round_trip_and_model_id(tmp_path):
    for sub in ("firmware/OncoSense", "ml", "tests"):
        (tmp_path / sub).mkdir(parents=True)
    X, y, _ = train.simulate(10, 5, seed=2)
    params = train.init(np.random.default_rng(2))
    mean, std = X.mean(0), X.std(0) + 1e-6
    train.export(params, mean, std, {"data": "unit test", "accuracy": 0.5}, X, root=tmp_path)
    header = (tmp_path / "firmware/OncoSense/model_weights.h").read_text()
    model = json.loads((tmp_path / "ml/model.json").read_text())
    assert f"#define ONCOSENSE_MODEL_ID 0x{model['model_id']:08X}u" in header
    assert np.allclose(model["weights"][0], params[0])
    params[0][0, 0] += 0.5
    assert train.model_id(params, mean, std) != model["model_id"]


# --------------------------------------------------------------- --real --

def write(path: pathlib.Path, rows: list[str], header=AI_HEADER + ",label") -> str:
    path.write_text("\n".join([header, *rows]) + "\n")
    return str(path)


def test_load_real_skips_flagged_no_contact_and_non_finite_rows(tmp_path):
    good = [0.1] * 12
    f = write(tmp_path / "ai.csv", [
        ai_row("dry", good),
        ai_row("dry", [9.9] * 12, flags="NO_CONTACT|OOD"),
        ai_row("dry", good, flags="AMBIENT_LEAK"),
        ai_row("dry", good, flags="SATURATED"),
        ai_row("dry", good, prediction="no_contact"),
        ai_row("dry", [math.nan] + [0.1] * 11),
        ai_row("dry", good, flags="NOVEL"),          # novel is fine: it's still a real reading
        ai_row("moisturized", good),                  # not a class name
    ])
    X, y, report = train.load_real([f])
    assert len(X) == 2 and set(y) == {sim.CLASSES.index("dry")}
    assert report["flagged"] == 4 and report["non_finite"] == 1 and report["unknown_label"] == 1


def test_load_real_skips_non_ai_files_and_requires_labels(tmp_path):
    hyd = write(tmp_path / "hyd.csv", ["0,hydration,660nm,64,0.2,0,0,0,Dry,OK,dry"],
                header="t_ms,mode,led,gain,signal,ambient,metric,trend,state,flags,label")
    nolabel = write(tmp_path / "nolabel.csv", [ai_row("x", [0.1] * 12).rsplit(",", 1)[0]], header=AI_HEADER)
    X, y, report = train.load_real([hyd, nolabel])
    assert len(X) == 0
    assert report["skipped_files"] == 2


def test_train_with_real_but_no_usable_rows_fails_loudly(tmp_path):
    f = write(tmp_path / "ai.csv", [ai_row("moisturized", [0.1] * 12)])
    res = subprocess.run([sys.executable, str(ROOT / "ml" / "train.py"), "--quick", "--no-export",
                          "--real", f], capture_output=True, text=True)
    assert res.returncode != 0
    assert "no usable" in (res.stdout + res.stderr).lower()
