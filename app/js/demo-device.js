// A virtual OncoSense Band for demo mode. It speaks the same serial protocol
// as firmware v3 (commands in, JSON lines and `#` messages out), so the app
// runs exactly the same code path with or without hardware. Readings come
// from the skin-optics simulator and the same neural network as the band.

import {
  ILLUMINATION, Novelty, classifyScore, classifySurface, erythemaIndex, extractFeatures,
  hydrationScore, makeRng, measure, melaninIndex, multiScan,
} from "./sim.js";

const FW = "3.0.0-demo";
const WHITE_CARD = 0.9;
const MODES = { 1: "hydration", 2: "scan", 3: "spectral", 4: "ai" };
const READING_MS = { hydration: 900, scan: 900, spectral: 1100, ai: 1800 };

export class DemoDevice {
  constructor(net, seed = 2025) {
    this.kind = "demo";
    this.net = net;
    this.rng = makeRng(seed);
    this.lineHandlers = [];
    this.closeHandlers = [];
    // A virtual wearer with medium skin tone, plus the skin under the band now.
    this.person = { melanin: 0.06, blood: 0.015, scatterA: 40, scatterB: 1.35 };
    this.skin = { hydration: 0.55, occlusive: 0, erythema: 0, coupling: 1 };
    this.contact = true;
    this.mode = "hydration";
    this.hydrationLed = "660nm";
    this.streaming = true;
    this.busy = false;
    this.teachPending = false;
    this.cal = {
      dry: null, moisturized: null, hydrationLed: "660nm", nirBaseline: null,
      white: null, aiBaseline: null,
    };
    this.novelty = new Novelty();
    this.trend = null;
    this.t0 = performance.now();
  }

  onLine(fn) {
    this.lineHandlers.push(fn);
  }

  onClose(fn) {
    this.closeHandlers.push(fn);
  }

  async connect() {
    this.say("# OncoSense Band firmware v" + FW + " (virtual band, simulated skin)");
    this.say("# Integration 50.0 ms, full scale 18000 counts");
    this.schedule();
  }

  async close() {
    clearTimeout(this.timer);
    this.closed = true;
    this.closeHandlers.forEach((fn) => fn(null));
  }

  // Demo-only: change the virtual skin from the app's control panel.
  setSkin(patch) {
    if ("contact" in patch) this.contact = patch.contact;
    Object.assign(this.skin, Object.fromEntries(Object.entries(patch).filter(([k]) => k in this.skin)));
  }

  say(line) {
    if (this.closed) return;
    queueMicrotask(() => this.lineHandlers.forEach((fn) => fn(line)));
  }

  emit(obj) {
    this.say(JSON.stringify(obj));
  }

  now() {
    return Math.round(performance.now() - this.t0);
  }

  schedule() {
    clearTimeout(this.timer);
    if (this.closed) return;
    this.timer = setTimeout(() => {
      if (this.streaming && !this.busy) this.reading();
      this.schedule();
    }, READING_MS[this.mode]);
  }

  // ---------------------------------------------------------- commands --

  // The firmware handles one command at a time and blocks during captures,
  // so the virtual band queues commands the same way.
  send(text) {
    const run = (this.queue ?? Promise.resolve()).then(async () => {
      for (const c of text) await this.command(c);
    });
    this.queue = run.catch(() => {}); // a failed command must not block later ones
    return run;
  }

  async command(c) {
    if (this.teachPending) {
      this.teachPending = false;
      const label = Number(c);
      if (Number.isInteger(label) && label >= 0 && label < this.net.classes.length) {
        await this.teach(label);
      } else {
        this.say("# Teach cancelled: expected a class number 0-4.");
      }
      this.status();
      return;
    }
    let changed = true;
    switch (c) {
      case "1": case "2": case "3": case "4":
        this.mode = MODES[c];
        this.trend = null;
        this.say("# Mode: " + this.mode);
        if (this.mode === "ai" && !this.cal.aiBaseline) {
          this.say("# Wear the band on clean, normal skin and press a for the AI baseline.");
        }
        this.schedule();
        break;
      case "n": case "c": case "j":
        break; // the virtual band always speaks JSON
      case "l":
        this.hydrationLed = this.hydrationLed === "660nm" ? "850nm" : "660nm";
        this.say("# Hydration LED: " + this.hydrationLed);
        this.trend = null;
        break;
      case "d": case "m":
        await this.capture(c === "d" ? "DRY reference" : "MOISTURIZED reference", 1400, () => {
          if (this.cal.hydrationLed !== this.hydrationLed) {
            Object.assign(this.cal, { dry: null, moisturized: null, hydrationLed: this.hydrationLed });
          }
          const v = this.average(() => this.hydrationSignal());
          this.cal[c === "d" ? "dry" : "moisturized"] = v;
          this.say("# Reference = " + v.toFixed(5));
        });
        break;
      case "b":
        await this.capture("clean-skin NIR baseline", 1400, () => {
          this.cal.nirBaseline = this.average(() => measure(this.person, this.skinNow(), "nir", this.rng)[9]);
          this.say("# Baseline = " + this.cal.nirBaseline.toFixed(5));
        });
        break;
      case "w":
        await this.capture("white reference", 1200, () => {
          this.cal.white = ILLUMINATION.white.map((v) => v * WHITE_CARD);
          this.say("# White reference stored.");
        });
        break;
      case "a":
        await this.capture("AI baseline on clean, normal skin", 2600, () => {
          const scans = Array.from({ length: 4 }, () => multiScan(this.person, this.skinNow(), this.rng));
          const avg = (k) => scans[0][k].map((_, i) => scans.reduce((s, sc) => s + sc[k][i], 0) / scans.length);
          this.cal.aiBaseline = { white: avg("white"), red: avg("red"), nir: avg("nir") };
          this.novelty.reset();
          this.say("# AI baseline stored.");
        });
        break;
      case "t":
        this.teachPending = true;
        this.say("# Teach: send the class number 0-4 for the skin under the band now.");
        changed = false;
        break;
      case "r":
        this.net.resetHead();
        this.say("# Neural network reset to factory weights.");
        break;
      case "x":
        this.cal = { dry: null, moisturized: null, hydrationLed: "660nm", nirBaseline: null, white: null, aiBaseline: null };
        this.net.resetHead();
        this.novelty.reset();
        this.say("# Calibration and personalization cleared.");
        break;
      case "p":
        this.streaming = !this.streaming;
        this.say(this.streaming ? "# Streaming resumed." : "# Streaming paused.");
        break;
      case "s":
        changed = false;
        this.status();
        break;
      default:
        changed = false;
        this.say(`# Unknown command '${c}'. Type h for help.`);
    }
    if (changed) this.status();
  }

  async capture(what, ms, fn) {
    this.busy = true;
    try {
      this.say(`# Capturing ${what}. Hold still...`);
      await new Promise((r) => setTimeout(r, ms));
      if (!this.contact) {
        this.say("# Calibration failed: check skin contact and re-seat the band.");
      } else {
        fn();
      }
    } finally {
      this.busy = false;
    }
  }

  async teach(label) {
    if (!this.cal.aiBaseline) {
      this.say("# Teach needs an AI baseline first (press a).");
      return;
    }
    this.busy = true;
    this.say(`# Teaching '${this.net.classes[label]}'. Hold still...`);
    await new Promise((r) => setTimeout(r, 1500));
    if (!this.contact) {
      this.say("# Teach failed: no clean readings. Check skin contact.");
      this.busy = false;
      return;
    }
    let fit = NaN;
    for (let i = 0; i < 3; i++) {
      const x = extractFeatures(multiScan(this.person, this.skinNow(), this.rng), this.cal.aiBaseline);
      fit = this.net.personalize(x, label);
    }
    this.say(`# Learned 3 examples; fit on all taught examples: ${Math.round(fit * 100)}%`);
    this.busy = false;
  }

  status() {
    this.emit({
      type: "status", fw: FW, mode: this.mode, streaming: this.streaming,
      hydration_led: this.hydrationLed, storage: "demo",
      calibration: {
        dry: this.cal.dry, moisturized: this.cal.moisturized, hydration_led: this.cal.hydrationLed,
        nir_baseline: this.cal.nirBaseline, white: !!this.cal.white, ai_baseline: !!this.cal.aiBaseline,
      },
      model: {
        classes: this.net.classes, personalized: this.net.personalized,
        examples: this.net.examples.length, novelty_ready: this.novelty.ready,
      },
    });
  }

  // ---------------------------------------------------------- readings --

  skinNow() {
    // Slow physiological drift so the trace looks alive, not frozen.
    const t = performance.now() / 1000;
    return {
      ...this.skin,
      hydration: Math.min(1, Math.max(0, this.skin.hydration + 0.015 * Math.sin(t / 7))),
      coupling: this.contact ? 1 + 0.03 * Math.sin(t / 3.1) : 0.02,
    };
  }

  average(fn, n = 6) {
    let s = 0;
    for (let i = 0; i < n; i++) s += fn();
    return s / n;
  }

  hydrationSignal() {
    return this.hydrationLed === "660nm"
      ? measure(this.person, this.skinNow(), "red", this.rng)[7]
      : measure(this.person, this.skinNow(), "nir", this.rng)[9];
  }

  flags() {
    return this.contact ? [] : ["NO_CONTACT"];
  }

  smooth(v) {
    if (!Number.isFinite(v) || !this.contact) return this.trend;
    this.trend = this.trend == null ? v : this.trend + 0.3 * (v - this.trend);
    return this.trend;
  }

  reading() {
    const t = this.now();
    if (this.mode === "hydration") {
      const signal = this.hydrationSignal();
      const c = this.cal;
      const calibrated = c.hydrationLed === this.hydrationLed && c.dry != null && c.moisturized != null;
      // The real band falls back to its v1 thresholds when uncalibrated. Those
      // are tied to real sensor counts, so the virtual band asks for calibration.
      const metric = calibrated ? hydrationScore(signal, c.dry, c.moisturized) : NaN;
      const state = !this.contact ? "No contact" : calibrated ? classifyScore(metric) : "Uncalibrated";
      this.emit({
        type: "hydration", t, led: this.hydrationLed, gain: 64, signal, ambient: 0.00008,
        metric: Number.isFinite(metric) ? metric : null, trend: this.smooth(metric), state,
        calibrated, flags: this.flags(),
      });
    } else if (this.mode === "scan") {
      const signal = measure(this.person, this.skinNow(), "nir", this.rng)[9];
      const ratio = this.cal.nirBaseline ? signal / this.cal.nirBaseline : null;
      const state = !this.contact ? "No contact" : classifySurface(ratio ?? NaN);
      this.emit({
        type: "scan", t, led: "850nm", gain: 32, signal, ambient: 0.00006, metric: ratio,
        trend: this.smooth(ratio), state, calibrated: ratio != null, flags: this.flags(),
      });
    } else if (this.mode === "spectral") {
      const raw = measure(this.person, this.skinNow(), "white", this.rng);
      const white = this.cal.white;
      const r = raw.map((v, i) => (white ? v / white[i] : v));
      const bands = Object.fromEntries(["f415", "f445", "f480", "f515", "f555", "f590", "f630", "f680", "clear", "nir"].map((b, i) => [b, r[i]]));
      this.emit({
        type: "spectral", t, gain: 16, reflectance: !!white, bands,
        melanin_idx: white ? melaninIndex(r[6]) : null,
        erythema_idx: white ? erythemaIndex(r[6], r[4]) : null, flags: this.flags(),
      });
    } else if (this.mode === "ai") {
      if (!this.cal.aiBaseline) {
        this.say("# AI mode needs a baseline: wear the band on clean, normal skin and press a.");
        return;
      }
      const scan = multiScan(this.person, this.skinNow(), this.rng);
      const x = extractFeatures(scan, this.cal.aiBaseline);
      const pred = this.net.predict(x);
      const ood = this.net.outOfDistribution(x);
      const nov = this.contact ? this.novelty.scoreAndUpdate(x) : this.novelty.score(x);
      const flags = this.flags();
      if (Number.isFinite(nov) && nov > 3) flags.push("NOVEL");
      if (ood > 3) flags.push("OOD");
      this.emit({
        type: "ai", t, label: this.contact ? pred.label : "no_contact", confidence: pred.confidence,
        probs: Object.fromEntries(this.net.classes.map((c, i) => [c, pred.probs[i]])),
        novelty: Number.isFinite(nov) ? nov : null, ood, personalized: this.net.personalized,
        features: x, white: scan.white, red680: scan.red[7], nir910: scan.nir[9], flags,
      });
    }
  }
}
