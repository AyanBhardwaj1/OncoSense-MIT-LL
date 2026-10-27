import assert from "node:assert/strict";
import { test } from "node:test";

import { Recorder } from "../js/recorder.js";

test("recorder flattens readings into CSV columns", () => {
  const rec = new Recorder();
  rec.add({ type: "ai", label: "normal" }); // ignored: not recording yet
  rec.start();
  rec.add({ type: "ai", t: 10, label: "normal", probs: { dry: 0.1, normal: 0.9 }, flags: [] });
  rec.add({ type: "ai", t: 20, label: "dry", probs: { dry: 0.7, normal: 0.3 }, flags: ["NOVEL", "OOD"], note: 'a "quoted", value' });
  rec.stop();
  assert.equal(rec.count, 2);
  assert.ok(rec.hasUnsaved);

  const [header, first, second] = rec.toCsv().trim().split("\n");
  const cols = header.split(",");
  assert.ok(cols.includes("probs_normal"));
  assert.ok(cols.includes("flags"));
  assert.equal(first.split(",")[cols.indexOf("flags")], "OK");
  assert.ok(second.includes("NOVEL|OOD"));
  assert.ok(second.includes('"a ""quoted"", value"'));

  const lines = rec.toJsonl().trim().split("\n").map((l) => JSON.parse(l));
  assert.equal(lines[1].label, "dry");
  assert.match(rec.fileStem(), /^oncosense-\d{4}-\d{2}-\d{2}-\d{2}-\d{2}$/);
});
