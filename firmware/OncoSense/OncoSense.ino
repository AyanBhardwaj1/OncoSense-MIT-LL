// =============================================================================
//  OncoSense Band — firmware v3 (neural network)
//  Wearable optical skin sensing: hydration, surface changes, pigment, and an
//  on-device neural network that reads all three light sources together.
//
//  Built during student summer research, in person at the MIT Lincoln
//  Laboratory, by Ayan Bhardwaj and Eric Niu.
//
//  Hardware: Adafruit QT Py (SAMD21) + Adafruit AS7341 10-channel spectral
//  sensor + 850 nm NIR LED + 660 nm red LED, sewn into a neoprene arm band.
//
//  Modes (switch over USB serial, 115200 baud, type `h` for help):
//    1  Hydration     660 nm (or 850 nm) diffuse reflectance, two-point calibrated
//    2  Surface scan  850 nm NIR return vs. your own clean-skin baseline
//    3  Spectral      on-board white LED, 10-band reflectance + melanin / erythema
//    4  AI            all three sources -> 12 features -> neural network,
//                     plus personal novelty and out-of-distribution scores
//
//  Output formats: readable text, CSV, or JSON lines (used by the web app).
//
//  This is a research prototype, not a medical device. It does not diagnose
//  skin cancer or any other condition.
// =============================================================================

#include <Wire.h>
#include <Adafruit_AS7341.h>

#include "config.h"
#include "Features.h"
#include "NeuralNet.h"
#include "Novelty.h"
#include "Optics.h"
#include "SkinModel.h"
#include "State.h"
#include "Storage.h"

using namespace oncosense;

// ------------------------------------------------------------------ State --

enum class Mode : uint8_t { Hydration = 1, SurfaceScan = 2, Spectral = 3, Ai = 4 };
enum class Format : uint8_t { Text, Csv, Json };

enum Flag : uint8_t {
  FLAG_NO_CONTACT = 1,
  FLAG_AMBIENT_LEAK = 2,
  FLAG_SATURATED = 4,
  FLAG_NOVEL = 8,
  FLAG_OOD = 16,
};

static Optics optics;
static Calibration cal;
static NeuralNet net;
static Novelty novelty;
static Mode mode = Mode::Hydration;
static LightSource hydrationLed = LightSource::Red660;
static Format format = Format::Text;
static bool streaming = true;
static bool headerPending = true;
static bool teachPending = false;
static unsigned long teachPendingSince = 0;
static Ema trend(0.3f);
static unsigned long lastMeasurementMs = 0;

// ------------------------------------------------------------- Prototypes --

static void printHelp();
static void printStatus();
static void handleCommand(char c);
static void setMode(Mode m);
static void setFormat(Format f);
static void runMeasurement();
static void runHydration();
static void runSurfaceScan();
static void runSpectral();
static void runAi();
static bool captureAverage(LightSource src, as7341_color_channel_t ch, float &result);
static void captureWhiteReference();
static bool captureMultiScan(MultiScan &scan, uint8_t &flags);
static void captureAiBaseline();
static void teach(int label);
static void persist();
static void restore();
static as7341_color_channel_t hydrationChannel();
static const char *modeName(Mode m);
static void info(const __FlashStringHelper *msg);
static uint8_t frameFlags(const Frame &f, as7341_color_channel_t watch);
static void printFlags(uint8_t flags);
static void jsonKey(const char *key);
static void jsonNum(float v, int digits);
static void jsonStr(const char *s);
static void jsonFlags(uint8_t flags);
static void toBands(const Frame &f, float *bands);

// ------------------------------------------------------------------ Setup --

void setup() {
  Serial.begin(SERIAL_BAUD);
  unsigned long start = millis();
  while (!Serial && millis() - start < SERIAL_WAIT_MS) {
  }

  if (!optics.begin()) {
    // Blink the NIR LED as a visible fault code (use a phone camera to see it).
    Serial.println(F("# ERROR: AS7341 not found. Check the STEMMA QT cable."));
    pinMode(PIN_LED_NIR, OUTPUT);
    while (true) {
      digitalWrite(PIN_LED_NIR, HIGH);
      delay(100);
      digitalWrite(PIN_LED_NIR, LOW);
      delay(900);
    }
  }

  Serial.print(F("# OncoSense Band firmware v"));
  Serial.println(F(ONCOSENSE_FW_VERSION));
  Serial.print(F("# Integration "));
  Serial.print(optics.integrationMs(), 1);
  Serial.print(F(" ms, full scale "));
  Serial.print(optics.fullScale());
  Serial.println(F(" counts"));
  restore();
  printHelp();
}

// ------------------------------------------------------------------- Loop --

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n' || c == ' ') continue;
    if (teachPending) {
      teachPending = false;
      if (c >= '0' && c < '0' + NeuralNet::kClasses) {
        teach(c - '0');
      } else {
        info(F("Teach cancelled: expected a class number 0-4."));
      }
      if (format == Format::Json) printStatus();
      continue;
    }
    handleCommand(c);
  }

  // Checked only after draining input: a class number that arrived while a
  // blocking measurement ran must still count as the teach label.
  if (teachPending && millis() - teachPendingSince > TEACH_TIMEOUT_MS) {
    teachPending = false;
    info(F("Teach timed out: send t and the class number together, e.g. t2."));
  }

  if (streaming && millis() - lastMeasurementMs >= MEASUREMENT_PERIOD_MS) {
    lastMeasurementMs = millis();
    runMeasurement();
  }
}

// --------------------------------------------------------------- Commands --

static void printHelp() {
  Serial.println(F("#"));
  Serial.println(F("# Modes:   1 hydration   2 surface scan   3 spectral   4 AI"));
  Serial.println(F("# Output:  n readable    c CSV            j JSON lines"));
  Serial.println(F("# Calibrate:"));
  Serial.println(F("#   d / m  DRY / MOISTURIZED reference      (hydration)"));
  Serial.println(F("#   l      toggle hydration LED 660 <-> 850 nm"));
  Serial.println(F("#   b      clean-skin NIR baseline          (surface scan)"));
  Serial.println(F("#   w      white reference card             (spectral)"));
  Serial.println(F("#   a      multi-source AI baseline         (AI)"));
  Serial.println(F("# Neural network:"));
  Serial.println(F("#   t0..t4 teach: label the current skin as"));
  Serial.println(F("#          0 very_dry  1 dry  2 normal  3 well_hydrated  4 occlusive"));
  Serial.println(F("#   r      reset the network to factory weights"));
  Serial.println(F("# Other:   x clear all calibration   p pause/resume   s status   h help"));
  Serial.println(F("#"));
}

static void handleCommand(char c) {
  float value;
  bool changed = true;
  switch (c) {
    case '1': setMode(Mode::Hydration); break;
    case '2': setMode(Mode::SurfaceScan); break;
    case '3': setMode(Mode::Spectral); break;
    case '4': setMode(Mode::Ai); break;

    case 'n': setFormat(Format::Text); break;
    case 'c': setFormat(Format::Csv); break;
    case 'j': setFormat(Format::Json); break;

    case 'l':
      hydrationLed = hydrationLed == LightSource::Red660 ? LightSource::Nir850
                                                         : LightSource::Red660;
      Serial.print(F("# Hydration LED: "));
      Serial.println(toString(hydrationLed));
      trend.reset();
      headerPending = true;
      break;

    case 'd':
    case 'm':
      info(c == 'd' ? F("Capturing DRY reference. Hold still...")
                    : F("Capturing MOISTURIZED reference. Hold still..."));
      if (captureAverage(hydrationLed, hydrationChannel(), value)) {
        if (cal.hydrationLed != hydrationLed) {
          // References from a different LED are meaningless now.
          if (!isnan(cal.dryRef) || !isnan(cal.hydratedRef)) {
            info(F("References captured with the other LED were discarded."));
          }
          cal.dryRef = NAN;
          cal.hydratedRef = NAN;
          cal.hydrationLed = hydrationLed;
        }
        (c == 'd' ? cal.dryRef : cal.hydratedRef) = value;
        Serial.print(F("# Reference = "));
        Serial.println(value, 5);
        if (!isnan(cal.dryRef) && !isnan(cal.hydratedRef) &&
            !referencesSeparable(cal.dryRef, cal.hydratedRef)) {
          info(F("Dry and moisturized references are too close (< 3 % apart) to score between."));
          info(F("Recapture after a bigger change in the skin."));
        }
        trend.reset();
        persist();
      }
      break;

    case 'b':
      info(F("Capturing clean-skin NIR baseline. Hold still..."));
      if (captureAverage(LightSource::Nir850, AS7341_CHANNEL_NIR, value)) {
        cal.nirBaseline = value;
        Serial.print(F("# Baseline = "));
        Serial.println(value, 5);
        trend.reset();
        persist();
      }
      break;

    case 'w': captureWhiteReference(); break;
    case 'a': captureAiBaseline(); break;

    case 't':
      teachPending = true;
      teachPendingSince = millis();
      info(F("Teach: send the class number 0-4 for the skin under the band now."));
      changed = false;
      break;

    case 'r':
      net.resetHead();
      persist();
      info(F("Neural network reset to factory weights."));
      break;

    case 'x':
      cal = Calibration();
      net.resetHead();
      novelty.reset();
      trend.reset();
      eraseState();
      info(F("Calibration and personalization cleared."));
      break;

    case 'p':
      streaming = !streaming;
      info(streaming ? F("Streaming resumed.") : F("Streaming paused."));
      break;

    case 's': changed = false; printStatus(); break;
    case 'h':
    case '?': changed = false; printHelp(); break;
    default:
      changed = false;
      Serial.print(F("# Unknown command '"));
      Serial.print(c);
      Serial.println(F("'. Type h for help."));
  }
  // Keep the web app's view of the device in sync.
  if (changed && format == Format::Json) printStatus();
}

static void setMode(Mode m) {
  mode = m;
  trend.reset();
  headerPending = true;
  Serial.print(F("# Mode: "));
  Serial.println(modeName(m));
  if (m == Mode::SurfaceScan && isnan(cal.nirBaseline)) {
    info(F("Place the band on clean, bare skin and press b to set the baseline."));
  }
  if (m == Mode::Spectral && !cal.hasWhite) {
    info(F("Hold a white card against the sensor and press w for a white reference."));
  }
  if (m == Mode::Ai && !cal.hasAiBaseline) {
    info(F("Wear the band on clean, normal skin and press a for the AI baseline."));
  }
}

static void setFormat(Format f) {
  format = f;
  headerPending = true;
}

static void printStatus() {
  if (format == Format::Json) {
    Serial.print(F("{\"type\":\"status\",\"fw\":\"" ONCOSENSE_FW_VERSION "\","));
    jsonKey("mode"); jsonStr(modeName(mode)); Serial.print(',');
    jsonKey("streaming"); Serial.print(streaming ? F("true") : F("false")); Serial.print(',');
    jsonKey("hydration_led"); jsonStr(toString(hydrationLed)); Serial.print(',');
    jsonKey("storage"); jsonStr(storageBackend()); Serial.print(',');
    Serial.print(F("\"calibration\":{"));
    jsonKey("dry"); jsonNum(cal.dryRef, 6); Serial.print(',');
    jsonKey("moisturized"); jsonNum(cal.hydratedRef, 6); Serial.print(',');
    jsonKey("hydration_led"); jsonStr(toString(cal.hydrationLed)); Serial.print(',');
    jsonKey("nir_baseline"); jsonNum(cal.nirBaseline, 6); Serial.print(',');
    jsonKey("white"); Serial.print(cal.hasWhite ? F("true") : F("false")); Serial.print(',');
    jsonKey("ai_baseline"); Serial.print(cal.hasAiBaseline ? F("true") : F("false"));
    Serial.print(F("},\"model\":{\"classes\":["));
    for (int i = 0; i < NeuralNet::kClasses; i++) {
      if (i) Serial.print(',');
      jsonStr(NeuralNet::className(i));
    }
    Serial.print(F("],"));
    jsonKey("personalized"); Serial.print(net.isPersonalized() ? F("true") : F("false"));
    Serial.print(',');
    jsonKey("examples"); Serial.print(net.exampleCount());
    Serial.print(',');
    jsonKey("novelty_ready"); Serial.print(novelty.ready() ? F("true") : F("false"));
    Serial.println(F("}}"));
    return;
  }
  Serial.print(F("# Firmware v" ONCOSENSE_FW_VERSION " | mode: "));
  Serial.print(modeName(mode));
  Serial.print(F(" | hydration LED: "));
  Serial.print(toString(hydrationLed));
  Serial.print(F(" | storage: "));
  Serial.println(storageBackend());
  Serial.print(F("# Dry ref: "));
  Serial.print(cal.dryRef, 5);
  Serial.print(F(" | moisturized ref: "));
  Serial.print(cal.hydratedRef, 5);
  Serial.print(F(" | NIR baseline: "));
  Serial.print(cal.nirBaseline, 5);
  Serial.print(F(" | white ref: "));
  Serial.print(cal.hasWhite ? F("yes") : F("no"));
  Serial.print(F(" | AI baseline: "));
  Serial.println(cal.hasAiBaseline ? F("yes") : F("no"));
  Serial.print(F("# Network: "));
  Serial.print(net.isPersonalized() ? F("personalized") : F("factory"));
  Serial.print(F(" ("));
  Serial.print(net.exampleCount());
  Serial.print(F(" examples this session) | novelty model: "));
  Serial.println(novelty.ready() ? F("ready") : F("warming up"));
}

// ---------------------------------------------------------- Measurements --

static void runMeasurement() {
  switch (mode) {
    case Mode::Hydration:   runHydration(); break;
    case Mode::SurfaceScan: runSurfaceScan(); break;
    case Mode::Spectral:    runSpectral(); break;
    case Mode::Ai:          runAi(); break;
  }
}

// Shared output for the single-value modes (hydration, surface scan).
static void printScalar(const char *type, const char *led, const Frame &f, float signal,
                        float ambient, float metric, float smooth, const char *state,
                        bool calibrated, uint8_t flags) {
  if (format == Format::Json) {
    Serial.print(F("{\"type\":"));
    jsonStr(type);
    Serial.print(F(",\"t\":"));
    Serial.print(millis());
    Serial.print(',');
    jsonKey("led"); jsonStr(led); Serial.print(',');
    jsonKey("gain"); jsonNum(f.gainX, 1); Serial.print(',');
    jsonKey("signal"); jsonNum(signal, 6); Serial.print(',');
    jsonKey("ambient"); jsonNum(ambient, 6); Serial.print(',');
    jsonKey("metric"); jsonNum(metric, 4); Serial.print(',');
    jsonKey("trend"); jsonNum(smooth, 4); Serial.print(',');
    jsonKey("state"); jsonStr(state); Serial.print(',');
    jsonKey("calibrated"); Serial.print(calibrated ? F("true") : F("false")); Serial.print(',');
    jsonFlags(flags);
    Serial.println('}');
    return;
  }
  if (format == Format::Csv) {
    if (headerPending) {
      Serial.println(F("t_ms,mode,led,gain,signal,ambient,metric,trend,state,flags"));
      headerPending = false;
    }
    Serial.print(millis());   Serial.print(',');
    Serial.print(type);       Serial.print(',');
    Serial.print(led);        Serial.print(',');
    Serial.print(f.gainX, 1); Serial.print(',');
    Serial.print(signal, 6);  Serial.print(',');
    Serial.print(ambient, 6); Serial.print(',');
    Serial.print(metric, 4);  Serial.print(',');
    Serial.print(smooth, 4);  Serial.print(',');
    Serial.print(state);      Serial.print(',');
    printFlags(flags);
    Serial.println();
    return;
  }
  Serial.print('[');
  Serial.print(type);
  Serial.print(' ');
  Serial.print(led);
  Serial.print(F("] signal "));
  Serial.print(signal, 5);
  Serial.print(F("  metric "));
  Serial.print(metric, 3);
  Serial.print(F("  ->  "));
  Serial.print(state);
  if (!calibrated) Serial.print(F("  (uncalibrated)"));
  Serial.print(F("  ["));
  printFlags(flags);
  Serial.println(']');
}

static void runHydration() {
  as7341_color_channel_t ch = hydrationChannel();
  Frame f;
  if (!optics.measure(hydrationLed, ch, f)) {
    info(F("Sensor read error."));
    return;
  }

  bool calibrated = cal.hydrationLed == hydrationLed && !isnan(cal.dryRef) &&
                    !isnan(cal.hydratedRef);
  float signal = f.signal[ch];
  float metric;
  HydrationState state;
  if (calibrated) {
    metric = hydrationScore(signal, cal.dryRef, cal.hydratedRef);
    state = classifyScore(metric);
  } else {
    // Fall back to the v1 (MainSkin) thresholds, which were tuned for 660 nm.
    metric = toLegacyCounts(signal);
    state = hydrationLed == LightSource::Red660 ? classifyLegacy(metric)
                                                : HydrationState::Unknown;
  }
  uint8_t flags = frameFlags(f, ch);
  bool contact = !(flags & FLAG_NO_CONTACT);
  float smooth = contact ? trend.update(metric) : trend.value();
  printScalar("hydration", toString(hydrationLed), f, signal, f.ambient[ch], metric, smooth,
              contact ? toString(state) : "No contact", calibrated, flags);
}

static void runSurfaceScan() {
  const as7341_color_channel_t ch = AS7341_CHANNEL_NIR;
  Frame f;
  if (!optics.measure(LightSource::Nir850, ch, f)) {
    info(F("Sensor read error."));
    return;
  }
  float signal = f.signal[ch];
  bool calibrated = cal.nirBaseline > 0.0f;
  float ratio = calibrated ? signal / cal.nirBaseline : NAN;
  SurfaceState state = classifySurface(signal, cal.nirBaseline);
  uint8_t flags = frameFlags(f, ch);
  bool contact = !(flags & FLAG_NO_CONTACT);
  float smooth = contact ? trend.update(ratio) : trend.value();
  const char *label = !contact ? "No contact" : (!calibrated ? "No baseline" : toString(state));
  printScalar("scan", "850nm", f, signal, f.ambient[ch], ratio, smooth, label, calibrated, flags);
}

static void runSpectral() {
  const as7341_color_channel_t watch = AS7341_CHANNEL_CLEAR;
  Frame f;
  if (!optics.measure(LightSource::OnboardWhite, watch, f)) {
    info(F("Sensor read error."));
    return;
  }

  // Reflectance relative to the white card if we have one, else raw signal.
  float r[12];
  for (uint8_t b = 0; b < kNumBands; b++) {
    uint8_t ch = kBands[b];
    if (!cal.hasWhite) {
      r[ch] = f.signal[ch];
    } else {
      // A band the card returned no light in has no reflectance (not raw units).
      r[ch] = cal.white[ch] > 0.0f ? f.signal[ch] / cal.white[ch] : NAN;
    }
  }
  float mi = cal.hasWhite ? melaninIndex(r[AS7341_CHANNEL_630nm_F7]) : NAN;
  float ei = cal.hasWhite ? erythemaIndex(r[AS7341_CHANNEL_630nm_F7],
                                          r[AS7341_CHANNEL_555nm_F5])
                          : NAN;
  uint8_t flags = frameFlags(f, watch);

  if (format == Format::Json) {
    Serial.print(F("{\"type\":\"spectral\",\"t\":"));
    Serial.print(millis());
    Serial.print(',');
    jsonKey("gain"); jsonNum(f.gainX, 1); Serial.print(',');
    jsonKey("reflectance"); Serial.print(cal.hasWhite ? F("true") : F("false"));
    Serial.print(F(",\"bands\":{"));
    for (uint8_t b = 0; b < kNumBands; b++) {
      if (b) Serial.print(',');
      jsonKey(kBandNames[b]);
      jsonNum(r[kBands[b]], 6);
    }
    Serial.print(F("},"));
    jsonKey("melanin_idx"); jsonNum(mi, 2); Serial.print(',');
    jsonKey("erythema_idx"); jsonNum(ei, 2); Serial.print(',');
    jsonFlags(flags);
    Serial.println('}');
    return;
  }

  if (format == Format::Csv && headerPending) {
    Serial.print(F("t_ms,mode,led,gain"));
    for (uint8_t b = 0; b < kNumBands; b++) {
      Serial.print(',');
      Serial.print(kBandNames[b]);
    }
    Serial.println(F(",melanin_idx,erythema_idx,flags"));
    headerPending = false;
  }

  bool csv = format == Format::Csv;
  if (csv) {
    Serial.print(millis());
    Serial.print(F(",spectral,white,"));
    Serial.print(f.gainX, 1);
  } else {
    Serial.print(cal.hasWhite ? F("[spectral R] ") : F("[spectral raw] "));
  }
  for (uint8_t b = 0; b < kNumBands; b++) {
    Serial.print(csv ? F(",") : F(" "));
    if (!csv) {
      Serial.print(kBandNames[b]);
      Serial.print('=');
    }
    Serial.print(r[kBands[b]], 5);
  }
  Serial.print(csv ? F(",") : F("  MI="));
  Serial.print(mi, 2);
  Serial.print(csv ? F(",") : F(" EI="));
  Serial.print(ei, 2);
  Serial.print(csv ? F(",") : F("  ["));
  printFlags(flags);
  Serial.println(csv ? "" : "]");
}

static void runAi() {
  if (!cal.hasAiBaseline) {
    info(F("AI mode needs a baseline: wear the band on clean, normal skin and press a."));
    return;
  }
  MultiScan scan;
  uint8_t flags = 0;
  if (!captureMultiScan(scan, flags)) {
    info(F("Sensor read error."));
    return;
  }

  float x[kFeatureCount], probs[NeuralNet::kClasses];
  extractFeatures(scan, cal.aiBaseline, x);
  int best = net.predict(x, probs);
  float ood = net.outOfDistribution(x);
  bool contact = !(flags & FLAG_NO_CONTACT);
  // Only learn the wearer's normal from clean frames.
  bool clean = !(flags & (FLAG_NO_CONTACT | FLAG_SATURATED | FLAG_AMBIENT_LEAK));
  float nov = clean ? novelty.scoreAndUpdate(x) : novelty.score(x);
  if (!isnan(nov) && nov > NOVELTY_THRESHOLD) flags |= FLAG_NOVEL;
  if (ood > OOD_THRESHOLD) flags |= FLAG_OOD;
  const char *label = contact ? NeuralNet::className(best) : "no_contact";

  if (format == Format::Json) {
    Serial.print(F("{\"type\":\"ai\",\"t\":"));
    Serial.print(millis());
    Serial.print(',');
    jsonKey("label"); jsonStr(label); Serial.print(',');
    jsonKey("confidence"); jsonNum(probs[best], 4); Serial.print(',');
    Serial.print(F("\"probs\":{"));
    for (int i = 0; i < NeuralNet::kClasses; i++) {
      if (i) Serial.print(',');
      jsonKey(NeuralNet::className(i));
      jsonNum(probs[i], 4);
    }
    Serial.print(F("},"));
    jsonKey("novelty"); jsonNum(nov, 3); Serial.print(',');
    jsonKey("ood"); jsonNum(ood, 3); Serial.print(',');
    jsonKey("personalized"); Serial.print(net.isPersonalized() ? F("true") : F("false"));
    Serial.print(F(",\"features\":["));
    for (int i = 0; i < kFeatureCount; i++) {
      if (i) Serial.print(',');
      jsonNum(x[i], 5);
    }
    Serial.print(F("],\"white\":["));
    for (int b = 0; b < kScanBands; b++) {
      if (b) Serial.print(',');
      jsonNum(scan.white[b], 6);
    }
    Serial.print(F("],"));
    jsonKey("red680"); jsonNum(scan.red[7], 6); Serial.print(',');
    jsonKey("nir910"); jsonNum(scan.nir[9], 6); Serial.print(',');
    jsonFlags(flags);
    Serial.println('}');
    return;
  }

  if (format == Format::Csv) {
    // Columns match ml/train.py --real (add a `label` column when logging).
    if (headerPending) {
      Serial.print(F("t_ms,mode,prediction,confidence"));
      for (int i = 0; i < NeuralNet::kClasses; i++) {
        Serial.print(F(",p_"));
        Serial.print(NeuralNet::className(i));
      }
      Serial.print(F(",novelty,ood"));
      for (int i = 0; i < kFeatureCount; i++) {
        Serial.print(',');
        Serial.print(kFeatureNames[i]);
      }
      Serial.println(F(",flags"));
      headerPending = false;
    }
    Serial.print(millis());
    Serial.print(F(",ai,"));
    Serial.print(label);
    Serial.print(',');
    Serial.print(probs[best], 4);
    for (int i = 0; i < NeuralNet::kClasses; i++) {
      Serial.print(',');
      Serial.print(probs[i], 4);
    }
    Serial.print(',');
    Serial.print(nov, 3);
    Serial.print(',');
    Serial.print(ood, 3);
    for (int i = 0; i < kFeatureCount; i++) {
      Serial.print(',');
      Serial.print(x[i], 5);
    }
    Serial.print(',');
    printFlags(flags);
    Serial.println();
    return;
  }

  Serial.print(F("[ai] "));
  Serial.print(label);
  Serial.print(' ');
  Serial.print(probs[best] * 100.0f, 0);
  Serial.print(F("%  ("));
  for (int i = 0, shown = 0; i < NeuralNet::kClasses; i++) {
    if (i == best || probs[i] < 0.05f) continue;
    if (shown++) Serial.print(F(", "));
    Serial.print(NeuralNet::className(i));
    Serial.print(' ');
    Serial.print(probs[i] * 100.0f, 0);
    Serial.print('%');
  }
  Serial.print(F(")  novelty "));
  Serial.print(nov, 2);
  Serial.print(F("  ood "));
  Serial.print(ood, 2);
  Serial.print(F("  ["));
  printFlags(flags);
  Serial.println(']');
}

// ------------------------------------------------------------ Calibration --

// Average several good measurements on one channel. Rejects frames with no
// skin contact or a saturated sensor so a bad capture can't poison the model.
static bool captureAverage(LightSource src, as7341_color_channel_t ch, float &result) {
  float values[CALIBRATION_MEASUREMENTS];
  int n = 0;
  for (int i = 0; i < CALIBRATION_MEASUREMENTS * 2 && n < CALIBRATION_MEASUREMENTS; i++) {
    Frame f;
    if (!optics.measure(src, ch, f)) continue;
    if (frameFlags(f, ch) & (FLAG_NO_CONTACT | FLAG_SATURATED)) continue;
    values[n++] = f.signal[ch];
  }
  if (n < CALIBRATION_MEASUREMENTS / 2) {
    info(F("Calibration failed: check skin contact and re-seat the band."));
    return false;
  }
  result = trimmedMean(values, n);
  return true;
}

static void captureWhiteReference() {
  info(F("Capturing white reference. Hold a white card flat on the sensor..."));
  float sum[12] = {0};
  int n = 0;
  for (int i = 0; i < CALIBRATION_MEASUREMENTS * 2 && n < CALIBRATION_MEASUREMENTS; i++) {
    Frame f;
    if (!optics.measure(LightSource::OnboardWhite, AS7341_CHANNEL_CLEAR, f)) continue;
    // Like every capture: no target in front of the sensor (or saturation) is rejected.
    if (frameFlags(f, AS7341_CHANNEL_CLEAR) & (FLAG_NO_CONTACT | FLAG_SATURATED)) continue;
    for (uint8_t b = 0; b < kNumBands; b++) sum[kBands[b]] += f.signal[kBands[b]];
    n++;
  }
  if (n < CALIBRATION_MEASUREMENTS / 2) {
    info(F("White reference failed: hold a white card flat against the sensor."));
    return;
  }
  for (uint8_t b = 0; b < kNumBands; b++) cal.white[kBands[b]] = sum[kBands[b]] / n;
  cal.hasWhite = true;
  persist();
  info(F("White reference stored."));
}

// One reading under each light source: 850 nm, 660 nm, then white.
static bool captureMultiScan(MultiScan &scan, uint8_t &flags) {
  Frame f;
  flags = 0;
  if (!optics.measure(LightSource::Nir850, AS7341_CHANNEL_NIR, f)) return false;
  toBands(f, scan.nir);
  flags |= frameFlags(f, AS7341_CHANNEL_NIR);
  if (!optics.measure(LightSource::Red660, AS7341_CHANNEL_680nm_F8, f)) return false;
  toBands(f, scan.red);
  flags |= frameFlags(f, AS7341_CHANNEL_680nm_F8);
  if (!optics.measure(LightSource::OnboardWhite, AS7341_CHANNEL_CLEAR, f)) return false;
  toBands(f, scan.white);
  flags |= frameFlags(f, AS7341_CHANNEL_CLEAR);
  return true;
}

static void captureAiBaseline() {
  info(F("Capturing AI baseline on clean, normal skin. Hold still (~10 s)..."));
  MultiScan sum;
  memset(&sum, 0, sizeof(sum));
  int n = 0;
  for (int i = 0; i < AI_BASELINE_SCANS * 2 && n < AI_BASELINE_SCANS; i++) {
    MultiScan s;
    uint8_t flags;
    if (!captureMultiScan(s, flags)) continue;
    if (flags & (FLAG_NO_CONTACT | FLAG_SATURATED)) continue;
    for (int b = 0; b < kScanBands; b++) {
      sum.white[b] += s.white[b];
      sum.red[b] += s.red[b];
      sum.nir[b] += s.nir[b];
    }
    n++;
  }
  if (n < (AI_BASELINE_SCANS + 1) / 2) {
    info(F("AI baseline failed: check skin contact and re-seat the band."));
    return;
  }
  for (int b = 0; b < kScanBands; b++) {
    cal.aiBaseline.white[b] = sum.white[b] / n;
    cal.aiBaseline.red[b] = sum.red[b] / n;
    cal.aiBaseline.nir[b] = sum.nir[b] / n;
  }
  cal.hasAiBaseline = true;
  novelty.reset();
  persist();
  info(F("AI baseline stored."));
}

static void teach(int label) {
  if (!cal.hasAiBaseline) {
    info(F("Teach needs an AI baseline first (press a)."));
    return;
  }
  Serial.print(F("# Teaching '"));
  Serial.print(NeuralNet::className(label));
  Serial.println(F("'. Hold still..."));
  float fit = NAN;
  int added = 0;
  for (int i = 0; i < TEACH_SCANS; i++) {
    MultiScan s;
    uint8_t flags;
    if (!captureMultiScan(s, flags) || (flags & (FLAG_NO_CONTACT | FLAG_SATURATED))) continue;
    float x[kFeatureCount];
    extractFeatures(s, cal.aiBaseline, x);
    fit = net.personalize(x, label);
    added++;
  }
  if (!added) {
    info(F("Teach failed: no clean readings. Check skin contact."));
    return;
  }
  persist();
  Serial.print(F("# Learned "));
  Serial.print(added);
  Serial.print(F(" examples; fit on all taught examples: "));
  Serial.print(fit * 100.0f, 0);
  Serial.println('%');
}

// ------------------------------------------------------------ Persistence --

static void persist() {
  Persisted p;
  p.cal = cal;
  p.headValid = net.isPersonalized();
  p.modelId = NeuralNet::modelId();
  memcpy(p.headW, net.headW, sizeof(p.headW));
  memcpy(p.headB, net.headB, sizeof(p.headB));
  saveState(p);
}

static void restore() {
  Persisted p;
  if (!loadState(p)) return;
  cal = p.cal;
  hydrationLed = cal.hydrationLed;  // the LED the saved references were taken with
  if (p.headValid && p.modelId != NeuralNet::modelId()) {
    info(F("Network weights changed since you taught it: using factory weights."));
  } else if (p.headValid) {
    memcpy(net.headW, p.headW, sizeof(net.headW));
    memcpy(net.headB, p.headB, sizeof(net.headB));
    net.markPersonalized();
  }
  Serial.print(F("# Restored calibration from "));
  Serial.println(storageBackend());
}

// ---------------------------------------------------------------- Helpers --

static as7341_color_channel_t hydrationChannel() {
  // F8 (680 nm) is the closest filter to the 660 nm LED; the NIR photodiode
  // (broad, ~910 nm peak) is the only band that sees the 850 nm LED.
  return hydrationLed == LightSource::Red660 ? AS7341_CHANNEL_680nm_F8
                                             : AS7341_CHANNEL_NIR;
}

static const char *modeName(Mode m) {
  switch (m) {
    case Mode::Hydration:   return "hydration";
    case Mode::SurfaceScan: return "scan";
    case Mode::Spectral:    return "spectral";
    case Mode::Ai:          return "ai";
  }
  return "?";
}

static void info(const __FlashStringHelper *msg) {
  Serial.print(F("# "));
  Serial.println(msg);
}

static void toBands(const Frame &f, float *bands) {
  for (uint8_t b = 0; b < kNumBands; b++) bands[b] = f.signal[kBands[b]];
}

static uint8_t frameFlags(const Frame &f, as7341_color_channel_t watch) {
  uint8_t flags = 0;
  float lit = f.signal[watch] + f.ambient[watch];
  if (f.minDeltaRaw < MIN_DELTA_RAW_COUNTS) flags |= FLAG_NO_CONTACT;
  if (lit > 0.0f && f.ambient[watch] / lit > MAX_AMBIENT_FRACTION) flags |= FLAG_AMBIENT_LEAK;
  if (f.saturated) flags |= FLAG_SATURATED;
  return flags;
}

static const char *const kFlagNames[] = {"NO_CONTACT", "AMBIENT_LEAK", "SATURATED", "NOVEL", "OOD"};
static const uint8_t kNumFlags = 5;

static void printFlags(uint8_t flags) {
  if (!flags) {
    Serial.print(F("OK"));
    return;
  }
  bool first = true;
  for (uint8_t i = 0; i < kNumFlags; i++) {
    if (!(flags & (1 << i))) continue;
    if (!first) Serial.print('|');
    Serial.print(kFlagNames[i]);
    first = false;
  }
}

static void jsonKey(const char *key) {
  Serial.print('"');
  Serial.print(key);
  Serial.print(F("\":"));
}

static void jsonNum(float v, int digits) {
  if (isnan(v) || isinf(v)) {
    Serial.print(F("null"));
  } else {
    Serial.print(v, digits);
  }
}

static void jsonStr(const char *s) {
  Serial.print('"');
  Serial.print(s);
  Serial.print('"');
}

static void jsonFlags(uint8_t flags) {
  Serial.print(F("\"flags\":["));
  bool first = true;
  for (uint8_t i = 0; i < kNumFlags; i++) {
    if (!(flags & (1 << i))) continue;
    if (!first) Serial.print(',');
    jsonStr(kFlagNames[i]);
    first = false;
  }
  Serial.print(']');
}
