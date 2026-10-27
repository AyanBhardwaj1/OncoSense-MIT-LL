// Skin optics simulator + feature extractor for demo mode.
// A JavaScript port of ml/skin_optics_sim.py and ml/features.py; the feature
// extractor must match firmware/OncoSense/Features.cpp exactly (tested in
// app/tests/model.test.mjs against vectors exported from Python).

export const BANDS = ["f415", "f445", "f480", "f515", "f555", "f590", "f630", "f680", "clear", "nir"];
export const BAND_NM = [415, 445, 480, 515, 555, 590, 630, 680, null, 910];
const CENTRES = [415, 445, 480, 515, 555, 590, 630, 680, 0, 910];
const MU_A_BLOOD = [2700, 700, 150, 120, 230, 120, 6, 4, 0, 5];
const MU_A_WATER = [0, 0, 0, 0, 0, 0, 0.003, 0.005, 0, 0.07];
export const ILLUMINATION = {
  nir: [0, 0, 0, 0, 0, 0, 0, 0.01, 0.6, 1.0],
  red: [0, 0, 0, 0, 0, 0.02, 0.25, 0.6, 0.7, 0.02],
  white: [0.15, 0.9, 0.5, 0.55, 0.8, 0.85, 0.7, 0.45, 1.0, 0.03],
};
const CLEAR = 8;

// Small seeded RNG so a demo session is repeatable.
export function makeRng(seed = 1) {
  let s = seed >>> 0;
  const uniform = () => {
    s = (s + 0x6d2b79f5) >>> 0;
    let t = s;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
  const normal = () => {
    const u = Math.max(uniform(), 1e-12);
    return Math.sqrt(-2 * Math.log(u)) * Math.cos(2 * Math.PI * uniform());
  };
  return { uniform, normal };
}

export function reflectance(person, skin) {
  const dryness = 1 - skin.hydration;
  const r = CENTRES.map((c, i) => {
    const lam = c > 0 ? c : 600;
    const muMel = person.melanin * 6.6e11 * lam ** -3.33 * 0.1;
    const muA = muMel + (person.blood + skin.erythema) * MU_A_BLOOD[i] + 0.7 * MU_A_WATER[i] + 0.2;
    let muS = person.scatterA * (1 + 0.35 * dryness) * (lam / 500) ** -person.scatterB;
    muS *= 1 - 0.1 * skin.occlusive;
    let v = Math.exp(-7 * Math.sqrt(muA / (3 * (muA + muS))));
    v += 0.015 + 0.05 * dryness + 0.07 * skin.occlusive;
    return v;
  });
  r[CLEAR] = r.slice(0, 8).reduce((a, b) => a + b, 0) / 8;
  return r;
}

// Ambient-corrected "basic counts" for one light source, all 10 bands.
export function measure(person, skin, source, rng, noise = 0.02) {
  const r = reflectance(person, skin);
  return r.map((v, i) => {
    let s = v * ILLUMINATION[source][i] * skin.coupling;
    s *= 1 + noise * rng.normal();
    s += 1e-4 * Math.abs(rng.normal());
    return Math.max(s, 1e-6);
  });
}

export function multiScan(person, skin, rng) {
  return { white: measure(person, skin, "white", rng), red: measure(person, skin, "red", rng), nir: measure(person, skin, "nir", rng) };
}

const FLOOR = 1e-6;
const safeLog = (num, den) => Math.log(Math.max(num, FLOOR) / Math.max(den, FLOOR));

// features[12], identical to Features.cpp / features.py.
export function extractFeatures(scan, base) {
  const f = [];
  for (let i = 0; i < 8; i++) f.push(safeLog(scan.white[i], base.white[i]));
  f.push(safeLog(scan.red[7], base.red[7]));
  f.push(safeLog(scan.nir[9], base.nir[9]));
  f.push(safeLog(scan.white[6], scan.white[4]));
  f.push(safeLog(scan.white[7], scan.white[2]));
  return f;
}

// ---- SkinModel.cpp ports (hydration / surface / pigment) ----

export function hydrationScore(signal, dry, wet) {
  const span = wet - dry;
  const scale = Math.max(Math.abs(dry), Math.abs(wet));
  if (!Number.isFinite(signal) || scale <= 0 || Math.abs(span) < 0.03 * scale) return NaN;
  return (100 * (signal - dry)) / span;
}

export function classifyScore(score) {
  if (!Number.isFinite(score)) return "Unknown";
  if (score < 25) return "Very Dry";
  if (score < 50) return "Dry";
  if (score < 85) return "Normal";
  if (score <= 130) return "Well Hydrated";
  return "Occlusive Film";
}

export function classifySurface(ratio) {
  if (!Number.isFinite(ratio)) return "No baseline";
  if (ratio >= 1.2) return "Occlusive film (oil / petrolatum)";
  if (ratio <= 0.75) return "Strongly reduced NIR return (scar/lesion-like)";
  if (ratio <= 0.9) return "Reduced NIR return (rash/acne-like)";
  return "Normal skin";
}

export const melaninIndex = (rRed) => (rRed > 0 ? 100 * Math.log10(1 / rRed) : NaN);
export const erythemaIndex = (rRed, rGreen) => (rRed > 0 && rGreen > 0 ? 100 * Math.log10(rRed / rGreen) : NaN);

// Personal novelty detector (Novelty.cpp port).
export class Novelty {
  constructor(alpha = 0.05, warmup = 15) {
    this.alpha = alpha;
    this.warmup = warmup;
    this.reset();
  }
  reset() {
    this.count = 0;
    this.mean = [];
    this.var = [];
  }
  get ready() {
    return this.count >= this.warmup;
  }
  score(x) {
    if (!this.ready) return NaN;
    let sum = 0;
    x.forEach((v, i) => {
      const d = v - this.mean[i];
      sum += (d * d) / Math.max(this.var[i], 1e-4);
    });
    return Math.sqrt(sum / x.length);
  }
  update(x) {
    if (this.count === 0) {
      this.mean = x.slice();
      this.var = x.map(() => 0);
    } else {
      const a = this.count < this.warmup ? 1 / (this.count + 1) : this.alpha;
      x.forEach((v, i) => {
        const d = v - this.mean[i];
        this.mean[i] += a * d;
        this.var[i] = (1 - a) * (this.var[i] + a * d * d);
      });
    }
    this.count++;
  }
  scoreAndUpdate(x) {
    const s = this.score(x);
    this.update(x);
    return s;
  }
}
