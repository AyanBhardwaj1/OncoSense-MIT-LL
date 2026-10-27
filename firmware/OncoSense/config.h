// =============================================================================
//  OncoSense Band — build-time configuration
//  Edit this file to match your wiring. Everything tunable lives here.
// =============================================================================
#pragma once

#include <Adafruit_AS7341.h>

// ---------------------------------------------------------------- Firmware --
#define ONCOSENSE_FW_VERSION "3.0.0"

// ------------------------------------------------------------------- Board --
// Reference build: Adafruit QT Py SAMD21 (M0) + Adafruit AS7341 over STEMMA QT.
//
// On QT Py SAMD21 / ESP32-C3 the STEMMA QT port is `Wire`.
// On QT Py RP2040 / ESP32-S2 / ESP32-S3 the STEMMA QT port is `Wire1`.
#define SENSOR_WIRE Wire

// Early bench bring-up used a dev board with the sensor on GPIO4 (SDA) and
// GPIO5 (SCL). Set to 1 on ESP32 boards that need explicit I2C pins.
#define USE_CUSTOM_I2C_PINS 0
#define CUSTOM_SDA_PIN 4
#define CUSTOM_SCL_PIN 5

#define SERIAL_BAUD 115200
// Wait at most this long for a USB serial monitor, then run anyway.
// (The v1 code used `while (!Serial);`, which froze the band on battery power.)
#define SERIAL_WAIT_MS 3000

// --------------------------------------------------------------- Emitters --
// Each LED is driven straight from a GPIO through a series resistor.
// Keep the LED current inside the MCU pin limit (~7 mA on SAMD21), or switch
// the LED through a small NPN/MOSFET for more optical power.
#define PIN_LED_NIR A1  // 850 nm near-infrared LED (EPIGAP OSA star module)
#define PIN_LED_RED A2  // 660 nm red LED

// The AS7341 breakout also has an on-board white LED, driven by the sensor
// itself. It is used for the broadband (visible) spectral mode.
#define ONBOARD_LED_MA 10  // 4 .. 258 mA, keep low: it sits against skin

// --------------------------------------------------------- Sensor timing --
// Integration time = (ATIME + 1) * (ASTEP + 1) * 2.78 us
// 29 / 599  ->  50 ms per integration, full scale = 18 000 counts.
// (v1 used 100 / 999 -> 281 ms and silently saturated above 65 535 counts.)
#define AS_ATIME 29
#define AS_ASTEP 599
#define AS_GAIN_START AS7341_GAIN_64X
#define AUTO_GAIN 1

// One measurement = this many (LED off, LED on) pairs, trimmed-mean averaged.
#define PAIRS_PER_MEASUREMENT 4
#define LED_SETTLE_MS 15
#define MEASUREMENT_PERIOD_MS 1000

// Captures for baseline / reference calibration average this many measurements.
#define CALIBRATION_MEASUREMENTS 8

// -------------------------------------------------------- Quality checks --
// LED-on minus LED-off must exceed this many raw counts, otherwise the optical
// head is not touching skin (or the LED is disconnected).
#define MIN_DELTA_RAW_COUNTS 40
// If ambient light is more than this fraction of the LED-on reading, the band
// is letting room light leak in under the optical head.
#define MAX_AMBIENT_FRACTION 0.50f

// ------------------------------------------------------------- AI mode --
// Scans averaged when capturing the wearer's AI baseline (command `a`).
#define AI_BASELINE_SCANS 4
// Scans recorded as labelled examples when teaching the network (`t` + 0-4).
#define TEACH_SCANS 3
// A pending `t` waits this long for its class number, then is cancelled, so a
// later mode key (1-4) can't be mistaken for a training label.
#define TEACH_TIMEOUT_MS 5000UL
// RMS z-score above which a reading is reported as unusual for this wearer.
#define NOVELTY_THRESHOLD 3.0f
// RMS z-score above which a reading is outside the model's training data.
#define OOD_THRESHOLD 3.0f
