"""Feature extraction shared by training, the app demo and the firmware.

This must stay identical to firmware/OncoSense/Features.cpp. The unit tests
check the two against each other through exported test vectors.

Inputs are 10-band arrays in firmware band order (F1..F8, Clear, NIR) of
ambient-corrected basic counts, one per light source, plus the wearer's
baseline scan for each source.
"""

from __future__ import annotations

import numpy as np

FEATURE_NAMES = [
    "d_f415", "d_f445", "d_f480", "d_f515", "d_f555", "d_f590", "d_f630", "d_f680",
    "d_red680", "d_nir910", "redness_shape", "pigment_slope",
]
N_FEATURES = len(FEATURE_NAMES)
FLOOR = 1e-6

F3, F5, F7, F8, NIR = 2, 4, 6, 7, 9


def extract(scan: dict[str, np.ndarray], base: dict[str, np.ndarray]) -> np.ndarray:
    w = np.maximum(scan["white"], FLOOR)
    wb = np.maximum(base["white"], FLOOR)
    red = max(scan["red660"][F8], FLOOR)
    red_b = max(base["red660"][F8], FLOOR)
    nir = max(scan["nir850"][NIR], FLOOR)
    nir_b = max(base["nir850"][NIR], FLOOR)
    return np.array([
        *np.log(w[:8] / wb[:8]),  # change in the white-light spectrum vs baseline
        np.log(red / red_b),      # change at 680 nm under the 660 nm LED
        np.log(nir / nir_b),      # change at NIR under the 850 nm LED
        np.log(w[F7] / w[F5]),    # absolute: red vs green (redness / blood)
        np.log(w[F8] / w[F3]),    # absolute: red vs blue (pigment slope)
    ], dtype=np.float64)
