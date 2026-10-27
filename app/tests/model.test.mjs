// The web app's JavaScript must compute exactly what the band computes.
// Vectors come from ml/train.py (tests/nn_test_vectors.h), the same file the
// C++ unit tests use, so Python, C++ and JavaScript are all checked together.
// Run: node --test app/tests

import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";

import { DemoDevice } from "../js/demo-device.js";
import { SkinNet } from "../js/model.js";
import { Novelty, classifyScore, extractFeatures, hydrationScore } from "../js/sim.js";

const root = new URL("../../", import.meta.url);
const header = readFileSync(new URL("tests/nn_test_vectors.h", root), "utf8");
const model = JSON.parse(readFileSync(new URL("app/model.json", root), "utf8"));

function cArray(name) {
  const m = header.match(new RegExp(`${name}\\[\\d+\\] = \\{([^}]*)\\}`));
  assert.ok(m, `array ${name} not found`);
  return m[1].split(",").map((s) => parseFloat(s));
}

const near = (a, b, tol, msg) => assert.ok(Math.abs(a - b) <= tol, `${msg}: ${a} vs ${b}`);

test("feature extraction matches Python and C++", () => {
  const scans = cArray("kFeatScans");
  const bases = cArray("kFeatBases");
  const expected = cArray("kFeatExpected");
  const unpack = (a, v) => ({
    white: a.slice(v * 30, v * 30 + 10),
    red: a.slice(v * 30 + 10, v * 30 + 20),
    nir: a.slice(v * 30 + 20, v * 30 + 30),
  });
  for (let v = 0; v < 4; v++) {
    const f = extractFeatures(unpack(scans, v), unpack(bases, v));
    f.forEach((x, i) => near(x, expected[v * 12 + i], 1e-4, `vector ${v} feature ${i}`));
  }
});

test("network inference matches numpy", () => {
  const net = new SkinNet(model);
  const inputs = cArray("kVectorInputs");
  const probs = cArray("kVectorProbs");
  const k = model.classes.length;
  for (let v = 0; v < inputs.length / 12; v++) {
    const out = net.predict(inputs.slice(v * 12, v * 12 + 12));
    out.probs.forEach((p, c) => near(p, probs[v * k + c], 1e-4, `vector ${v} class ${c}`));
  }
});

test("personalization learns a label and resets", () => {
  const net = new SkinNet(model);
  const x = cArray("kVectorInputs").slice(0, 12);
  const before = net.predict(x).best;
  const target = (before + 2) % model.classes.length;
  for (let i = 0; i < 3; i++) net.personalize(x, target);
  assert.equal(net.predict(x).best, target);
  assert.ok(Number.isNaN(net.personalize(x, 99)));
  net.resetHead();
  assert.equal(net.predict(x).best, before);
});

test("hydration score and novelty mirror the firmware", () => {
  near(hydrationScore(1.5, 1, 2), 50, 1e-9, "rising");
  near(hydrationScore(1.25, 2, 1), 75, 1e-9, "falling");
  assert.ok(Number.isNaN(hydrationScore(1, 1, 1.01)));
  assert.equal(classifyScore(131), "Occlusive Film");
  const nov = new Novelty();
  for (let i = 0; i < 20; i++) nov.update(Array(12).fill(0.001 * (i % 3)));
  assert.ok(nov.ready);
  assert.ok(nov.score(Array(12).fill(0.5)) > 3);
});

test("virtual band speaks the firmware protocol", async () => {
  const dev = new DemoDevice(new SkinNet(model), 1);
  const lines = [];
  dev.onLine((l) => lines.push(l));
  await dev.connect();
  await dev.send("4");
  await dev.send("a");
  await new Promise((r) => setTimeout(r, 2200));
  await dev.close();
  const json = lines.filter((l) => l.startsWith("{")).map((l) => JSON.parse(l));
  const status = json.filter((o) => o.type === "status").at(-1);
  assert.equal(status.mode, "ai");
  assert.equal(status.calibration.ai_baseline, true);
  assert.deepEqual(status.model.classes, model.classes);
  assert.ok(lines.some((l) => l.startsWith("# AI baseline stored")));
});
