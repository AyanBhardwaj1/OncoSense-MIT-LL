// Regression tests for the web-app review findings.
// Run: node --test app/tests/*.test.mjs

import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";

import { DemoDevice } from "../js/demo-device.js";
import { SkinNet } from "../js/model.js";
import { Recorder } from "../js/recorder.js";
import { SerialDevice } from "../js/serial-device.js";
import { topClass } from "../js/charts.js";

const model = JSON.parse(readFileSync(new URL("../model.json", import.meta.url), "utf8"));
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// A fake Web Serial port whose readable side we can feed or break.
function fakeSerial() {
  const listeners = new Set();
  const port = {
    opened: false,
    closed: false,
    written: "",
    readableController: null,
    async open() {
      this.opened = true;
      this.readable = new ReadableStream({ start: (c) => (this.readableController = c) });
      this.writable = new WritableStream({ write: (chunk) => (this.written += new TextDecoder().decode(chunk)) });
    },
    async close() {
      this.closed = true;
      this.readable = null;
    },
    feed(text) {
      this.readableController.enqueue(new TextEncoder().encode(text));
    },
    // Fatal errors leave port.readable null (Web Serial spec); non-fatal
    // ones (framing, overrun) replace it with a fresh stream.
    fail(err, { fatal = true } = {}) {
      this.readableController.error(err);
      this.readable = fatal ? null : new ReadableStream({ start: (c) => (this.readableController = c) });
    },
    // A broken port whose readable keeps erroring immediately.
    breakForever(err) {
      this.readable = { pipeTo: async (w) => { const wr = w.getWriter(); await wr.abort(err); throw err; } };
    },
  };
  const serial = {
    requestPort: async () => port,
    addEventListener: (_t, fn) => listeners.add(fn),
    removeEventListener: (_t, fn) => listeners.delete(fn),
  };
  // Node 22 has a read-only global `navigator`; replace it for this test.
  Object.defineProperty(globalThis, "navigator", { value: { serial }, configurable: true, writable: true });
  return { port, listeners };
}

test("a throwing line handler does not kill the serial connection", async () => {
  const { port } = fakeSerial();
  const dev = new SerialDevice();
  const seen = [];
  let closed = null;
  dev.onLine((line) => {
    seen.push(line);
    if (line.includes("boom")) throw new Error("render bug");
  });
  dev.onClose((reason) => (closed = reason));
  await dev.connect();
  port.feed('{"type":"boom"}\n{"type":"ok"}\n');
  await sleep(20);
  assert.deepEqual(seen, ['{"type":"boom"}', '{"type":"ok"}']);
  assert.equal(closed, null);
  assert.equal(port.written, "js");
  await dev.close();
});

test("a fatal read error releases the port and the disconnect listener", async () => {
  const { port, listeners } = fakeSerial();
  const dev = new SerialDevice();
  let reason;
  dev.onClose((r) => (reason = r));
  await dev.connect();
  assert.equal(listeners.size, 1);
  port.fail(new Error("device lost"));
  await sleep(30);
  assert.match(reason, /Connection lost/);
  assert.equal(port.closed, true, "port must be closed so it can be reopened");
  assert.equal(listeners.size, 0, "disconnect listener must be removed");
});

test("close() is idempotent and reports a user disconnect once", async () => {
  fakeSerial();
  const dev = new SerialDevice();
  const reasons = [];
  dev.onClose((r) => reasons.push(r));
  await dev.connect();
  await dev.close();
  await dev.close();
  assert.deepEqual(reasons, [null]);
});

test("virtual band runs commands one at a time, like the firmware", async () => {
  const dev = new DemoDevice(new SkinNet(model), 3);
  const lines = [];
  dev.onLine((l) => lines.push(l));
  await dev.connect();
  const capture = dev.send("4a");
  await sleep(10);
  const modeSwitch = dev.send("1"); // pressed while the AI baseline is being captured
  await Promise.all([capture, modeSwitch]);
  await sleep(10);
  await dev.close();
  const stored = lines.findIndex((l) => l.includes("AI baseline stored"));
  const mode = lines.findIndex((l) => l === "# Mode: hydration");
  assert.ok(stored >= 0 && mode > stored, "mode switch must wait for the capture to finish");
});

test("virtual band refuses to learn from a band with no skin contact", async () => {
  const dev = new DemoDevice(new SkinNet(model), 4);
  const lines = [];
  dev.onLine((l) => lines.push(l));
  await dev.connect();
  await dev.send("4a");
  dev.setSkin({ contact: false });
  await dev.send("t2");
  await sleep(10);
  await dev.close();
  assert.ok(lines.some((l) => l.startsWith("# Teach failed")));
  assert.equal(dev.net.personalized, false);
});

test("recorder treats readings added after an export as unsaved", () => {
  const rec = new Recorder();
  rec.start();
  rec.add({ type: "ai" });
  rec.exported = true; // what download() records
  assert.equal(rec.hasUnsaved, false);
  rec.add({ type: "ai" });
  assert.equal(rec.hasUnsaved, true);
});

test("topClass tolerates missing, empty and null probabilities", () => {
  assert.equal(topClass(undefined), null);
  assert.equal(topClass({}), null);
  assert.equal(topClass({ dry: null, normal: 0.7, occlusive: 0.2 }), "normal");
});

test("a non-fatal read error keeps the connection and later lines still arrive", async () => {
  const { port } = fakeSerial();
  const dev = new SerialDevice();
  const seen = [];
  let closed = "open";
  dev.onLine((l) => seen.push(l));
  dev.onClose((r) => (closed = r));
  await dev.connect();
  port.fail(new Error("framing"), { fatal: false });
  await sleep(20);
  port.feed('{"type":"after"}\n');
  await sleep(20);
  assert.deepEqual(seen, ['{"type":"after"}']);
  assert.equal(closed, "open");
  await dev.close();
});

test("a port that keeps erroring is shut down instead of spinning", async () => {
  const { port } = fakeSerial();
  const dev = new SerialDevice();
  let reason;
  dev.onClose((r) => (reason = r));
  await dev.connect();
  port.breakForever(new Error("stuck"));
  port.fail(new Error("stuck"), { fatal: false });
  port.breakForever(new Error("stuck"));
  await sleep(50);
  assert.match(reason ?? "", /Connection lost/);
  assert.equal(port.closed, true);
});
