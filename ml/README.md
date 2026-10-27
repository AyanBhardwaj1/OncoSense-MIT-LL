# ml/: neural network training

| File | Purpose |
|---|---|
| `skin_optics_sim.py` | Physics-informed simulator of AS7341 readings under each light source (melanin, blood, scattering, hydration, occlusive film, noise) |
| `features.py` | The 12-feature extractor. **Must match** `firmware/OncoSense/Features.cpp` (checked by `tests/test_ai.cpp`) |
| `train.py` | numpy-only MLP training. Exports `model_weights.h`, `model.json` and test vectors |
| `model.json` | Exported weights for the web app's demo mode |

```bash
pip install -r ml/requirements.txt
```

```bash
python ml/train.py
```

```bash
python ml/train.py --real data/*.csv
```

`--real` fine-tunes on AI-mode CSVs recorded with `tools/oncosense_logger.py --send 4c --label <class>`. The label must be one of `very_dry`, `dry`, `normal`, `well_hydrated` or `occlusive`. Rows the band flagged `NO_CONTACT`, `SATURATED` or `AMBIENT_LEAK`, and rows with non-finite features, are never used. Files from other modes are skipped. The run fails, with a count of what was skipped, if nothing usable is left. The exported accuracy is re-measured after fine-tuning.

Full write-up, including the honest limits of the simulated bootstrap model: [docs/08-neural-network.md](../docs/08-neural-network.md).
