# Tests

Every piece of code in this repository runs in a test on a laptop, and in CI on every push.

| Layer | What runs | Command |
|---|---|---|
| **Signal-model unit tests** | `SkinModel.cpp`: trimmed mean, trend, two-point hydration score, reference separation, v1 compatibility, surface scan, pigment indices | `make -C tests` |
| **Firmware in the loop** | The **real, unmodified firmware** (`firmware/OncoSense/*.ino` + `*.cpp`), compiled for the host against simulated Arduino, AS7341 and flash hardware with a virtual clock (`tests/firmware_sim/`). Tests drive it through its serial commands | `python -m pytest tests/firmware_sim -q` |
| **Tools** | Logger line routing and file naming, analyzer loading, grouping, statistics and plots | `python -m pytest tests/test_tools.py -q` |
| **End to end** | Firmware CSV output → logger session files → analyzer report | included in the firmware-in-the-loop suite |

```bash
make -C tests
```

```bash
python -m pytest tests -q
```

## The firmware simulator

`tests/firmware_sim/harness.cpp` includes `OncoSense.ino` directly and runs `setup()` and `loop()` from a scenario script:

```
boot                          # call setup()
scene src 1 <12 values>       # what the 660 nm LED returns on each AS7341 channel
scene ambient 0.05            # room light leaking under the band
scene contact 0               # band lifted off the skin
send d                        # type a serial command
run 6000                      # run loop() for 6 s of virtual time
```

The simulated AS7341 converts each scene into raw counts using the firmware's own gain and integration settings, and clips at full scale like the real chip. That lets the tests check ambient subtraction, auto-gain, saturation, contact and light-leak flags, calibration, and CSV structure. They cover behaviour that would otherwise need the physical band.

What the simulator cannot prove: real I²C timing, real LED optics, and how the real AS7341 library behaves when the cable is unplugged. Those still need a bench test on hardware.
