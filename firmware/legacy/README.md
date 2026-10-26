# Legacy firmware (v1, Summer 2025)

The original sketches from the summer prototype, preserved unchanged as a record of the project's starting point. **Use [`../OncoSense`](../OncoSense) for new work.**

| Sketch | What it did | Known issues (fixed in v2) |
|---|---|---|
| [`MainSkin`](MainSkin/MainSkin.ino) | 660 nm LED on A2, AS7341 F8 (680 nm) channel. Fixed thresholds: Very Dry < 150 ≤ Dry < 350 ≤ Healthy < 700 ≤ Vaseline-like. **Used in the demo video.** | Includes room light. Integration settings allow silent clipping. `while(!Serial)` blocks on battery. Thresholds are absolute |
| [`AdaSpecCode`](AdaSpecCode/AdaSpecCode.ino) | 850 nm LED on A1, AS7341 NIR channel. Baseline averaged at boot. Vaseline ≥ +20 %, rash ≤ −10 %, scar ≤ −25 %. Developed but not well tested. | `Wire.begin(4,5)` is ESP32-only. Baseline captured without a contact check. Room light included |
