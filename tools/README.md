# Tools

Python utilities for recording and analyzing OncoSense Band data.

```bash
pip install -r tools/requirements.txt
```

| Script | Purpose |
|---|---|
| `oncosense_logger.py` | Reads the band's CSV stream over USB serial and writes one labelled CSV per session/mode to `data/` |
| `analyze_session.py` | Per-label statistics (mean, SD, coefficient of variation, Cohen's d) and plots (time series, mean spectrum) to `figures/` |

```bash
python tools/oncosense_logger.py --list
```

```bash
python tools/oncosense_logger.py --port /dev/cu.usbmodem1101 --send 1c --label dry --seconds 60
```

```bash
python tools/analyze_session.py data/*.csv
```

Rows flagged `NO_CONTACT`, `AMBIENT_LEAK` or `SATURATED` are excluded from the statistics but still plotted. See [docs/06-calibration-and-usage.md](../docs/06-calibration-and-usage.md) for the full protocol.
