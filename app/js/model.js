// The OncoSense skin-state network in JavaScript.
// Same weights (model.json, exported by ml/train.py) and same math as
// firmware/OncoSense/NeuralNet.cpp, so demo mode behaves like the band.

const HEAD_L2 = 0.02;

function dense(W, b, x, relu) {
  const out = new Array(W.length);
  for (let o = 0; o < W.length; o++) {
    let z = b[o];
    const row = W[o];
    for (let i = 0; i < x.length; i++) z += row[i] * x[i];
    out[o] = relu && z < 0 ? 0 : z;
  }
  return out;
}

function softmax(z) {
  const mx = Math.max(...z);
  const e = z.map((v) => Math.exp(v - mx));
  const sum = e.reduce((a, b) => a + b, 0);
  return e.map((v) => v / sum);
}

function argmax(a) {
  let best = 0;
  for (let i = 1; i < a.length; i++) if (a[i] > a[best]) best = i;
  return best;
}

export class SkinNet {
  constructor(model) {
    this.model = model;
    this.classes = model.classes;
    const [W1, b1, W2, b2, W3, b3] = model.weights;
    Object.assign(this, { W1, b1, W2, b2, W3, b3 });
    this.resetHead();
  }

  static async load(url = "model.json") {
    const res = await fetch(url);
    if (!res.ok) throw new Error(`Could not load ${url} (${res.status})`);
    return new SkinNet(await res.json());
  }

  resetHead() {
    this.headW = this.W3.map((row) => row.slice());
    this.headB = this.b3.slice();
    this.examples = [];
    this.personalized = false;
  }

  standardize(features) {
    return features.map((v, i) => (v - this.model.mean[i]) / this.model.std[i]);
  }

  hidden(features) {
    const h1 = dense(this.W1, this.b1, this.standardize(features), true);
    return dense(this.W2, this.b2, h1, true);
  }

  predict(features) {
    const probs = softmax(dense(this.headW, this.headB, this.hidden(features), false));
    const best = argmax(probs);
    return { probs, best, label: this.classes[best], confidence: probs[best] };
  }

  // RMS z-score against the training data (NeuralNet::outOfDistribution).
  outOfDistribution(features) {
    const z = this.standardize(features);
    return Math.sqrt(z.reduce((s, v) => s + v * v, 0) / z.length);
  }

  // On-band personalization, mirrored: SGD on the output layer only, with an
  // L2 pull toward the factory weights. Returns fit accuracy on all examples.
  personalize(features, label, epochs = 40, lr = 0.05) {
    if (!(label >= 0 && label < this.classes.length)) return NaN;
    this.examples.push({ h: this.hidden(features), label });
    if (this.examples.length > 24) this.examples.shift();
    for (let e = 0; e < epochs; e++) {
      for (const { h, label: y } of this.examples) {
        const p = softmax(dense(this.headW, this.headB, h, false));
        p[y] -= 1;
        for (let o = 0; o < p.length; o++) {
          for (let i = 0; i < h.length; i++) {
            const w = this.headW[o][i];
            this.headW[o][i] -= lr * (p[o] * h[i] + HEAD_L2 * (w - this.W3[o][i]));
          }
          this.headB[o] -= lr * p[o];
        }
      }
    }
    this.personalized = true;
    const correct = this.examples.filter(
      ({ h, label: y }) => argmax(dense(this.headW, this.headB, h, false)) === y,
    ).length;
    return correct / this.examples.length;
  }
}
