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

`--real` fine-tunes on AI-mode CSVs recorded with `tools/oncosense_logger.py --send 4c --label <class>`.

Full write-up, including the honest limits of the simulated bootstrap model: [docs/08-neural-network.md](../docs/08-neural-network.md).
