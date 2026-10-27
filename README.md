<h1 align="center">OncoSense Band</h1>

<p align="center">
  <b>A wearable optical sensor that shines light into the skin and reads what comes back.</b><br>
  Student summer research in person at the MIT Lincoln Laboratory · Summer 2025
</p>

<p align="center">
  <a href="https://github.com/AyanBhardwaj1/OncoSense-MIT-LL/actions/workflows/ci.yml"><img src="https://github.com/AyanBhardwaj1/OncoSense-MIT-LL/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <img src="https://img.shields.io/badge/MCU-Adafruit%20QT%20Py%20SAMD21-6f42c1" alt="MCU">
  <img src="https://img.shields.io/badge/sensor-AS7341%2010--channel-0e7490" alt="Sensor">
  <img src="https://img.shields.io/badge/optics-660%20%2F%20850%20nm-b91c1c" alt="Wavelengths">
  <img src="https://img.shields.io/badge/firmware-v3.0.0%20(neural--net)-111827" alt="Firmware">
</p>

<p align="center">
  <img src="docs/images/band-worn-electronics.jpg" width="37%" alt="OncoSense Band worn on the arm, electronics side">
  <img src="docs/images/optical-head-on-skin.jpg" width="45%" alt="Optical head (NIR LED and spectral sensor) against the skin">
</p>

---

## Overview

Skin cancer is the most common cancer in the United States. More than **5 million** cases are diagnosed each year, and **1 in 5** Americans will develop it by age 70. When it is caught early, survival is **above 95 %**. Early detection depends on screening, and screening is episodic, expensive and hard to reach for many people.

**OncoSense** asks whether a low-cost **wearable** can use **light** to watch the skin continuously. The band holds near-infrared and red LEDs plus a 10-channel spectral sensor against the arm. It measures how the skin scatters and returns light, and turns that into a reading of skin state.

The project began with **nonlinear optics** (second harmonic generation from collagen, a known marker of tumors). Expert review showed SHG needs femtosecond lasers, so we pivoted to **linear optics** (diffuse reflectance and photoluminescence), which LEDs can drive. We aimed it first at **skin hydration and surface changes**, as a demonstrable stepping stone back toward cancer screening.

> ⚠️ Research prototype, not a medical device. OncoSense does not diagnose skin cancer or any other condition.

## Contents

| | |
|---|---|
| 📖 [Background](docs/01-background.md) | The problem, the goal, and the project context |
| 📚 [Literature review](docs/02-literature-review.md) · [PDF](docs/OncoSense_Literature_Review.pdf) | Optical cancer detection, optical hydration sensing, the components |
| 🔬 [Optics theory](docs/03-optics-theory.md) | Linear vs. nonlinear optics, light in skin, the firmware math |
| ✏️ [Design process](docs/04-design-process.md) | Timeline, expert consultations, sketches, iterations, trade-offs |
| 🔧 [Hardware](hardware/README.md) | BOM, wiring diagram, mechanical design, assembly |
| 💾 [Firmware](firmware/README.md) | Architecture, build, serial commands, what changed from v1 to v2 |
| 📊 [Testing and results](docs/05-testing-and-results.md) | What we tested, outcomes, root causes, validation protocol |
| 🎯 [Calibration and usage](docs/06-calibration-and-usage.md) | Step-by-step use of each mode, logging, tuning, troubleshooting |
| 🗺️ [Roadmap](docs/07-roadmap.md) | From the hydration proof of concept back to cancer screening |
| 🧠 [Neural network](docs/08-neural-network.md) · [ml/](ml) | Firmware v3: multi-source features, on-device MLP, novelty detection, on-band learning |
| 🖥️ [Web app](docs/09-web-app.md) · [app/](app) | OncoSense Monitor: browser dashboard that connects to the band over USB, with a virtual-band demo |
| 🧪 [Tests](tests/README.md) | Unit tests, the firmware running in a hardware simulator, tool tests, and end-to-end checks |
| 🗂️ [Engineering portfolio](portfolio/README.md) | A one-page case study of the whole project |

## How it works

```mermaid
flowchart LR
    subgraph Band["Neoprene arm band"]
        direction TB
        subgraph Inner["Inner face, against skin"]
            LED1["850 nm NIR LED"]
            LED2["660 nm red LED"]
            SENS["AS7341 spectral sensor<br/>F1–F8 · Clear · NIR"]
        end
        subgraph Outer["Outer face"]
            MCU["QT Py SAMD21"]
            BAT["Battery pack"]
        end
    end
    SKIN(("Skin<br/>scatter · absorb · emit"))
    LED1 & LED2 -- light in --> SKIN
    SKIN -- diffuse return --> SENS
    MCU -- GPIO --> LED1 & LED2
    SENS -- I²C / STEMMA QT --> MCU
    BAT --> MCU
    MCU -- USB serial / CSV --> PC["Laptop<br/>logger and analysis"]
```

1. **Illuminate.** An LED shines into the skin. The light scatters through the stratum corneum, epidermis and dermis.
2. **Detect.** The AS7341, mounted beside the LED, measures the light that comes back out. The side-by-side geometry favours light that has travelled through tissue over surface glare.
3. **Correct.** The firmware reads each sample with the LED **off** and then **on**, and subtracts the two to remove room light. It also auto-ranges the gain and averages several pairs.
4. **Interpret.** Signals are compared with the wearer's own calibration references (dry skin, moisturized skin, clean baseline, white card) and classified.

| Mode | Light | Channel | Output |
|---|---|---|---|
| **1 · Hydration** | 660 nm (or 850 nm) | F8 680 nm (or NIR) | Hydration score from 0 (dry ref) to 100 (moisturized ref) → Very Dry … Occlusive Film |
| **2 · Surface scan** | 850 nm | NIR | % change from your clean-skin baseline → Normal / Occlusive / Reduced return |
| **3 · Spectral** | On-board white LED | All 10 | Reflectance spectrum, **melanin index**, **erythema index** |

## Hardware

<p align="center">
  <img src="docs/images/band-inside-optical-head.jpg" width="24%" alt="Inside of the band">
  <img src="docs/images/band-electronics-outer.jpg" width="35%" alt="Electronics on the outer face">
  <img src="docs/images/band-worn-side.jpg" width="35%" alt="Band worn on the arm">
</p>

| Component | Part |
|---|---|
| Microcontroller | Adafruit QT Py SAMD21 |
| Detector | Adafruit AS7341 10-channel spectral sensor (415–680 nm + Clear + NIR) |
| Light sources | 850 nm NIR LED (EPIGAP OSA, 3D-printed mount) · 660 nm red LED · AS7341 on-board white LED |
| Wearable | Neoprene band, hook-and-loop closure, battery pocket, JST power connector |

Full BOM, wiring and assembly: **[hardware/README.md](hardware/README.md)**

## Firmware v2

The summer prototype ran two simple sketches (kept in [`firmware/legacy`](firmware/legacy)). Firmware v2 rebuilds them into one modular, tested firmware:

* 🌗 **Ambient-light rejection.** LED on/off differential measurement.
* 📈 **Auto-gain with saturation detection.** v1 silently clipped at 65 535 counts.
* 🎯 **Per-user two-point calibration.** The score is direction-agnostic, so it works whichever way hydration moves the signal.
* 🩹 **Quality flags.** `NO_CONTACT`, `AMBIENT_LEAK` and `SATURATED` on every reading.
* 🧪 **Unit-tested models.** `SkinModel.cpp` is pure C++ and tested on the host. The firmware is compiled in CI.
* 📄 **CSV streaming** for the Python [logger and analyzer](tools).
* 🔋 **Battery-safe boot.** v1's `while(!Serial)` froze the band whenever it was not plugged in over USB.

```bash
arduino-cli compile --upload -p <port> --fqbn adafruit:samd:adafruit_qtpy_m0 firmware/OncoSense
```

Then open a serial monitor at 115200 baud and type `h`. → **[firmware/README.md](firmware/README.md)**

## Neural network (firmware v3)

> This branch, **`neural-net`**, builds on firmware v2.

Mode **4 (AI)** reads the skin under **all three light sources**, extracts **12 features** (spectral change from the wearer's baseline, plus redness and pigment shape), and runs a **12→16→12→5 neural network on the QT Py itself**. That is about 480 parameters in 2 KB of flash, with no TensorFlow needed.

* **Trust signals.** Confidence, an **out-of-distribution** score (is this unlike the training data?) and a **personal novelty** score (is this unlike *my* recent readings?).
* **On-band learning.** `t0`–`t4` labels the current skin, and the band fine-tunes its output layer on the spot. Weights are saved to flash.
* **Training pipeline.** [`ml/`](ml) trains in pure numpy on a physics-informed skin-optics simulator, then exports C++ weights. Unit tests check the C++ against numpy.
* **Honest status.** The bootstrap model scores 75 % on *simulated* unseen people. It needs real labelled data, and the pipeline for that is built in.

→ **[docs/08-neural-network.md](docs/08-neural-network.md)**

## OncoSense Monitor (web app)

> This branch, **`web-app`**, builds on the `neural-net` branch.

<p align="center">
  <img src="docs/images/app-live-ai.jpg" width="80%" alt="OncoSense Monitor showing a live AI reading with class probabilities">
</p>

The computer-side companion to the band. It runs in the browser, connects to the band over USB (Web Serial, Chrome or Edge), and shows the neural network's reading live with confidence and trust signals. It also walks you through calibration, teaches the network with one click, and records sessions to CSV. **No band? The virtual band runs the same protocol and the same network on simulated skin.**

```bash
python3 app/serve.py
```

That opens `http://localhost:8765`. Press **Connect band**, or **Try the virtual band**. → **[docs/09-web-app.md](docs/09-web-app.md)**

## Results

| | |
|---|---|
| **Wearable** | Neoprene band with Velcro-mounted components. Clean matte finish, comfortable, light-tight |
| **Sensing** | ≈ **65 % success**. Most trials picked up skin hydration, but calibration was not consistent between sessions |
| **Proof of concept** | Optical skin sensing works on a wearable. Adding calibration and components is the path to a cancer-focused device |
| **Follow-up** | Root-cause analysis led to firmware v2 and a quantitative [validation protocol](docs/05-testing-and-results.md#54-validation-protocol-v2) |

## Repository layout

```
OncoSense-MIT-LL/
├── README.md                    ← you are here
├── portfolio/README.md          ← one-page engineering case study
├── docs/
│   ├── 01-background.md … 07-roadmap.md
│   ├── OncoSense_Literature_Review.pdf
│   └── images/                  ← product photos, prototypes, concept sketches
├── hardware/
│   ├── README.md                ← BOM, wiring, mechanical design, assembly
│   └── wiring-diagram.svg
├── ml/                          ← skin-optics simulator, feature extractor, numpy MLP trainer
├── firmware/
│   ├── OncoSense/               ← firmware v3 (Optics, SkinModel, Features, NeuralNet, Novelty, Storage)
│   └── legacy/                  ← original v1 sketches (MainSkin, AdaSpecCode)
├── app/                         ← OncoSense Monitor web app (Web Serial dashboard + virtual band)
├── tools/                       ← Python serial logger and session analyzer
├── tests/                       ← unit tests, firmware-in-the-loop simulator, tool tests
└── .github/workflows/ci.yml     ← unit tests and firmware compile on every push
```

## Team and acknowledgements

**Team:** Ayan Bhardwaj and Eric Niu. Built as student summer research in person at the MIT Lincoln Laboratory, and presented to an audience at MIT.

We are grateful to:

* **Dr. Yuzhe Xiao** and **Jack Hasty** (PhD candidate), Applied Physics Research Group, University of North Texas, for reviewing the optics and guiding the pivot from nonlinear to linear sensing.
* **Robert** at **AFFOA** (Advanced Functional Fabrics of America), for methods to scale the concept down into a textile wearable.
