#!/usr/bin/env python3
"""Train the OncoSense skin-state network and export it to the firmware.

    python ml/train.py                        # bootstrap on simulated data
    python ml/train.py --real data/ai_*.csv   # fine-tune on real labelled scans
    python ml/train.py --quick                # fast run used by CI

Network: 12 features -> 16 ReLU -> 12 ReLU -> 5 softmax (~480 parameters,
about 2 KB of flash). It is small enough to run on a SAMD21 in well under a
millisecond and to fine-tune its last layer on the band itself.

Outputs
  firmware/OncoSense/model_weights.h   C++ weights + standardisation
  ml/model.json, app/model.json        same weights for the web app's demo mode
  tests/nn_test_vectors.h              inputs and expected outputs so the C++
                                       inference is checked against numpy

Only numpy is required. Real data comes from tools/oncosense_logger.py in AI
mode with JSON output converted by --real (see ml/README.md).
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import skin_optics_sim as sim  # noqa: E402
from features import FEATURE_NAMES, N_FEATURES, extract  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[1]
LAYERS = [N_FEATURES, 16, 12, len(sim.CLASSES)]


# ------------------------------------------------------------------ data --

def simulate(n_people: int, per_person: int, seed: int):
    rng = np.random.default_rng(seed)
    X, y, groups = [], [], []
    for pid in range(n_people):
        person = sim.random_person(rng)
        base = sim.baseline_scan(person, rng)
        for _ in range(per_person):
            state = sim.random_state(rng)
            X.append(extract(sim.scan(person, state, rng), base))
            y.append(sim.label_of(state))
            groups.append(pid)
    return np.array(X), np.array(y), np.array(groups)


def load_real(paths: list[str]):
    """CSV rows with columns: label, then the 12 feature columns (see ml/README.md)."""
    X, y = [], []
    for p in paths:
        with open(p, encoding="utf-8") as f:
            header = f.readline().strip().split(",")
            idx = [header.index(n) for n in FEATURE_NAMES]
            li = header.index("label")
            for line in f:
                cols = line.strip().split(",")
                if len(cols) != len(header) or cols[li] not in sim.CLASSES:
                    continue
                X.append([float(cols[i]) for i in idx])
                y.append(sim.CLASSES.index(cols[li]))
    return np.array(X), np.array(y)


# ----------------------------------------------------------------- model --

def init(rng):
    params = []
    for fan_in, fan_out in zip(LAYERS[:-1], LAYERS[1:]):
        W = rng.standard_normal((fan_out, fan_in)) * np.sqrt(2.0 / fan_in)
        params += [W, np.zeros(fan_out)]
    return params


def forward(params, X):
    acts = [X]
    h = X
    for i in range(0, len(params), 2):
        z = h @ params[i].T + params[i + 1]
        h = np.maximum(z, 0) if i < len(params) - 2 else z
        acts.append(h)
    logits = acts[-1]
    e = np.exp(logits - logits.max(axis=1, keepdims=True))
    return acts, e / e.sum(axis=1, keepdims=True)


def train(X, y, params, epochs, lr=3e-3, l2=1e-4, batch=128, seed=0):
    rng = np.random.default_rng(seed)
    m = [np.zeros_like(p) for p in params]
    v = [np.zeros_like(p) for p in params]
    t = 0
    for epoch in range(epochs):
        order = rng.permutation(len(X))
        for s in range(0, len(X), batch):
            idx = order[s:s + batch]
            acts, probs = forward(params, X[idx])
            grad = probs.copy()
            grad[np.arange(len(idx)), y[idx]] -= 1
            grad /= len(idx)
            grads = [None] * len(params)
            for li in range(len(params) // 2 - 1, -1, -1):
                W = params[2 * li]
                grads[2 * li] = grad.T @ acts[li] + l2 * W
                grads[2 * li + 1] = grad.sum(axis=0)
                if li > 0:
                    grad = (grad @ W) * (acts[li] > 0)
            t += 1
            for k, g in enumerate(grads):
                m[k] = 0.9 * m[k] + 0.1 * g
                v[k] = 0.999 * v[k] + 0.001 * g * g
                mh, vh = m[k] / (1 - 0.9 ** t), v[k] / (1 - 0.999 ** t)
                params[k] -= lr * mh / (np.sqrt(vh) + 1e-8)
    return params


def evaluate(params, X, y, name):
    _, probs = forward(params, X)
    pred = probs.argmax(axis=1)
    acc = (pred == y).mean()
    k = len(sim.CLASSES)
    cm = np.zeros((k, k), dtype=int)
    for t_, p_ in zip(y, pred):
        cm[t_, p_] += 1
    print(f"\n{name}: accuracy {acc:.3f} on {len(y)} samples")
    width = max(len(c) for c in sim.CLASSES)
    print(" " * (width + 2) + " ".join(f"{c[:6]:>6}" for c in sim.CLASSES) + "   <- predicted")
    for i, c in enumerate(sim.CLASSES):
        print(f"{c:>{width}}  " + " ".join(f"{n:6d}" for n in cm[i]))
    return acc


# ---------------------------------------------------------------- export --

def c_array(name, arr):
    flat = ", ".join(f"{x:.8g}f" for x in np.asarray(arr, dtype=np.float64).ravel())
    return f"static const float {name}[{np.size(arr)}] = {{{flat}}};\n"


def export(params, mean, std, meta, X_test):
    W1, b1, W2, b2, W3, b3 = params
    header = [
        "// Generated by ml/train.py. Do not edit by hand.\n",
        f"// Training data: {meta['data']}\n",
        f"// Held-out accuracy (people not seen in training): {meta['accuracy']:.3f}\n",
        "#pragma once\n\n",
        "namespace oncosense {\nnamespace model {\n\n",
        f"static const int kInputs = {LAYERS[0]};\n",
        f"static const int kHidden1 = {LAYERS[1]};\n",
        f"static const int kHidden2 = {LAYERS[2]};\n",
        f"static const int kClasses = {LAYERS[3]};\n\n",
        "static const char *const kClassNames[kClasses] = {"
        + ", ".join(f'"{c}"' for c in sim.CLASSES) + "};\n\n",
        "// Standardisation: x' = (x - kMean) / kStd\n",
        c_array("kMean", mean), c_array("kStd", std), "\n",
        "// Weights are row-major [out][in].\n",
        c_array("kW1", W1), c_array("kB1", b1),
        c_array("kW2", W2), c_array("kB2", b2),
        c_array("kW3", W3), c_array("kB3", b3),
        "\n}  // namespace model\n}  // namespace oncosense\n",
    ]
    (ROOT / "firmware/OncoSense/model_weights.h").write_text("".join(header))

    model = {
        "classes": sim.CLASSES, "features": FEATURE_NAMES, "layers": LAYERS,
        "mean": mean.tolist(), "std": std.tolist(),
        "weights": [p.tolist() for p in params], "meta": meta,
    }
    for path in ("ml/model.json", "app/model.json"):
        (ROOT / path).write_text(json.dumps(model, separators=(",", ":")))

    # Test vectors: raw features -> expected probabilities.
    raw = X_test[:6]
    _, probs = forward(params, (raw - mean) / std)
    lines = ["// Generated by ml/train.py: C++ inference must match numpy.\n#pragma once\n\n",
             f"static const int kNumVectors = {len(raw)};\n",
             c_array("kVectorInputs", raw), c_array("kVectorProbs", probs)]

    # Feature-extraction vectors: (scan, baseline) -> features, so the C++
    # Features.cpp is checked against ml/features.py.
    rng = np.random.default_rng(123)
    scans, bases, feats = [], [], []
    for _ in range(4):
        person = sim.random_person(rng)
        base = sim.baseline_scan(person, rng, n=2)
        sc = sim.scan(person, sim.random_state(rng), rng)
        scans.append([sc[k] for k in ("white", "red660", "nir850")])
        bases.append([base[k] for k in ("white", "red660", "nir850")])
        feats.append(extract(sc, base))
    lines += ["\n// Feature vectors: [white(10), red(10), nir(10)] per scan.\n",
              "static const int kNumFeatureVectors = 4;\n",
              c_array("kFeatScans", np.array(scans)), c_array("kFeatBases", np.array(bases)),
              c_array("kFeatExpected", np.array(feats))]
    (ROOT / "tests/nn_test_vectors.h").write_text("".join(lines))


# ------------------------------------------------------------------ main --

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--people", type=int, default=400)
    ap.add_argument("--per-person", type=int, default=40)
    ap.add_argument("--epochs", type=int, default=60)
    ap.add_argument("--real", nargs="*", default=[], help="labelled real-feature CSVs to fine-tune on")
    ap.add_argument("--quick", action="store_true", help="small, fast run (CI)")
    ap.add_argument("--no-export", action="store_true")
    ap.add_argument("--seed", type=int, default=7)
    args = ap.parse_args()
    if args.quick:
        args.people, args.per_person, args.epochs = 60, 20, 8

    X, y, groups = simulate(args.people, args.per_person, args.seed)
    # Hold out whole people so the score reflects unseen skin, not memorisation.
    test_people = groups >= int(args.people * 0.8)
    mean, std = X[~test_people].mean(axis=0), X[~test_people].std(axis=0) + 1e-6
    Xs = (X - mean) / std

    params = init(np.random.default_rng(args.seed))
    params = train(Xs[~test_people], y[~test_people], params, args.epochs, seed=args.seed)
    acc = evaluate(params, Xs[test_people], y[test_people], "Simulated, held-out people")
    data = f"simulated ({args.people} virtual people x {args.per_person} states)"

    if args.real:
        Xr, yr = load_real(args.real)
        if len(Xr):
            print(f"\nFine-tuning on {len(Xr)} real labelled scans")
            params = train((Xr - mean) / std, yr, params, max(20, args.epochs // 2), lr=1e-3)
            evaluate(params, (Xr - mean) / std, yr, "Real data (training fit, not a held-out score)")
            data += f" + {len(Xr)} real scans"

    if not args.no_export:
        export(params, mean, std, {"data": data, "accuracy": round(float(acc), 4)}, X[test_people])
        print("\nExported firmware/OncoSense/model_weights.h, ml/model.json, tests/nn_test_vectors.h")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
