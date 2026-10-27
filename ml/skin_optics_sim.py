"""Physics-informed simulator of what the OncoSense Band's AS7341 sees.

There is no labelled human dataset yet, so the bootstrap neural network is
trained on readings simulated from a simple skin-optics model. The model is
deliberately transparent so every assumption can be checked and replaced:

Tissue (per AS7341 band centre wavelength):
    mu_a  = melanin + blood + water + baseline          (absorption, 1/cm)
    mu_s' = a * (lambda / 500 nm) ** -b                  (reduced scattering, 1/cm)
    R_d   = exp(-7 * sqrt(mu_a / (3 * (mu_a + mu_s'))))  (diffuse reflectance,
            the simple approximation popularised by S. Jacques)

Hydration enters through scattering: a dry stratum corneum has air gaps that
raise the refractive-index mismatch, which increases scattering and adds a
diffuse surface term (Jiang et al. 2011). An occlusive film (petrolatum, oil)
adds a near-surface, spectrally flat return. That matches the v1 observation
that Vaseline *raised* the reading.

Sensor: each light source illuminates each band with a fixed weight (LED
spectrum x filter overlap). The reading is reflectance x illumination x
contact coupling, plus multiplicative noise and a small ambient residual.

Absorption coefficients are rounded literature values (melanin power law from
Jacques 2013; whole-blood absorption approximated at the band centres). The
model is for *bootstrapping and testing* the pipeline, not a validated skin
model. Real labelled data from tools/oncosense_logger.py replaces it.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

# AS7341 bands in firmware order (kBands): F1..F8, Clear, NIR.
BANDS = ["f415", "f445", "f480", "f515", "f555", "f590", "f630", "f680", "clear", "nir"]
CENTRES_NM = np.array([415, 445, 480, 515, 555, 590, 630, 680, 0, 910], dtype=float)
SPECTRAL = slice(0, 8)  # F1..F8
CLEAR, NIR = 8, 9

# Whole-blood absorption (1/cm, ~80 % oxygenated) at the band centres.
MU_A_BLOOD = np.array([2700, 700, 150, 120, 230, 120, 6, 4, 0, 5], dtype=float)
# Water absorption (1/cm). Tiny below 950 nm; included for completeness.
MU_A_WATER = np.array([0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.003, 0.005, 0, 0.07])

# How strongly each light source illuminates each band (LED spectrum x filter).
ILLUMINATION = {
    "nir850": np.array([0, 0, 0, 0, 0, 0, 0, 0.01, 0.6, 1.0]),
    "red660": np.array([0, 0, 0, 0, 0, 0.02, 0.25, 0.6, 0.7, 0.02]),
    "white":  np.array([0.15, 0.9, 0.5, 0.55, 0.8, 0.85, 0.7, 0.45, 1.0, 0.03]),
}
SOURCES = list(ILLUMINATION)

CLASSES = ["very_dry", "dry", "normal", "well_hydrated", "occlusive"]


@dataclass
class Person:
    melanin: float  # epidermal melanin volume fraction (0.01 light .. 0.35 dark)
    blood: float    # dermal blood volume fraction
    scatter_a: float
    scatter_b: float


@dataclass
class SkinState:
    hydration: float     # 0 = very dry .. 1 = very hydrated
    occlusive: float     # 0 = bare skin .. 1 = thick petrolatum film
    erythema: float      # extra blood from irritation / redness
    coupling: float      # contact pressure / placement factor (~1)


def random_person(rng: np.random.Generator) -> Person:
    return Person(
        melanin=float(np.exp(rng.uniform(np.log(0.01), np.log(0.35)))),
        blood=float(rng.uniform(0.005, 0.03)),
        scatter_a=float(rng.uniform(30, 50)),
        scatter_b=float(rng.uniform(1.1, 1.6)),
    )


def reflectance(p: Person, s: SkinState) -> np.ndarray:
    """Diffuse + surface reflectance per band (Clear is filled in later)."""
    lam = np.where(CENTRES_NM > 0, CENTRES_NM, 600.0)
    mu_mel = p.melanin * 6.6e11 * lam ** -3.33 * 0.1  # thin epidermal layer
    mu_a = mu_mel + (p.blood + s.erythema) * MU_A_BLOOD + 0.7 * MU_A_WATER + 0.2

    dryness = 1.0 - s.hydration
    a = p.scatter_a * (1.0 + 0.35 * dryness)          # dry SC scatters more
    mu_s = a * (lam / 500.0) ** -p.scatter_b
    mu_s *= 1.0 - 0.10 * s.occlusive                  # index matching under film

    r = np.exp(-7.0 * np.sqrt(mu_a / (3.0 * (mu_a + mu_s))))
    r += 0.015 + 0.05 * dryness                        # rough, flaky surface
    r += 0.07 * s.occlusive                            # film reflection
    r[CLEAR] = np.mean(r[SPECTRAL])
    return r


def measure(p: Person, s: SkinState, source: str, rng: np.random.Generator,
            noise: float = 0.02) -> np.ndarray:
    """Ambient-corrected basic counts for one source, all 10 bands."""
    signal = reflectance(p, s) * ILLUMINATION[source] * s.coupling
    signal *= 1.0 + noise * rng.standard_normal(signal.shape)
    signal += 1e-4 * np.abs(rng.standard_normal(signal.shape))  # ambient residual
    return np.maximum(signal, 1e-6)


def scan(p: Person, s: SkinState, rng: np.random.Generator) -> dict[str, np.ndarray]:
    return {src: measure(p, s, src, rng) for src in SOURCES}


def label_of(s: SkinState) -> int:
    if s.occlusive >= 0.5:
        return CLASSES.index("occlusive")
    if s.hydration < 0.2:
        return CLASSES.index("very_dry")
    if s.hydration < 0.4:
        return CLASSES.index("dry")
    if s.hydration < 0.7:
        return CLASSES.index("normal")
    return CLASSES.index("well_hydrated")


def random_state(rng: np.random.Generator) -> SkinState:
    occlusive = float(rng.uniform(0.5, 1.0)) if rng.random() < 0.2 else float(rng.uniform(0, 0.15))
    return SkinState(
        hydration=float(rng.uniform(0, 1)),
        occlusive=occlusive,
        erythema=float(rng.uniform(0, 0.01)) if rng.random() < 0.3 else 0.0,
        coupling=float(rng.uniform(0.85, 1.15)),
    )


BASELINE_STATE = SkinState(hydration=0.55, occlusive=0.0, erythema=0.0, coupling=1.0)


def baseline_scan(p: Person, rng: np.random.Generator, n: int = 8) -> dict[str, np.ndarray]:
    """What the firmware stores when the wearer presses `b` in AI mode."""
    scans = [scan(p, BASELINE_STATE, rng) for _ in range(n)]
    return {src: np.mean([sc[src] for sc in scans], axis=0) for src in SOURCES}
