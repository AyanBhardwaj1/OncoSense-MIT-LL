# 6 · Calibration and usage

## Quick start

1. Flash [`firmware/OncoSense`](../firmware/OncoSense) ([build steps](../firmware/README.md#build-and-flash)).
2. Put the band on your inner forearm with the optical head flat against clean skin. Make it snug but comfortable.
3. Open a serial monitor at **115200 baud**. You will see the banner and the command list.
4. Watch the flags. You want `OK`, not `NO_CONTACT` or `AMBIENT_LEAK`.

## Mode 1: Hydration

| Step | Do | Press |
|---|---|---|
| 1 | Select hydration mode (the default) | `1` |
| 2 | *Optional:* switch the LED (660 nm ↔ 850 nm) | `l` |
| 3 | Wipe the site with isopropyl alcohol and wait 2 min | `d` (dry reference) |
| 4 | Apply moisturizer, wait 10 min, blot the excess | `m` (moisturized reference) |
| 5 | Wear normally. Readings stream every second | — |

Until both references exist, the firmware uses the **v1 MainSkin thresholds** (only valid with the 660 nm LED) and marks readings `uncalibrated`.

## Mode 2: Surface scan

| Step | Do | Press |
|---|---|---|
| 1 | Select surface-scan mode | `2` |
| 2 | Place the band on **clean, bare, healthy** skin | `b` (baseline) |
| 3 | Move to the area you want to compare (a moisturized patch, a scar, a test product) | — |

The output shows the percentage change from your baseline and its category.

## Mode 3: Spectral (pigment and redness)

| Step | Do | Press |
|---|---|---|
| 1 | Select spectral mode | `3` |
| 2 | Hold a plain white card (printer paper works) flat over the sensor | `w` |
| 3 | Put the band back on the skin | — |

The output is 10-band reflectance plus a **melanin index (MI)** and an **erythema index (EI)**.

## Logging data

```bash
pip install -r tools/requirements.txt
```

```bash
python tools/oncosense_logger.py --list
```

```bash
python tools/oncosense_logger.py --port /dev/cu.usbmodem1101 --send 1c --label moisturized --seconds 60
```

```bash
python tools/analyze_session.py data/*.csv
```

`--send 1c` selects hydration mode and switches on CSV output when the logger connects. Each run writes a labelled CSV in `data/`. The analyzer prints per-label statistics and saves plots to `figures/`.

## Tuning

Every tunable is in [`config.h`](../firmware/OncoSense/config.h):

| Setting | Default | Change it when |
|---|---|---|
| `PAIRS_PER_MEASUREMENT` | 4 | Readings are noisy (raise it) or you want faster updates (lower it) |
| `AS_ATIME`, `AS_ASTEP` | 29, 599 (50 ms) | You need a longer integration for very weak signals |
| `MIN_DELTA_RAW_COUNTS` | 40 | `NO_CONTACT` fires while the band is clearly on (lower it) |
| `MAX_AMBIENT_FRACTION` | 0.5 | You are testing in bright light on purpose |
| `ONBOARD_LED_MA` | 10 | Spectral mode is saturated (lower it) or too dim (raise it) |
| `SENSOR_WIRE` / `USE_CUSTOM_I2C_PINS` | `Wire` / 0 | You are using a different QT Py variant or an ESP32 board |

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| `ERROR: AS7341 not found`, NIR LED blinking | STEMMA QT cable unplugged or reversed, or the wrong `SENSOR_WIRE` for your board |
| Always `NO_CONTACT` | LED not wired, resistor value too high, or the band is loose |
| `AMBIENT_LEAK` | The optical head isn't covered. Tighten the band or move it away from bright light |
| `SATURATED` | The LED is too close or too bright. Use a larger resistor or a lower `ONBOARD_LED_MA` |
| `Calibration failed` | Most frames during capture had no contact. Re-seat the band and hold still |
| Score jumps around | Raise `PAIRS_PER_MEASUREMENT`, hold the arm still, and use the `trend` column |
