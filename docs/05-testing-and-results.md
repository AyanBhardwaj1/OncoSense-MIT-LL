# 5 · Testing and results

## 5.1 What we tested (Summer 2025)

| Experiment | Firmware | Setup | Outcome |
|---|---|---|---|
| **Skin hydration classification** | `MainSkin.ino` (v1) | 660 nm LED, AS7341 F8 (680 nm) channel, fixed thresholds | Picked up hydration differences (dry, normal, Vaseline-coated skin) in most trials. Thresholds needed re-tuning between sessions. **This is the sketch used in the demo video.** |
| **Surface anomalies** | `AdaSpecCode.ino` (v1) | 850 nm LED, AS7341 NIR channel, ±% thresholds against a startup baseline | Built to test lesions, bumps, scars and moisture. Developed, but **not well tested** |
| **Wearability** | — | Neoprene band with Velcro, components fixed to the fabric | Clean matte finish, comfortable, held the optical head in place during wear |

## 5.2 Results as presented

| | |
|---|---|
| **Fabric / band** | Neoprene band with Velcro-mounted components. Clean matte finish, comfortable to wear. |
| **≈ 65 % success** | Calibration was not fully successful, but most experiments did pick up skin hydration. |
| **Proof of concept** | Linear and nonlinear optics are real, useful phenomena for skin sensing. The concept works on a wearable. |
| **Next** | More calibration and more components are needed to build a device aimed at cancer detection. A melanin experiment can be added. |

## 5.3 Why calibration was hard: root causes

Looking back at the v1 code and the test sessions, these issues explain most of the inconsistency:

1. **Room light got into the readings.** v1 read with the LED on only. Any light leaking under the band added straight into the signal.
2. **Silent saturation.** v1's sensor settings allowed 101 000 counts, but the register tops out at 65 535, so bright readings clipped without warning.
3. **One sample per reading.** Motion and pressure changes went straight into the classification.
4. **Fixed absolute thresholds.** Thresholds of 150 / 350 / 700 counts were tuned on one person in one session. Skin tone, placement and band tightness all shift the absolute level.
5. **No contact check.** A loose band and dry skin could produce the same low reading.
6. **Unchecked baseline.** The surface-scan baseline was captured at boot, whether or not the band was on clean skin.

Firmware v2 addresses each of these directly (see [firmware/README.md](../firmware/README.md#what-changed-from-v1)).

## 5.4 Validation protocol (v2)

A repeatable protocol for collecting data that supports or refutes the hydration claim:

1. **Acclimate** for 15 min at room temperature. Same arm, same band position (mark the skin), same tightness.
2. **Dry reference.** Wipe the site with isopropyl alcohol, wait 2 min, then press `d`.
3. **Moisturized reference.** Apply a fixed amount of moisturizer, wait 10 min, blot the excess, then press `m`.
4. **Record time courses** with the logger, one labelled file per condition:

```bash
python tools/oncosense_logger.py --port /dev/cu.usbmodem1101 --send 1c --label baseline --seconds 120
```

5. **Analyze.**

```bash
python tools/analyze_session.py data/*.csv
```

The analyzer reports, per condition, the mean, standard deviation, **coefficient of variation** (repeatability) and **Cohen's d** between conditions (separation).

6. **Gold-standard comparison.** Where possible, take a capacitance corneometer reading at the same site and time. The literature reports r ≈ 0.93 between optical and capacitance hydration (Chen et al., 2023). That is the bar to aim for.
7. **Repeat across people and skin tones.** Use the spectral-mode melanin index to report results by pigmentation level.

| Metric | Target |
|---|---|
| Repeatability (CV on unchanged skin) | < 5 % |
| Dry vs. moisturized separation | Cohen's d > 2 |
| Agreement with corneometer | r > 0.8 |
| Flagged frames during normal wear | < 10 % |

## 5.5 Limitations

* **Not a diagnostic device.** The rash, acne, scar and lesion labels are experimental categories of NIR return. They are not medical diagnoses.
* **Small sample.** Summer testing used a handful of people and sessions.
* **Single source–detector distance.** The device can't separate surface effects from depth effects (a ring detector or several spacings would).
* **Silicon detector.** It can't see the strong water bands at 1450 / 1950 nm.
* **Skin-tone dependence.** Melanin lowers visible-band signals. Per-user calibration helps, but a diverse dataset is still needed.
