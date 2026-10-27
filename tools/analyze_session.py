#!/usr/bin/env python3
"""Summarize and plot OncoSense Band CSV sessions.

    python analyze_session.py data/*.csv               # summary table + plots
    python analyze_session.py data/*.csv --out figs    # choose plot directory

For hydration / surface-scan files it plots signal and score over time and
prints per-label statistics (mean, standard deviation, coefficient of
variation), which is how repeatability and dry-vs-moisturized separation are
judged. For spectral files it plots the mean 10-band spectrum per label.
"""

from __future__ import annotations

import argparse
import pathlib
import sys

try:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import pandas as pd
except ImportError:  # pragma: no cover
    sys.exit("pandas and matplotlib are required:  pip install -r tools/requirements.txt")

BANDS = ["f415", "f445", "f480", "f515", "f555", "f590", "f630", "f680", "clear", "nir"]
WAVELENGTHS = {"f415": 415, "f445": 445, "f480": 480, "f515": 515, "f555": 555,
               "f590": 590, "f630": 630, "f680": 680, "nir": 910}


def load(paths: list[str]) -> pd.DataFrame:
    frames = []
    for p in paths:
        df = pd.read_csv(p, comment="#")
        if "label" not in df.columns:
            df["label"] = pathlib.Path(p).stem
        df["file"] = pathlib.Path(p).name
        frames.append(df)
    if not frames:
        sys.exit("No input files.")
    return pd.concat(frames, ignore_index=True)


def good_rows(df: pd.DataFrame) -> pd.DataFrame:
    if "flags" not in df.columns:
        return df
    return df[df["flags"].astype(str).str.fullmatch("OK")]


def summarize_scalar(df: pd.DataFrame, out: pathlib.Path) -> None:
    ok = good_rows(df)
    dropped = len(df) - len(ok)
    stats = ok.groupby("label")["signal"].agg(["count", "mean", "std"])
    stats["cv_%"] = 100 * stats["std"] / stats["mean"]
    print("\nSignal by label (rows flagged NO_CONTACT / AMBIENT_LEAK / SATURATED excluded):")
    print(stats.round(5).to_string())
    if dropped:
        print(f"({dropped} flagged rows excluded)")

    labels = list(stats.index)
    if len(labels) >= 2:
        a, b = labels[0], labels[1]
        sa, sb = ok[ok.label == a]["signal"], ok[ok.label == b]["signal"]
        pooled = ((sa.var() + sb.var()) / 2) ** 0.5
        if pooled > 0:
            print(f"\nSeparation {a} vs {b}: Cohen's d = {(sb.mean() - sa.mean()) / pooled:.2f}")

    fig, axes = plt.subplots(2, 1, figsize=(9, 6), sharex=True)
    for label, g in df.groupby("label"):
        t = (g["t_ms"] - g["t_ms"].iloc[0]) / 1000
        axes[0].plot(t, g["signal"], ".-", label=label, ms=3)
        axes[1].plot(t, g["metric"], ".-", label=label, ms=3)
    axes[0].set_ylabel("signal (basic counts)")
    axes[1].set_ylabel("score / ratio")
    axes[1].set_xlabel("time (s)")
    axes[0].legend()
    for ax in axes:
        ax.grid(alpha=0.3)
    fig.suptitle("OncoSense session")
    fig.tight_layout()
    path = out / "session_timeseries.png"
    fig.savefig(path, dpi=150)
    print(f"\nSaved {path}")


def summarize_spectral(df: pd.DataFrame, out: pathlib.Path) -> None:
    ok = good_rows(df)
    bands = [b for b in BANDS if b in ok.columns and b != "clear"]
    means = ok.groupby("label")[bands].mean()
    print("\nMean reflectance by label:")
    print(means.round(4).to_string())
    if {"melanin_idx", "erythema_idx"} <= set(ok.columns):
        print("\nPigment indices:")
        print(ok.groupby("label")[["melanin_idx", "erythema_idx"]].agg(["mean", "std"]).round(2).to_string())

    fig, ax = plt.subplots(figsize=(8, 4.5))
    x = [WAVELENGTHS[b] for b in bands]
    for label, row in means.iterrows():
        ax.plot(x, row.values, "o-", label=label)
    ax.set_xlabel("band centre (nm)")
    ax.set_ylabel("reflectance (vs white card)")
    ax.grid(alpha=0.3)
    ax.legend()
    fig.tight_layout()
    path = out / "session_spectrum.png"
    fig.savefig(path, dpi=150)
    print(f"\nSaved {path}")


def summarize_ai(df: pd.DataFrame, out: pathlib.Path) -> None:
    print("\nAI predictions by label (rows with NO_CONTACT / AMBIENT_LEAK / SATURATED excluded):")
    clean = df[~df["flags"].astype(str).str.contains("NO_CONTACT|AMBIENT_LEAK|SATURATED")]
    table = pd.crosstab(clean["label"], clean["prediction"])
    print(table.to_string())
    matched = clean[clean["label"].isin(clean["prediction"].unique())]
    if len(matched):
        acc = (matched["label"] == matched["prediction"]).mean()
        print(f"\nAgreement where the label is a model class: {acc:.1%} of {len(matched)} rows")
    print("\nMean novelty / out-of-distribution by label:")
    print(clean.groupby("label")[["novelty", "ood", "confidence"]].mean().round(3).to_string())

    fig, ax = plt.subplots(figsize=(9, 3.5))
    for label, g in df.groupby("label"):
        t = (g["t_ms"] - g["t_ms"].iloc[0]) / 1000
        ax.plot(t, g["confidence"], ".-", label=label, ms=3)
    ax.set_xlabel("time (s)")
    ax.set_ylabel("model confidence")
    ax.set_ylim(0, 1)
    ax.grid(alpha=0.3)
    ax.legend()
    fig.tight_layout()
    path = out / "session_ai.png"
    fig.savefig(path, dpi=150)
    print(f"\nSaved {path}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+")
    ap.add_argument("--out", default="figures")
    args = ap.parse_args()

    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    df = load(args.files)
    modes = set(df.get("mode", pd.Series(dtype=str)).dropna().unique())

    if "ai" in modes:
        summarize_ai(df[df["mode"] == "ai"], out)
    if "spectral" in modes:
        summarize_spectral(df[df["mode"] == "spectral"], out)
    scalar = df[df["mode"].isin(["hydration", "scan"])] if "mode" in df else df
    if len(scalar):
        summarize_scalar(scalar, out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
