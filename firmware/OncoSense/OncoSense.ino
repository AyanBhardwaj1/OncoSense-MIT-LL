// =============================================================================
//  OncoSense Band — firmware v2
//  Wearable optical skin sensing: hydration, surface changes and pigment.
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
//
//  This is a research prototype, not a medical device. It does not diagnose
//  skin cancer or any other condition.
// =============================================================================

#include <Wire.h>
#include <Adafruit_AS7341.h>

#include "config.h"
#include "Optics.h"
#include "SkinModel.h"

using namespace oncosense;

// ------------------------------------------------------------------ State --

enum class Mode : uint8_t { Hydration = 1, SurfaceScan = 2, Spectral = 3 };

struct Calibration {
  // Hydration (two-point), tied to the LED it was captured with.
  float dryRef = NAN;
  float hydratedRef = NAN;
  LightSource hydrationLed = LightSource::Red660;
  // Surface scan.
  float nirBaseline = NAN;
  // Spectral: white reference card, per AS7341 channel.
  float white[12];
  bool hasWhite = false;
};

static Optics optics;
static Calibration cal;
static Mode mode = Mode::Hydration;
static LightSource hydrationLed = LightSource::Red660;
static bool csvOutput = false;
static bool streaming = true;
static bool headerPending = true;
static Ema trend(0.3f);
static unsigned long lastMeasurementMs = 0;

// ------------------------------------------------------------- Prototypes --

static void printHelp();
static void printStatus();
static void handleCommand(char c);
static void setMode(Mode m);
static void runMeasurement();
static void runHydration();
static void runSurfaceScan();
static void runSpectral();
static bool captureAverage(LightSource src, as7341_color_channel_t ch, float &result);
static void captureWhiteReference();
static as7341_color_channel_t hydrationChannel();
static const char *modeName(Mode m);
static void info(const __FlashStringHelper *msg);
static void printFlags(const Frame &f, as7341_color_channel_t watch);
static bool hasContact(const Frame &f);

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
  printHelp();
}

// ------------------------------------------------------------------- Loop --

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c != '\r' && c != '\n' && c != ' ') handleCommand(c);
  }

  if (streaming && millis() - lastMeasurementMs >= MEASUREMENT_PERIOD_MS) {
    lastMeasurementMs = millis();
    runMeasurement();
  }
}

// --------------------------------------------------------------- Commands --

static void printHelp() {
  Serial.println(F("#"));
  Serial.println(F("# Commands:"));
  Serial.println(F("#   1  hydration mode        2  surface-scan mode     3  spectral mode"));
  Serial.println(F("#   l  toggle hydration LED (660 nm <-> 850 nm)"));
  Serial.println(F("#   d  capture DRY reference          (hydration)"));
  Serial.println(F("#   m  capture MOISTURIZED reference  (hydration)"));
  Serial.println(F("#   b  capture clean-skin baseline    (surface scan)"));
  Serial.println(F("#   w  capture WHITE reference card   (spectral)"));
  Serial.println(F("#   x  clear all calibration"));
  Serial.println(F("#   c  toggle CSV / readable output   p  pause / resume"));
  Serial.println(F("#   s  status                         h  this help"));
  Serial.println(F("#"));
}

static void handleCommand(char c) {
  float value;
  switch (c) {
    case '1': setMode(Mode::Hydration); break;
    case '2': setMode(Mode::SurfaceScan); break;
    case '3': setMode(Mode::Spectral); break;

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
        Serial.println(value, 4);
        if (!isnan(cal.dryRef) && !isnan(cal.hydratedRef) &&
            !referencesSeparable(cal.dryRef, cal.hydratedRef)) {
          info(F("Dry and moisturized references are too close (< 3 % apart) to score between."));
          info(F("Recapture after a bigger change in the skin."));
        }
        trend.reset();
      }
      break;

    case 'b':
      info(F("Capturing clean-skin NIR baseline. Hold still..."));
      if (captureAverage(LightSource::Nir850, AS7341_CHANNEL_NIR, value)) {
        cal.nirBaseline = value;
        Serial.print(F("# Baseline = "));
        Serial.println(value, 4);
        trend.reset();
      }
      break;

    case 'w':
      captureWhiteReference();
      break;

    case 'x':
      cal = Calibration();
      trend.reset();
      info(F("Calibration cleared."));
      break;

    case 'c':
      csvOutput = !csvOutput;
      headerPending = true;
      break;

    case 'p':
      streaming = !streaming;
      info(streaming ? F("Streaming resumed.") : F("Streaming paused."));
      break;

    case 's': printStatus(); break;
    case 'h':
    case '?': printHelp(); break;
    default:
      Serial.print(F("# Unknown command '"));
      Serial.print(c);
      Serial.println(F("'. Type h for help."));
  }
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
}

static void printStatus() {
  Serial.print(F("# Mode: "));
  Serial.print(modeName(mode));
  Serial.print(F(" | hydration LED: "));
  Serial.print(toString(hydrationLed));
  Serial.print(F(" | gain: "));
  Serial.print(optics.gainX(), 1);
  Serial.println(F("x"));
  Serial.print(F("# Dry ref: "));
  Serial.print(cal.dryRef, 4);
  Serial.print(F(" | moisturized ref: "));
  Serial.print(cal.hydratedRef, 4);
  Serial.print(F(" (LED "));
  Serial.print(toString(cal.hydrationLed));
  Serial.println(F(")"));
  Serial.print(F("# NIR baseline: "));
  Serial.print(cal.nirBaseline, 4);
  Serial.print(F(" | white reference: "));
  Serial.println(cal.hasWhite ? F("yes") : F("no"));
}

// ---------------------------------------------------------- Measurements --

static void runMeasurement() {
  switch (mode) {
    case Mode::Hydration:   runHydration(); break;
    case Mode::SurfaceScan: runSurfaceScan(); break;
    case Mode::Spectral:    runSpectral(); break;
  }
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
  bool contact = hasContact(f);
  float smooth = contact ? trend.update(metric) : trend.value();

  if (csvOutput) {
    if (headerPending) {
      Serial.println(F("t_ms,mode,led,gain,signal,ambient,metric,trend,state,flags"));
      headerPending = false;
    }
    Serial.print(millis());              Serial.print(',');
    Serial.print(F("hydration,"));
    Serial.print(toString(hydrationLed)); Serial.print(',');
    Serial.print(f.gainX, 1);            Serial.print(',');
    Serial.print(signal, 5);             Serial.print(',');
    Serial.print(f.ambient[ch], 5);      Serial.print(',');
    Serial.print(metric, 2);             Serial.print(',');
    Serial.print(smooth, 2);             Serial.print(',');
    Serial.print(contact ? toString(state) : "No contact"); Serial.print(',');
    printFlags(f, ch);
    Serial.println();
    return;
  }

  Serial.print(F("[hydration "));
  Serial.print(toString(hydrationLed));
  Serial.print(F("] signal "));
  Serial.print(signal, 4);
  if (calibrated) {
    Serial.print(F("  score "));
    Serial.print(metric, 1);
    Serial.print(F(" (trend "));
    Serial.print(smooth, 1);
    Serial.print(')');
  } else {
    Serial.print(F("  v1-equiv "));
    Serial.print(metric, 0);
  }
  Serial.print(F("  ->  "));
  Serial.print(contact ? toString(state) : "No contact");
  if (!calibrated) Serial.print(F("  (uncalibrated: press d / m)"));
  Serial.print(F("  ["));
  printFlags(f, ch);
  Serial.println(']');
}

static void runSurfaceScan() {
  const as7341_color_channel_t ch = AS7341_CHANNEL_NIR;
  Frame f;
  if (!optics.measure(LightSource::Nir850, ch, f)) {
    info(F("Sensor read error."));
    return;
  }

  float signal = f.signal[ch];
  float ratio = cal.nirBaseline > 0.0f ? signal / cal.nirBaseline : NAN;
  SurfaceState state = classifySurface(signal, cal.nirBaseline);
  bool contact = hasContact(f);
  float smooth = contact ? trend.update(ratio) : trend.value();

  if (csvOutput) {
    if (headerPending) {
      Serial.println(F("t_ms,mode,led,gain,signal,ambient,metric,trend,state,flags"));
      headerPending = false;
    }
    Serial.print(millis());         Serial.print(',');
    Serial.print(F("scan,850nm,"));
    Serial.print(f.gainX, 1);       Serial.print(',');
    Serial.print(signal, 5);        Serial.print(',');
    Serial.print(f.ambient[ch], 5); Serial.print(',');
    Serial.print(ratio, 4);         Serial.print(',');
    Serial.print(smooth, 4);        Serial.print(',');
    Serial.print(contact ? toString(state) : "No contact"); Serial.print(',');
    printFlags(f, ch);
    Serial.println();
    return;
  }

  Serial.print(F("[scan 850nm] NIR "));
  Serial.print(signal, 4);
  if (!isnan(ratio)) {
    Serial.print(F("  vs baseline "));
    Serial.print((ratio - 1.0f) * 100.0f, 1);
    Serial.print('%');
  }
  Serial.print(F("  ->  "));
  if (!contact) {
    Serial.print(F("No contact"));
  } else if (isnan(ratio)) {
    Serial.print(F("No baseline (press b on clean skin)"));
  } else {
    Serial.print(toString(state));
  }
  Serial.print(F("  ["));
  printFlags(f, ch);
  Serial.println(']');
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

  if (csvOutput && headerPending) {
    Serial.print(F("t_ms,mode,led,gain"));
    for (uint8_t b = 0; b < kNumBands; b++) {
      Serial.print(',');
      Serial.print(kBandNames[b]);
    }
    Serial.println(F(",melanin_idx,erythema_idx,flags"));
    headerPending = false;
  }

  if (csvOutput) {
    Serial.print(millis());
    Serial.print(F(",spectral,white,"));
    Serial.print(f.gainX, 1);
  } else {
    Serial.print(cal.hasWhite ? F("[spectral R] ") : F("[spectral raw] "));
  }
  for (uint8_t b = 0; b < kNumBands; b++) {
    Serial.print(csvOutput ? F(",") : F(" "));
    if (!csvOutput) {
      Serial.print(kBandNames[b]);
      Serial.print('=');
    }
    Serial.print(r[kBands[b]], 4);
  }
  if (csvOutput) {
    Serial.print(',');
    Serial.print(mi, 2);
    Serial.print(',');
    Serial.print(ei, 2);
    Serial.print(',');
  } else {
    Serial.print(F("  MI="));
    Serial.print(mi, 1);
    Serial.print(F(" EI="));
    Serial.print(ei, 1);
    Serial.print(F("  ["));
  }
  printFlags(f, watch);
  Serial.println(csvOutput ? "" : "]");
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
    if (!hasContact(f) || f.saturated) continue;
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
    if (!hasContact(f) || f.saturated) continue;
    for (uint8_t b = 0; b < kNumBands; b++) sum[kBands[b]] += f.signal[kBands[b]];
    n++;
  }
  if (n < CALIBRATION_MEASUREMENTS / 2) {
    info(F("White reference failed: hold a white card flat against the sensor."));
    return;
  }
  for (uint8_t b = 0; b < kNumBands; b++) cal.white[kBands[b]] = sum[kBands[b]] / n;
  cal.hasWhite = true;
  info(F("White reference stored."));
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
    case Mode::SurfaceScan: return "surface scan";
    case Mode::Spectral:    return "spectral";
  }
  return "?";
}

static void info(const __FlashStringHelper *msg) {
  Serial.print(F("# "));
  Serial.println(msg);
}

static bool hasContact(const Frame &f) { return f.minDeltaRaw >= MIN_DELTA_RAW_COUNTS; }

static void printFlags(const Frame &f, as7341_color_channel_t watch) {
  bool any = false;
  float lit = f.signal[watch] + f.ambient[watch];
  if (!hasContact(f)) {
    Serial.print(F("NO_CONTACT"));
    any = true;
  }
  if (lit > 0.0f && f.ambient[watch] / lit > MAX_AMBIENT_FRACTION) {
    if (any) Serial.print('|');
    Serial.print(F("AMBIENT_LEAK"));
    any = true;
  }
  if (f.saturated) {
    if (any) Serial.print('|');
    Serial.print(F("SATURATED"));
    any = true;
  }
  if (!any) Serial.print(F("OK"));
}
