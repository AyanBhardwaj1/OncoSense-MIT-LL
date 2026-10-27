// OncoSense Monitor: the browser companion to the OncoSense Band.
// Talks to the band (or the virtual band) through one line-based protocol,
// renders live readings, guides calibration and records sessions.

import { CLASS_LABELS, History, ProbabilityBars, drawSpectrum } from "./charts.js";
import { DemoDevice } from "./demo-device.js";
import { SkinNet } from "./model.js";
import { Recorder } from "./recorder.js";
import { SerialDevice } from "./serial-device.js";

const $ = (id) => document.getElementById(id);
const VIEWS = ["ai", "hydration", "scan", "spectral"];
const MODE_KEY = { ai: "4", hydration: "1", scan: "2", spectral: "3" };
const nf1 = new Intl.NumberFormat(undefined, { maximumFractionDigits: 1, minimumFractionDigits: 1 });
const nf0 = new Intl.NumberFormat(undefined, { maximumFractionDigits: 0 });
const nfSig = new Intl.NumberFormat(undefined, { maximumSignificantDigits: 3 });
const pct = new Intl.NumberFormat(undefined, { style: "percent", maximumFractionDigits: 0 });
const signedPct = new Intl.NumberFormat(undefined, { style: "percent", maximumFractionDigits: 1, signDisplay: "exceptZero" });

const DESCRIPTIONS = {
  very_dry: "Much more light is scattered back near the surface than on your baseline skin. That is the optical signature of a dry, rough stratum corneum.",
  dry: "Somewhat more surface scattering than your baseline. The skin reads drier than usual.",
  normal: "Light returns much as it did when you set your baseline.",
  well_hydrated: "Less light scatters at the surface, so more of it travels into the skin. That is typical of well-hydrated skin.",
  occlusive: "A flat, mirror-like return across every band. That points to a film on the skin, such as petrolatum or oil.",
  no_contact: "The band can’t see its own LED light. Tighten the strap so the optical head sits flat on the skin.",
};

const FLAG_TEXT = {
  NO_CONTACT: ["No skin contact: tighten the band", "alert"],
  AMBIENT_LEAK: ["Room light is leaking in under the band", "warn"],
  SATURATED: ["Sensor saturated: reading clipped", "warn"],
  NOVEL: ["Unusual for you compared with recent readings", "warn"],
  OOD: ["Unlike the data the network was trained on", "warn"],
};

const CALIBRATION = {
  ai: [
    { key: "ai_baseline", cmd: "a", title: "AI baseline", how: "Wear the band on clean, normal skin and hold still for about 10 seconds.", action: "Capture baseline" },
  ],
  hydration: [
    { key: "dry", cmd: "d", title: "Dry reference", how: "Wipe the site with isopropyl alcohol and wait 2 minutes.", action: "Capture dry" },
    { key: "moisturized", cmd: "m", title: "Moisturized reference", how: "Apply moisturizer, wait 10 minutes, and blot the excess.", action: "Capture moisturized" },
  ],
  scan: [
    { key: "nir_baseline", cmd: "b", title: "Clean-skin baseline", how: "Place the band on clean, bare, healthy skin.", action: "Capture baseline" },
  ],
  spectral: [
    { key: "white", cmd: "w", title: "White reference", how: "Hold a plain white card, such as printer paper, flat over the sensor.", action: "Capture white card" },
  ],
};

const state = {
  device: null,
  net: null,
  status: null,
  view: "ai",
  busy: false,
  recorder: new Recorder(),
  probs: new ProbabilityBars($("probs")),
  history: new History($("history")),
  toastTimer: null,
};

// ------------------------------------------------------------- startup --

function init() {
  const params = new URLSearchParams(location.search);
  state.view = VIEWS.includes(params.get("view")) ? params.get("view") : "ai";

  const note = $("support-note");
  if (!window.isSecureContext) {
    note.textContent = "USB access needs a secure page. Open the monitor from localhost (python3 app/serve.py) or over https.";
    note.dataset.tone = "warn";
    $("connect").disabled = true;
  } else if (!SerialDevice.supported()) {
    note.textContent = "This browser can’t talk to USB devices. Use Chrome or Edge on a desktop computer, or try the virtual band.";
    note.dataset.tone = "warn";
    $("connect").disabled = true;
  } else {
    note.textContent = "Works in Chrome and Edge on desktop. Close the Arduino Serial Monitor first, because only one app can use the port at a time.";
  }

  $("connect").addEventListener("click", connectSerial);
  $("demo").addEventListener("click", connectDemo);
  $("disconnect").addEventListener("click", () => state.device?.close());
  $("pause").addEventListener("click", () => send("p"));
  $("record").addEventListener("click", toggleRecording);
  $("export-csv").addEventListener("click", () => exportSession("csv"));
  $("export-jsonl").addEventListener("click", () => exportSession("jsonl"));
  $("reset-net").addEventListener("click", () => armConfirm($("reset-net"), "Press again to forget what it learned", "r"));
  $("clear-cal").addEventListener("click", () => armConfirm($("clear-cal"), "Press again to clear everything", "x"));
  setupTabs();
  setupSkinControls();

  window.addEventListener("beforeunload", (e) => {
    if (state.recorder.hasUnsaved) {
      e.preventDefault();
      e.returnValue = "";
    }
  });

  if (params.get("demo") === "1") connectDemo();
  if ("serviceWorker" in navigator && location.protocol !== "file:") {
    navigator.serviceWorker.register("sw.js").catch(() => {});
  }
}

// ---------------------------------------------------------- connection --

async function connectSerial() {
  const device = new SerialDevice();
  setConnection("connecting", "Choose the band’s port…");
  try {
    attach(device);
    await device.connect();
    setConnection("live", "Band connected over USB");
    showDashboard(false);
    await send(MODE_KEY[state.view]);
  } catch (err) {
    state.device = null;
    if (err.name === "NotFoundError") {
      setConnection("idle", "Not connected");
      toast("No port chosen. Press Connect band and pick the QT Py port.");
    } else if (err.name === "NetworkError" || err.name === "InvalidStateError") {
      setConnection("error", "Port busy");
      toast("That port is in use. Close the Arduino Serial Monitor or other apps, then try again.");
    } else {
      setConnection("error", "Connection failed");
      toast(`Could not connect: ${err.message}. Unplug the band, plug it back in, and try again.`);
    }
  }
}

async function connectDemo() {
  try {
    state.net ??= await SkinNet.load("model.json");
  } catch (err) {
    toast(`Could not load the network weights (${err.message}). Serve the app folder over http.`);
    return;
  }
  const device = new DemoDevice(state.net);
  attach(device);
  await device.connect();
  setConnection("live", "Virtual band, simulated skin");
  showDashboard(true);
  await send("j");
  await send(MODE_KEY[state.view]);
  await send("s");
}

function attach(device) {
  state.device = device;
  device.onLine(handleLine);
  device.onClose((reason) => {
    if (state.device !== device) return;
    state.device = null;
    state.status = null;
    state.busy = false;
    showWelcome();
    if (reason) {
      setConnection("error", "Disconnected");
      toast(`${reason} Plug it back in and press Connect band.`);
    } else {
      setConnection("idle", "Not connected");
    }
  });
}

async function send(text) {
  if (!state.device) return;
  try {
    await state.device.send(text);
  } catch (err) {
    toast(`Could not send to the band: ${err.message}`);
  }
}

function setConnection(kind, text) {
  $("connection").dataset.state = kind;
  $("connection-text").textContent = text;
}

function showDashboard(isDemo) {
  $("welcome").hidden = true;
  $("dashboard").hidden = false;
  $("disconnect").hidden = false;
  $("skin-block").hidden = !isDemo;
  selectView(state.view, { sendMode: false });
  window.scrollTo({ top: 0 });
  $("main").focus({ preventScroll: true });
}

function showWelcome() {
  $("welcome").hidden = false;
  $("dashboard").hidden = true;
  $("disconnect").hidden = true;
}

// ----------------------------------------------------------- protocol --

function handleLine(line) {
  if (line.startsWith("{")) {
    let msg;
    try {
      msg = JSON.parse(line);
    } catch {
      log(line);
      return;
    }
    if (msg.type === "status") {
      updateStatus(msg);
    } else if (VIEWS.includes(msg.type)) {
      state.recorder.add(msg);
      updateSessionStatus();
      if (msg.type === state.view) renderReading(msg);
    }
    return;
  }
  log(line);
  const text = line.replace(/^#\s*/, "");
  if (/Capturing|Teaching/.test(text)) {
    state.busy = true;
    renderCalibration();
    renderTeach();
    updateAction();
  }
  if (/stored|failed|Reference =|Baseline =|Learned|cancelled|cleared|reset to factory/i.test(text)) {
    toast(text);
  }
  if (/^Learned/.test(text)) $("teach-status").textContent = text;
}

function updateStatus(status) {
  state.status = status;
  state.busy = false;
  const paused = status.streaming === false;
  $("pause").setAttribute("aria-pressed", String(paused));
  $("pause").textContent = paused ? "Resume" : "Pause";
  if (status.model?.classes) state.probs.setClasses(status.model.classes);
  renderCalibration();
  renderTeach();
  renderWaiting();
  updateAction();
}

// The next calibration step for this view, offered right in the reading card.
function updateAction() {
  const wrap = $("reading-action");
  const next = (CALIBRATION[state.view] ?? []).find((s) => !isDone(s.key));
  if (!next || !state.status) {
    wrap.hidden = true;
    return;
  }
  wrap.hidden = false;
  let btn = wrap.querySelector("button");
  if (!btn) {
    btn = document.createElement("button");
    btn.type = "button";
    btn.className = "btn btn-primary";
    btn.addEventListener("click", () => {
      state.busy = true;
      renderCalibration();
      updateAction();
      send(btn.dataset.cmd);
    });
    wrap.append(btn);
  }
  btn.dataset.cmd = next.cmd;
  btn.textContent = state.busy ? "Capturing… hold still" : next.action;
  btn.disabled = state.busy;
}

// Before the first reading of a view, say what is needed to get one.
function renderWaiting() {
  if (state.hasReading) return;
  const c = state.status?.calibration;
  if (state.view === "ai" && c && !c.ai_baseline) {
    setState("Capture your AI baseline to begin", "muted");
    $("reading-detail").textContent = "The network reads every change relative to your own skin. Wear the band on clean, normal skin, then press Capture baseline.";
  }
}

function log(line) {
  const pre = $("log");
  const lines = (pre.textContent + line + "\n").split("\n");
  pre.textContent = lines.slice(-200).join("\n");
  pre.scrollTop = pre.scrollHeight;
}

// --------------------------------------------------------------- views --

function setupTabs() {
  const tabs = [...document.querySelectorAll('[role="tab"]')];
  tabs.forEach((tab, i) => {
    tab.addEventListener("click", () => selectView(tab.dataset.view));
    tab.addEventListener("keydown", (e) => {
      const step = e.key === "ArrowRight" ? 1 : e.key === "ArrowLeft" ? -1 : 0;
      if (e.key === "Home" || e.key === "End" || step) {
        e.preventDefault();
        const next = e.key === "Home" ? 0 : e.key === "End" ? tabs.length - 1 : (i + step + tabs.length) % tabs.length;
        tabs[next].focus();
        selectView(tabs[next].dataset.view);
      }
    });
  });
}

function selectView(view, { sendMode = true } = {}) {
  state.view = view;
  const url = new URL(location.href);
  url.searchParams.set("view", view);
  history.replaceState(null, "", url);

  document.querySelectorAll('[role="tab"]').forEach((tab) => {
    const selected = tab.dataset.view === view;
    tab.setAttribute("aria-selected", String(selected));
    tab.tabIndex = selected ? 0 : -1;
  });
  $("panel").setAttribute("aria-labelledby", `tab-${view}`);

  const ai = view === "ai";
  $("probs-block").hidden = !ai;
  $("teach-block").hidden = !ai;
  $("spectrum-block").hidden = !(ai || view === "spectral");
  configureHistory(view);
  resetReading(view);
  state.hasReading = false;
  renderCalibration();
  renderTeach();
  if (sendMode) send(MODE_KEY[view]);
}

function configureHistory(view) {
  const configs = {
    ai: {
      range: [0, 1],
      bands: [
        { from: 0, to: 0.25, label: "Very dry" }, { from: 0.25, to: 0.5, label: "Dry" },
        { from: 0.5, to: 0.75, label: "Normal" }, { from: 0.75, to: 1, label: "Hydrated" },
      ],
      label: "Hydration estimate over time, from the network’s probabilities",
      note: "Hydration estimate, weighted by the network’s probabilities",
    },
    hydration: {
      range: [-10, 150],
      bands: [
        { from: -10, to: 25, label: "Very dry" }, { from: 25, to: 50, label: "Dry" },
        { from: 50, to: 85, label: "Normal" }, { from: 85, to: 130, label: "Hydrated" },
        { from: 130, to: 150, label: "Film" },
      ],
      label: "Hydration score over time",
      note: "Score: 0 = your dry reference, 100 = your moisturized reference",
    },
    scan: {
      range: [0.6, 1.4],
      bands: [
        { from: 0.6, to: 0.75, label: "Strong" }, { from: 0.75, to: 0.9, label: "Reduced" },
        { from: 0.9, to: 1.2, label: "Normal" }, { from: 1.2, to: 1.4, label: "Film" },
      ],
      label: "Near-infrared return relative to baseline over time",
      note: "Near-infrared return ÷ your clean-skin baseline",
    },
    spectral: {
      range: [0, 100],
      bands: [],
      format: (v) => nf0.format(v),
      label: "Melanin index over time",
      note: "Melanin index (needs a white reference)",
    },
  };
  const c = configs[view];
  state.history.configure(c);
  $("history-note").textContent = c.note;
}

function resetReading(view) {
  const meters = {
    ai: ["Confidence", "Novelty", "Unfamiliarity"],
    hydration: ["Score", "Signal", "LED"],
    scan: ["Change vs baseline", "Signal", "Gain"],
    spectral: ["Melanin index", "Erythema index", "Gain"],
  }[view];
  ["confidence", "novelty", "ood"].forEach((id, i) => {
    $(`meter-${id}`).querySelector(".meter-label").textContent = meters[i];
    $(`value-${id}`).textContent = "–";
    $(`value-${id}`).removeAttribute("data-tone");
  });
  setState("Waiting for the first reading…", "muted");
  $("reading-detail").textContent = view === "ai"
    ? "Each AI reading lights all three sources in turn, so it takes two or three seconds."
    : "Readings arrive about once a second.";
  $("flags").replaceChildren();
  $("reading-meta").hidden = true;
  updateAction();
  drawSpectrum($("spectrum"), Array(10).fill(NaN));
  $("spectrum-note").textContent = view === "ai" ? "Under the white LED, ambient light removed" : "";
}

function setState(text, tone) {
  const el = $("reading-state");
  el.textContent = text;
  if (tone) el.dataset.tone = tone;
  else el.removeAttribute("data-tone");
}

function setMeter(id, text, tone) {
  const el = $(`value-${id}`);
  el.textContent = text;
  if (tone) el.dataset.tone = tone;
  else el.removeAttribute("data-tone");
}

function renderFlags(flags = []) {
  const items = flags.filter((f) => FLAG_TEXT[f]).map((f) => {
    const li = document.createElement("li");
    li.textContent = FLAG_TEXT[f][0];
    li.dataset.tone = FLAG_TEXT[f][1];
    return li;
  });
  $("flags").replaceChildren(...items);
}

const fmt = (v, f = nfSig) => (Number.isFinite(v) ? f.format(v) : "–");

function renderReading(r) {
  state.hasReading = true;
  $("reading-meta").hidden = false;
  renderFlags(r.flags);
  const noContact = r.flags?.includes("NO_CONTACT");

  if (r.type === "ai") {
    if (noContact) {
      setState("No skin contact", "muted");
      $("reading-detail").textContent = DESCRIPTIONS.no_contact;
    } else {
      setState(CLASS_LABELS[r.label] ?? r.label);
      $("reading-detail").textContent = DESCRIPTIONS[r.label] ?? "";
    }
    setMeter("confidence", pct.format(r.confidence));
    setMeter("novelty", r.novelty == null ? "Learning…" : nf1.format(r.novelty), r.novelty > 3 ? "warn" : null);
    setMeter("ood", nf1.format(r.ood), r.ood > 3 ? "warn" : null);
    state.probs.update(r.probs);
    const p = r.probs;
    const dryWet = (p.very_dry ?? 0) + (p.dry ?? 0) + (p.normal ?? 0) + (p.well_hydrated ?? 0);
    if (!noContact && dryWet > 0.05) {
      state.history.push(((p.dry ?? 0) / 3 + (2 * (p.normal ?? 0)) / 3 + (p.well_hydrated ?? 0)) / dryWet);
    }
    drawSpectrum($("spectrum"), r.white ?? []);
    return;
  }

  if (r.type === "hydration") {
    setState(r.state, noContact || !r.calibrated ? "muted" : null);
    $("reading-detail").textContent = noContact
      ? DESCRIPTIONS.no_contact
      : r.calibrated
        ? "Scored against your own dry and moisturized references, so it works for your skin and this band position."
        : "Capture a dry and a moisturized reference to turn this signal into a hydration score.";
    setMeter("confidence", r.calibrated ? fmt(r.metric, nf0) : "–");
    setMeter("novelty", fmt(r.signal));
    setMeter("ood", r.led === "850nm" ? "850 nm" : "660 nm");
    if (r.calibrated && !noContact) state.history.push(r.metric);
    return;
  }

  if (r.type === "scan") {
    setState(r.state, noContact || !r.calibrated ? "muted" : null);
    $("reading-detail").textContent = noContact
      ? DESCRIPTIONS.no_contact
      : r.calibrated
        ? "Near-infrared light reaches the dermis. This compares how much returns with your clean-skin baseline. These are experimental categories, not a diagnosis."
        : "Capture a clean-skin baseline first. Every surface-scan reading is compared with it.";
    setMeter("confidence", r.calibrated ? signedPct.format(r.metric - 1) : "–");
    setMeter("novelty", fmt(r.signal));
    setMeter("ood", `${fmt(r.gain, nf0)}×`);
    if (r.calibrated && !noContact) state.history.push(r.metric);
    return;
  }

  if (r.type === "spectral") {
    const bands = ["f415", "f445", "f480", "f515", "f555", "f590", "f630", "f680", "clear", "nir"].map((b) => r.bands?.[b]);
    drawSpectrum($("spectrum"), bands, { unit: r.reflectance ? "as reflectance against the white card" : "raw" });
    $("spectrum-note").textContent = r.reflectance ? "Reflectance against your white card" : "Raw signal (no white reference yet)";
    if (r.reflectance && Number.isFinite(r.melanin_idx)) {
      setState(`Melanin index ${nf0.format(r.melanin_idx)}`);
      $("reading-detail").textContent = "Pigment and redness from red and green reflectance. Higher melanin index means more pigment, and higher erythema index means more redness.";
      state.history.push(r.melanin_idx);
    } else {
      setState("Uncalibrated spectrum", "muted");
      $("reading-detail").textContent = "Capture a white reference to turn raw light into reflectance and pigment indices.";
    }
    setMeter("confidence", fmt(r.melanin_idx, nf1));
    setMeter("novelty", fmt(r.erythema_idx, nf1));
    setMeter("ood", `${fmt(r.gain, nf0)}×`);
  }
}

// --------------------------------------------------------- calibration --

const TICK = '<svg viewBox="0 0 16 16" width="14" height="14" aria-hidden="true"><path d="M3.5 8.5l3 3 6-7" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"/></svg>';

function isDone(key) {
  const c = state.status?.calibration;
  if (!c) return false;
  if (key === "dry" || key === "moisturized") return c[key] != null && c.hydration_led === state.status.hydration_led;
  if (key === "nir_baseline") return c[key] != null;
  return c[key] === true;
}

function renderCalibration() {
  const steps = CALIBRATION[state.view] ?? [];
  const list = $("cal-steps");
  list.replaceChildren(
    ...steps.map((s) => {
      const li = document.createElement("li");
      li.className = "cal-step";
      const done = isDone(s.key);
      li.dataset.done = String(done);
      li.innerHTML = `<span class="tick">${TICK}</span><span class="cal-step-title"></span><span class="cal-step-how"></span>`;
      li.querySelector(".cal-step-title").textContent = s.title + (done ? " (done)" : "");
      li.querySelector(".cal-step-how").textContent = s.how;
      const btn = document.createElement("button");
      btn.type = "button";
      btn.className = `btn btn-small ${done ? "btn-quiet" : "btn-primary"}`;
      btn.textContent = state.busy ? "Capturing…" : done ? "Capture again" : s.action;
      btn.disabled = state.busy || !state.status;
      btn.addEventListener("click", () => {
        state.busy = true;
        renderCalibration();
        send(s.cmd);
      });
      li.append(btn);
      return li;
    }),
  );

  if (state.view === "hydration") {
    const led = state.status?.hydration_led ?? "660nm";
    const li = document.createElement("li");
    li.className = "cal-step";
    li.dataset.done = "true";
    li.innerHTML = `<span class="tick">${TICK}</span><span class="cal-step-title"></span><span class="cal-step-how"></span>`;
    li.querySelector(".cal-step-title").textContent = `Light source: ${led === "850nm" ? "850 nm near-infrared" : "660 nm red"}`;
    li.querySelector(".cal-step-how").textContent = "Switching the LED needs new references.";
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "btn btn-quiet btn-small";
    btn.textContent = led === "850nm" ? "Use 660 nm" : "Use 850 nm";
    btn.disabled = state.busy || !state.status;
    btn.addEventListener("click", () => send("l"));
    li.append(btn);
    list.append(li);
  }

  const notes = {
    ai: "The network reads every change relative to your own baseline.",
    hydration: "Two references make the score personal to your skin.",
    scan: "Surface changes are measured against healthy skin.",
    spectral: "A white card turns raw light into reflectance.",
  };
  $("cal-note").textContent = notes[state.view];
}

function renderTeach() {
  const wrap = $("teach");
  const classes = state.status?.model?.classes ?? Object.keys(CLASS_LABELS);
  const ready = !!state.status?.calibration?.ai_baseline;
  if (wrap.childElementCount !== classes.length) {
    wrap.replaceChildren(
      ...classes.map((c, i) => {
        const btn = document.createElement("button");
        btn.type = "button";
        btn.className = "btn";
        btn.dataset.index = String(i);
        btn.innerHTML = `<span class="swatch" aria-hidden="true"></span><span></span>`;
        btn.querySelector(".swatch").style.background = `var(--c-${c})`;
        btn.lastElementChild.textContent = CLASS_LABELS[c] ?? c;
        btn.setAttribute("aria-label", `Teach: my skin is ${(CLASS_LABELS[c] ?? c).toLowerCase()} right now`);
        btn.addEventListener("click", () => {
          state.busy = true;
          renderTeach();
          send("t" + i);
        });
        return btn;
      }),
    );
  }
  wrap.querySelectorAll("button").forEach((b) => (b.disabled = !ready || state.busy || !state.status));
  const m = state.status?.model;
  if (!ready) $("teach-status").textContent = "Capture the AI baseline first.";
  else if (state.busy) $("teach-status").textContent = "Learning from three scans…";
  else if (m?.personalized) $("teach-status").textContent = `Personalized${m.examples ? ` with ${m.examples} examples this session` : ""}.`;
  else $("teach-status").textContent = "Using factory weights.";
  $("reset-net").disabled = !m?.personalized || state.busy;
}

// -------------------------------------------------------------- session --

function toggleRecording() {
  const rec = state.recorder;
  if (rec.recording) {
    rec.stop();
    toast(`Recording stopped: ${nf0.format(rec.count)} readings. Export them from the Session panel.`);
  } else {
    rec.start();
  }
  $("record").setAttribute("aria-pressed", String(rec.recording));
  $("record-label").textContent = rec.recording ? "Stop recording" : "Record session";
  updateSessionStatus();
}

function updateSessionStatus() {
  const rec = state.recorder;
  const n = nf0.format(rec.count);
  $("session-status").textContent = rec.recording
    ? `Recording: ${n} readings so far.`
    : rec.count
      ? `${n} readings recorded${rec.hasUnsaved ? ", not exported yet" : ""}.`
      : "Not recording. Recorded readings stay in this browser until you export them.";
  $("export-csv").disabled = rec.count === 0;
  $("export-jsonl").disabled = rec.count === 0;
}

function exportSession(kind) {
  state.recorder.download(kind);
  updateSessionStatus();
}

// Destructive actions need a second press within 4 seconds.
function armConfirm(btn, armedText, cmd) {
  btn.dataset.restLabel ??= btn.textContent;
  const disarm = () => {
    clearTimeout(btn.armTimer);
    btn.dataset.armed = "false";
    btn.textContent = btn.dataset.restLabel;
  };
  if (btn.dataset.armed === "true") {
    disarm();
    send(cmd);
    return;
  }
  btn.dataset.armed = "true";
  btn.textContent = armedText;
  btn.armTimer = setTimeout(disarm, 4000);
}

// ---------------------------------------------------------- virtual skin --

function setupSkinControls() {
  const bind = (id, key, scale) => {
    const input = $(`skin-${id}`);
    const out = $(`out-${id}`);
    input.addEventListener("input", () => {
      const v = Number(input.value);
      out.textContent = `${v} %`;
      if (state.device?.kind === "demo") state.device.setSkin({ [key]: (v / 100) * scale });
    });
  };
  bind("hydration", "hydration", 1);
  bind("occlusive", "occlusive", 1);
  bind("erythema", "erythema", 0.01);
  $("skin-contact").addEventListener("change", (e) => {
    if (state.device?.kind === "demo") state.device.setSkin({ contact: e.target.checked });
  });
}

// ---------------------------------------------------------------- toast --

function toast(text) {
  const el = $("toast");
  el.textContent = text.replace(/\.\.\./g, "…");
  el.dataset.show = "true";
  clearTimeout(state.toastTimer);
  state.toastTimer = setTimeout(() => (el.dataset.show = "false"), 4500);
}

init();
