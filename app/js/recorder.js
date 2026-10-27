// Records readings during a session and exports them as CSV or JSON lines.

const stamp = new Intl.DateTimeFormat("en-CA", {
  year: "numeric", month: "2-digit", day: "2-digit", hour: "2-digit", minute: "2-digit", hour12: false,
});

export class Recorder {
  constructor() {
    this.rows = [];
    this.recording = false;
    this.startedAt = null;
    this.exported = true;
  }

  start() {
    this.rows = [];
    this.recording = true;
    this.startedAt = new Date();
    this.exported = false;
  }

  stop() {
    this.recording = false;
  }

  add(reading) {
    if (!this.recording) return;
    this.rows.push({ received: new Date().toISOString(), ...reading });
    this.exported = false;
  }

  get count() {
    return this.rows.length;
  }

  get hasUnsaved() {
    return this.rows.length > 0 && !this.exported;
  }

  fileStem() {
    const d = this.startedAt ?? new Date();
    return "oncosense-" + stamp.format(d).replace(/[^0-9]+/g, "-").replace(/-$/, "");
  }

  // Flatten nested objects (probs, bands, flags, features) into columns.
  static flatten(obj, prefix = "", out = {}) {
    for (const [k, v] of Object.entries(obj)) {
      const key = prefix ? `${prefix}_${k}` : k;
      if (Array.isArray(v)) {
        if (k === "flags") out[key] = v.join("|") || "OK";
        else v.forEach((x, i) => (out[`${key}_${i}`] = x));
      } else if (v && typeof v === "object") {
        Recorder.flatten(v, key, out);
      } else {
        out[key] = v;
      }
    }
    return out;
  }

  toCsv() {
    const flat = this.rows.map((r) => Recorder.flatten(r));
    const cols = [...new Set(flat.flatMap((r) => Object.keys(r)))];
    const cell = (v) => {
      if (v == null) return "";
      const s = String(v);
      return /[",\n]/.test(s) ? `"${s.replace(/"/g, '""')}"` : s;
    };
    return [cols.join(","), ...flat.map((r) => cols.map((c) => cell(r[c])).join(","))].join("\n") + "\n";
  }

  toJsonl() {
    return this.rows.map((r) => JSON.stringify(r)).join("\n") + "\n";
  }

  download(kind) {
    const csv = kind === "csv";
    const blob = new Blob([csv ? this.toCsv() : this.toJsonl()], { type: csv ? "text/csv" : "application/x-ndjson" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = `${this.fileStem()}.${csv ? "csv" : "jsonl"}`;
    document.body.append(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 1000);
    this.exported = true;
  }
}
