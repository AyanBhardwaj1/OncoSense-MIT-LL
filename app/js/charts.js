// Small, dependency-free SVG charts for the monitor.

const SVG_NS = "http://www.w3.org/2000/svg";
const fmtPct = new Intl.NumberFormat(undefined, { style: "percent", maximumFractionDigits: 0 });

function el(name, attrs = {}, text) {
  const node = document.createElementNS(SVG_NS, name);
  for (const [k, v] of Object.entries(attrs)) node.setAttribute(k, v);
  if (text != null) node.textContent = text;
  return node;
}

export const CLASS_LABELS = {
  very_dry: "Very dry",
  dry: "Dry",
  normal: "Normal",
  well_hydrated: "Well hydrated",
  occlusive: "Occlusive film",
};

// ------------------------------------------------------- probability bars --

// The most probable class, ignoring missing / null / non-numeric values.
export function topClass(probs) {
  let best = null;
  for (const [c, p] of Object.entries(probs ?? {})) {
    if (Number.isFinite(p) && (best === null || p > probs[best])) best = c;
  }
  return best;
}

export class ProbabilityBars {
  constructor(container) {
    this.container = container;
    this.rows = new Map();
  }

  setClasses(classes) {
    if (!Array.isArray(classes)) return;
    if (this.classes && this.classes.join() === classes.join()) return;
    this.classes = classes;
    this.container.replaceChildren();
    this.rows.clear();
    for (const c of classes) {
      const row = document.createElement("div");
      row.className = "prob";
      row.innerHTML = `
        <span class="prob-name"></span>
        <span class="prob-track" aria-hidden="true"><span class="prob-fill"></span></span>
        <span class="prob-value">–</span>`;
      row.querySelector(".prob-name").textContent = CLASS_LABELS[c] ?? c;
      row.querySelector(".prob-fill").style.background = `var(--c-${c}, var(--accent))`;
      this.container.append(row);
      this.rows.set(c, row);
    }
  }

  update(probs) {
    const top = topClass(probs);
    for (const [c, row] of this.rows) {
      const p = probs?.[c];
      const ok = Number.isFinite(p);
      row.dataset.top = String(c === top);
      row.querySelector(".prob-fill").style.transform = `scaleX(${ok ? Math.max(0, Math.min(1, p)) : 0})`;
      row.querySelector(".prob-value").textContent = ok ? fmtPct.format(p) : "–";
    }
  }
}

// ------------------------------------------------------------- history --

export class History {
  constructor(svg, capacity = 120) {
    this.svg = svg;
    this.capacity = capacity;
    this.points = [];
    this.range = [0, 1];
    this.bands = [];
    this.format = (v) => String(v);
  }

  configure({ range, bands = [], format, label }) {
    this.range = range;
    this.bands = bands;
    if (format) this.format = format;
    if (label) this.svg.setAttribute("aria-label", label);
    this.points = [];
    this.draw();
  }

  push(value) {
    if (!Number.isFinite(value)) return;
    this.points.push(value);
    if (this.points.length > this.capacity) this.points.shift();
    this.draw();
  }

  draw() {
    const W = 640;
    const H = 200;
    const top = 8;
    const bottom = H - 8;
    const left = 0;
    const right = W - 84;
    const [lo, hi] = this.range;
    const y = (v) => bottom - ((Math.min(hi, Math.max(lo, v)) - lo) / (hi - lo)) * (bottom - top);
    const nodes = [];

    // Shaded bands mark the class regions (tone steps, no grid lines).
    this.bands.forEach((b, i) => {
      if (i % 2 === 0) nodes.push(el("rect", { class: "band", x: left, y: y(b.to), width: right - left, height: Math.max(0, y(b.from) - y(b.to)) }));
      nodes.push(el("text", { class: "chart-text", x: right + 8, y: (y(b.from) + y(b.to)) / 2 + 4 }, b.label));
    });

    if (this.points.length === 0) {
      nodes.push(el("text", { class: "chart-empty", x: 0, y: H / 2 }, "The trace appears with the first reading."));
    } else {
      const n = this.points.length;
      const x = (i) => left + (n === 1 ? right : (i / (this.capacity - 1)) * (right - left));
      const pts = this.points.map((v, i) => `${x(i).toFixed(1)},${y(v).toFixed(1)}`);
      nodes.push(el("path", { class: "area", d: `M${x(0)},${bottom} L${pts.join(" L")} L${x(n - 1)},${bottom} Z`, opacity: 0.6 }));
      nodes.push(el("polyline", { class: "line", points: pts.join(" ") }));
      const last = this.points[n - 1];
      nodes.push(el("circle", { cx: x(n - 1), cy: y(last), r: 5, fill: "var(--accent)" }));
      if (!this.bands.length) {
        nodes.push(el("text", { class: "chart-text", x: right + 8, y: y(last) + 4 }, this.format(last)));
      }
    }
    this.svg.replaceChildren(...nodes);
  }
}

// ------------------------------------------------------------ spectrum --

// Flat colours approximating each AS7341 filter's wavelength.
const BAND_COLOURS = ["#7b4fd0", "#3f5fe0", "#1f9be0", "#18b07a", "#8cc63f", "#f2c12e", "#f07f2a", "#d8392b", "#9aa4a1", "#6e2a2a"];
const BAND_TEXT = ["415", "445", "480", "515", "555", "590", "630", "680", "Clear", "NIR"];

export function drawSpectrum(svg, values, { baseline = null, unit = "" } = {}) {
  const W = 640;
  const H = 220;
  const top = 12;
  const bottom = H - 30;
  const n = values.length;
  const gap = 10;
  const bw = (W - gap * (n - 1)) / n;
  const finite = values.filter(Number.isFinite);
  const max = Math.max(1e-9, ...finite, ...(baseline ? baseline.filter(Number.isFinite) : []));
  const nodes = [];
  values.forEach((v, i) => {
    const x = i * (bw + gap);
    const h = Number.isFinite(v) ? (v / max) * (bottom - top) : 0;
    nodes.push(el("rect", { class: "bar", x, y: bottom - h, width: bw, height: Math.max(h, 1), rx: 3, fill: BAND_COLOURS[i] }));
    if (baseline && Number.isFinite(baseline[i])) {
      const by = bottom - (baseline[i] / max) * (bottom - top);
      nodes.push(el("rect", { class: "baseline-tick", x: x - 2, y: by - 1.5, width: bw + 4, height: 3, rx: 1.5 }));
    }
    nodes.push(el("text", { class: "chart-text", x: x + bw / 2, y: H - 8, "text-anchor": "middle" }, BAND_TEXT[i]));
  });
  if (!finite.length) {
    nodes.push(el("text", { class: "chart-empty", x: 0, y: H / 2 }, "The spectrum appears with the first reading."));
  }
  svg.replaceChildren(...nodes);
  svg.setAttribute("aria-label", `Light returned in each sensor band${unit ? ", " + unit : ""}`);
}
