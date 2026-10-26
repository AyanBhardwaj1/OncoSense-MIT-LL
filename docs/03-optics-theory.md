# 3 · Optics theory

How light interacts with skin, why the project moved from nonlinear to linear optics, and the math the firmware uses.

## 3.1 Linear vs. nonlinear optics

**Linear optics.** The material's response does not depend on how bright the light is. A lens works because its thickness $L(x,y)$ changes across its face, which adds a position-dependent phase delay:

$$\Delta\Phi(x,y) = n\,k_0\,L(x,y), \qquad k_0 = \frac{2\pi}{\lambda}$$

Lens thickness varies quadratically with distance from the centre, so the phase delay is quadratic too. That turns a plane wave into a converging spherical wave.

**Nonlinear optics.** At high intensity the refractive index itself depends on intensity (the optical Kerr effect):

$$n(x,y) = n_0 + n_2\,I(x,y) \quad\Rightarrow\quad \Delta\Phi(x,y) = n_2\,k_0\,L\,I(x,y)$$

A Gaussian beam is most intense at its centre, so it raises the index most there. The medium becomes a lens and the beam **self-focuses**. The same high-intensity regime produces **second harmonic generation**: non-centrosymmetric structures such as collagen fibrils emit light at twice the frequency, which is half the wavelength:

$$\omega_{SHG} = 2\,\omega \qquad\Longleftrightarrow\qquad \lambda_{SHG} = \lambda/2 \quad(\text{e.g. } 800\ \text{nm} \to 400\ \text{nm})$$

Tumors remodel the collagen around them, so SHG images can separate cancerous from healthy tissue without any stain.

### Why we pivoted

We discussed the concept with **Dr. Yuzhe Xiao and PhD candidate Jack Hasty (Applied Physics Research Group, University of North Texas)**. They confirmed that the physics is sound. They also pointed out that SHG needs **high-peak-power pulsed (femtosecond) lasers**. An LED is several orders of magnitude too weak, and the 400 nm SHG light would be absorbed by skin before it got out.

**Decision:** use **linear** responses that an LED *can* drive:

* **Diffuse reflectance.** Light that enters the skin, scatters and comes back.
* **Photoluminescence.** Light the skin re-emits after it absorbs the excitation.

## 3.2 How light travels through skin

```
          LED                 AS7341
           │                    ▲
           ▼   ┌── specular ──┐ │      (surface glare: blocked by side-by-side geometry)
═══════════════╪══════════════╪═╪═════ stratum corneum  ← hydration changes scattering here
                ╲    diffuse   ╱
  epidermis      ╲   path     ╱        ← melanin absorbs (strongest in blue, falls toward NIR)
  dermis          ╰──────────╯         ← hemoglobin absorbs; collagen scatters
```

| Factor | Effect on the returned light |
|---|---|
| **Dry stratum corneum** | Air gaps between the cells raise the refractive-index mismatch, so there is more scattering and **more** diffuse return |
| **Hydrated stratum corneum** | Water fills the gaps, so there is less scattering, deeper penetration and **less** diffuse return |
| **Occlusive film** (petrolatum, oil) | Smooth, high-index surface layer. Adds mirror-like and near-surface return, so the signal often **rises** |
| **Melanin** | Absorbs strongly in the visible and less toward the NIR. Darker skin lowers the visible return |
| **Hemoglobin** | Absorbs green and yellow (about 540–580 nm). Redness lowers the green return relative to red |

The two hydration effects (scattering and film reflection) push the signal in **opposite directions**. The net direction depends on the wavelength, the spacing between LED and detector, and the product applied. Our v1 experiments saw higher readings with Vaseline. The literature reports lower diffuse reflectance after water-based moisturizers. Firmware v2 therefore calibrates the **direction** as well as the scale (see §3.4).

## 3.3 The AS7341 sensor

| Channel | Centre (nm) | Used for |
|---|---|---|
| F1–F4 | 415, 445, 480, 515 | Spectral mode |
| F5 | 555 | Erythema index (green, hemoglobin absorption) |
| F6 | 590 | Spectral mode |
| F7 | 630 | Melanin and erythema index (red) |
| F8 | 680 | **Hydration** under the 660 nm LED |
| Clear | broadband | Contact check in spectral mode |
| NIR | ~910, broad | **Surface scan** and hydration under the 850 nm LED |

Integration time and full scale:

$$t_{int} = (\text{ATIME}+1)(\text{ASTEP}+1)\times 2.78\,\mu s, \qquad \text{FS} = \min\big(65535,\ (\text{ATIME}+1)(\text{ASTEP}+1)\big)$$

v1 used ATIME = 100 and ASTEP = 999, so $(101)(1000) = 101\,000 > 65\,535$. Bright readings clipped without any warning. v2 uses ATIME = 29 and ASTEP = 599, which gives 50 ms and a full scale of 18 000 counts, plus auto-gain and a saturation flag.

## 3.4 Firmware math

**Ambient-corrected, normalized signal.** For $N$ LED-off/LED-on pairs:

$$S = \frac{\operatorname{trimmedMean}_{i=1..N}\big(\text{lit}_i - \text{dark}_i\big)}{G \cdot t_{int}}$$

with gain $G$ and $t_{int}$ in ms. The result is in "basic counts" and stays comparable after a gain change.

**Two-point hydration score.** The wearer records a dry reference $S_{dry}$ (after an alcohol wipe) and a moisturized reference $S_{wet}$:

$$H = 100 \cdot \frac{S - S_{dry}}{S_{wet} - S_{dry}}$$

$H = 0$ means "as dry as my dry reference" and $H = 100$ means "as hydrated as my moisturized reference". This holds whether moisturizing raises or lowers the signal. If the two references are within 3 % of each other the score is undefined, because the setup can't separate them.

| Score | State |
|---|---|
| < 25 | Very dry |
| 25–50 | Dry |
| 50–85 | Normal |
| 85–130 | Well hydrated |
| > 130 | Occlusive film (well past the moisturized reference) |

**Surface scan.** Ratio to the wearer's clean-skin NIR baseline, $R = S / S_{base}$, with the v1 thresholds kept:

| Ratio | State |
|---|---|
| ≥ 1.20 | Occlusive film (oil / petrolatum) |
| 0.90 – 1.20 | Normal |
| 0.75 – 0.90 | Reduced NIR return (v1 label: rash/acne) |
| ≤ 0.75 | Strongly reduced NIR return (v1 label: scar/dry lesion) |

**Pigment and redness indices.** These use reflectance against a white card, $R_\lambda = S_\lambda / S_{\lambda,white}$:

$$MI = 100\,\log_{10}\frac{1}{R_{630}}, \qquad EI = 100\,\log_{10}\frac{R_{630}}{R_{555}}$$

These are the classic reflectance-based melanin and erythema indices. They fulfil the "melanin project" listed as a next step in our final talk.

## 3.5 Why each wavelength

| Source | Why |
|---|---|
| **660 nm red** | Melanin absorbs less than in the blue and green, the AS7341 has a matching filter (F8, 680 nm), and it is sensitive to surface scattering |
| **850 nm NIR** | Optical window: light reaches the dermis, blood absorbs little, and silicon detectors still respond. Used for the depth-weighted surface scan |
| **White (on-board)** | Broadband illumination across all 8 visible filters, for pigment and redness |
| *970 / 1450 nm (future)* | Real water absorption bands. 1450 nm needs an InGaAs detector, because silicon stops responding at about 1100 nm |
| *365–405 nm UV (future)* | Excites tissue autofluorescence, the most proven path back toward cancer screening |
