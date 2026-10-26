# 1 · Background and motivation

## The problem

Skin cancer is the most common cancer in the United States:

* **Over 5 million** skin cancer cases are diagnosed in the U.S. each year.
* **1 in 5** Americans will develop skin cancer by age 70.
* When skin cancer is **caught early**, survival rates are **above 95 %**.

<sub>Figures as presented in our final talk. They are the widely cited U.S. statistics published by the Skin Cancer Foundation and the American Academy of Dermatology.</sub>

The gap is not treatment. It is **early detection**. Today's screening path is a dermatologist visit, a dermoscope, and a biopsy when something looks suspicious. That path is expensive and episodic, and many people simply don't have access to it. We set out to ask:

> *Could a low-cost wearable use light to watch the skin continuously, so that changes get noticed sooner?*

## Why optics

Light is a natural fit for skin. It is non-invasive, fast and cheap to generate, and decades of research show that healthy and malignant skin interact with light differently:

* **Diffuse reflectance** changes with tissue structure, blood (hemoglobin) and pigment (melanin).
* **Autofluorescence** changes with tissue metabolism (NADH, flavins) and structural proteins (collagen, elastin).
* **Second harmonic generation (SHG)** from collagen exposes the disordered collagen around tumors.

The [literature review](02-literature-review.md) covers this in depth.

## Project goal

Build a **wearable arm band** that holds a light source and a miniature spectrometer against the skin, records how the skin returns light, and turns that into useful information.

| Stage | Target |
|---|---|
| Vision | Tell carcinogenic skin apart from healthy skin using linear and nonlinear optical signatures |
| Feasible now | Measure skin **hydration** and **surface changes** (occlusive films, irritated or scarred patches) with diffuse reflectance and photoluminescence |
| Proof of concept | A working band, firmware that classifies skin state, and a documented path back toward cancer screening |

## Context

OncoSense was built by Ayan Bhardwaj and Eric (**EA Labs**) as student summer research in person at the MIT Lincoln Laboratory in Summer 2025, and was presented to an audience at MIT at the end of the program.

**This is a research prototype, not a medical device.** It does not diagnose skin cancer or any other condition.
