# 2 · Literature review

📄 **Full review (PDF):** [OncoSense_Literature_Review.pdf](OncoSense_Literature_Review.pdf)

This page is a structured summary of the full review. It covers the research that shaped each design decision.

---

## 2.1 Optical techniques for skin cancer detection

| Technique | What it measures | Evidence | Wearable? |
|---|---|---|---|
| **Diffuse reflectance spectroscopy (DRS)** | Visible or NIR light scattered back from tissue. Shaped by hemoglobin, melanin and tissue structure | Tested in vivo in clinical studies | ✅ LEDs and photodiodes |
| **Autofluorescence (LIAFS)** | Faint fluorescence from collagen, elastin, NADH and flavins under UV or blue excitation | BCC fluoresces less than normal skin, SCC more, and melanoma is strongly suppressed | ✅ needs a UV LED |
| **DRS + autofluorescence combined** | Structure, pigment and metabolism together | **About 93 % accuracy for malignant melanoma** in a combined system (Borisova et al.) | ✅ a target for future versions |
| **Second harmonic generation (SHG)** | Light at exactly half the excitation wavelength, produced by non-centrosymmetric collagen | Shows disordered tumor-associated collagen without labels. Polarization-sensitive SHG separates diseased from healthy skin | ❌ needs femtosecond lasers and high-NA optics |
| **Two-photon fluorescence** | Nonlinear excitation of tissue fluorophores | Lab microscopy | ❌ needs a mode-locked laser |

**Takeaway:** linear techniques (DRS and fluorescence) can be miniaturized. Nonlinear techniques (SHG and two-photon) reveal more but are still lab instruments. Combining several optical signals gives the best diagnostic accuracy.

## 2.2 Skin hydration by optical methods

* Water absorbs in the NIR at about **970, 1450 and 1950 nm** (O–H overtones).
* **Chen et al. (2023):** DRS from 400–1000 nm tracks hydration, with a **correlation of about 0.93** against the standard capacitance corneometer.
* **Jiang et al. (2011):** dehydrated skin (acetone/alcohol) became **brighter** and scattered more. Moisturized skin showed **less** reflectance and scattering. Water fills the air gaps in the stratum corneum and lowers the refractive-index mismatch.
* **Mamouei et al. (2021):** a wearable sensor with three NIR LEDs (about 970, 1200 and 1450 nm) and photodiodes tracked dermal water content against lab references. **A few discrete wavelengths are enough.**
* **Qassem & Kyriacou (2014–2015):** NIR reflectance tracked the effect of daily moisturizer use over weeks.

**What this means for OncoSense:** at 660–850 nm, hydration mostly shows up as a change in **scattering**. A strong mirror-like reflection off a wet or oily surface film can push the signal the other way. Measurement geometry and calibration have to handle both cases. This is why firmware v2 uses a **direction-agnostic two-point calibration**. See the [optics theory](03-optics-theory.md).

## 2.3 Key components

* **850 nm NIR LED.** Sits in the tissue "optical window", where blood absorbs less and light reaches the dermis. Silicon detectors are still sensitive here. The same family of wavelengths as pulse oximetry.
* **Miniature spectral sensor.** On-chip interference filters replace the gratings and monochromators of a bench spectrometer. Low-cost (< $100) multispectral sensors can now be embedded in wearables (Workman, 2024). *The review discussed the AS726x family; the build uses the **AS7341**, which has 8 visible bands plus Clear and NIR.*
* **Microcontroller (QT Py).** Postage-stamp-sized. Drives the LEDs, reads the sensor over I²C, and could run on-device ML in later versions.
* **Nonlinear optics (future).** A UV LED (365–405 nm) for autofluorescence is realistic. SHG is not, for a battery-powered wearable.

## 2.4 Trends that make this feasible

* Handheld Raman and autofluorescence probes have identified BCC during surgery, so optical diagnosis is moving from bench to bedside.
* Wearable NIR spectroscopy (muscle oxygenation, fNIRS headbands) shows that multi-channel NIR sensing can be wireless and wearable.
* Cosmetics and dermatology research increasingly wants objective, continuous skin measurements.

## 2.5 Conclusions that shaped the design

1. **Multimodal is the end goal.** More wavelengths and more techniques give more reliable diagnosis.
2. **Start with something you can demonstrate.** Hydration and surface sensing with reflectance is grounded in solid evidence and works with off-the-shelf parts.
3. **Validate against gold standards.** Compare against a corneometer for hydration, and against clinical assessment or biopsy for any lesion claims.
4. **Account for biological variability.** Skin tone (melanin), ambient light and placement all move the signal, so calibrate per user and measure differentially.

## References discussed

* Borisova et al. (2014): combined autofluorescence and diffuse reflectance for skin cancer diagnosis.
* Jiang et al. (2011): optical changes in skin from moisturizing and dehydration.
* Chen et al. (2023): 400–1000 nm DRS for quantifying skin hydration.
* Mamouei et al. (2021): wearable multi-wavelength NIR sensor for dermal water content.
* Qassem & Kyriacou (2014–2015): NIR reflectance monitoring of moisturizer effects.
* Workman (2024): review of portable and miniaturized spectroscopy.
* Park et al.: SHG imaging of collagen in skin and tumors.
* Dr. H's Instructables (2018): practical notes on AS726x spectral sensors.
