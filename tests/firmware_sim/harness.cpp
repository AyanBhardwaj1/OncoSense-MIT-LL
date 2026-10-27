// Firmware-in-the-loop harness: runs the real OncoSense firmware
// (firmware/OncoSense/OncoSense.ino and its modules, unmodified) against the
// simulated Arduino core, AS7341 sensor and flash in this folder.
//
// It reads a scenario script on stdin, one command per line:
//   boot                      call setup()
//   send <chars>              type characters into the serial port
//   run <ms>                  run loop() for <ms> of virtual time
//   scene contact|present|fail <0|1>
//   scene noise <relative>
//   scene ambient <v>         same ambient level (basic counts) on every channel
//   scene src <0|1|2> <12 values>   per-channel return for NIR / red / white LED
//   stats                     print "@@ stats ..." counters
// Firmware serial output goes to stdout as it is produced. Harness lines
// start with "@@ ". Test driver: tests/firmware_sim/test_firmware.py.

#include <stdarg.h>

#include <iostream>
#include <sstream>

#include "Arduino.h"
#include "OncoSense.ino"

// ------------------------------------------------------------ sim state --

namespace sim {
unsigned long nowMs = 0;
int pinState[64] = {0};
long delayBudget = 2000000;
Scene scene;
int gainSets = 0;
int flashWrites = 0;
static uint32_t rngState = 12345;

void advance(unsigned long ms) {
  nowMs += ms;
  delayBudget -= (long)ms;
  if (delayBudget < 0) throw Halt();
}

static float uniform() {
  rngState = rngState * 1664525u + 1013904223u;
  return (rngState >> 8) / 16777216.0f;
}
}  // namespace sim

HardwareSerialSim Serial;
TwoWire Wire;

// ---------------------------------------------------------- Print impl --

size_t Print::printf_(const char *fmt, ...) {
  char buf[64];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  put(buf);
  return n > 0 ? (size_t)n : 0;
}

size_t Print::printFloat(double number, uint8_t digits) {
  if (isnan(number)) return print("nan");
  if (isinf(number)) return print("inf");
  if (number > 4294967040.0 || number < -4294967040.0) return print("ovf");
  size_t n = 0;
  if (number < 0.0) {
    n += print('-');
    number = -number;
  }
  double rounding = 0.5;
  for (uint8_t i = 0; i < digits; ++i) rounding /= 10.0;
  number += rounding;
  unsigned long whole = (unsigned long)number;
  double remainder = number - (double)whole;
  n += print(whole);
  if (digits > 0) n += print('.');
  while (digits-- > 0) {
    remainder *= 10.0;
    unsigned int d = (unsigned int)remainder;
    n += print(d);
    remainder -= d;
  }
  return n;
}

// -------------------------------------------------------- sensor model --

bool Adafruit_AS7341::begin(uint8_t, TwoWire *, int32_t) { return sim::scene.present; }

bool Adafruit_AS7341::readAllChannels(uint16_t *buf) {
  uint32_t steps = (uint32_t)(atime_ + 1) * (uint32_t)(astep_ + 1);
  float tintMs = steps * 0.00278f;
  sim::advance((unsigned long)(2 * tintMs) + 2);  // two SMUX passes
  if (sim::scene.readsFail) return false;

  int src = -1;
  if (sim::pinState[A1]) src = 0;
  else if (sim::pinState[A2]) src = 1;
  else if (onboardLed_) src = 2;

  float gainX = gain_ == AS7341_GAIN_0_5X ? 0.5f : (float)(1UL << ((int)gain_ - 1));
  float fullScale = steps > 65535 ? 65535.0f : (float)steps;
  for (int ch = 0; ch < 12; ch++) {
    float basic = sim::scene.ambient[ch];
    if (src >= 0 && sim::scene.contact) basic += sim::scene.source[src][ch];
    float noise = 1.0f + sim::scene.noise * (2.0f * sim::uniform() - 1.0f);
    float counts = basic * noise * gainX * tintMs;
    buf[ch] = (uint16_t)(counts < 0 ? 0 : counts > fullScale ? fullScale : counts + 0.5f);
  }
  return true;
}

// --------------------------------------------------------- interpreter --

static void flush() {
  std::cout << Serial.output;
  Serial.output.clear();
  std::cout.flush();
}

int main() {
  std::string line;
  while (std::getline(std::cin, line)) {
    std::istringstream in(line);
    std::string cmd;
    in >> cmd;
    try {
      if (cmd == "boot") {
        setup();
      } else if (cmd == "send") {
        std::string chars;
        in >> chars;
        Serial.input += chars;
      } else if (cmd == "run") {
        unsigned long ms = 0;
        in >> ms;
        unsigned long end = sim::nowMs + ms;
        while (sim::nowMs < end) {
          loop();
          sim::advance(1);
        }
      } else if (cmd == "scene") {
        std::string what;
        in >> what;
        if (what == "contact") in >> sim::scene.contact;
        else if (what == "present") in >> sim::scene.present;
        else if (what == "fail") in >> sim::scene.readsFail;
        else if (what == "noise") in >> sim::scene.noise;
        else if (what == "ambient") {
          float v;
          in >> v;
          for (float &a : sim::scene.ambient) a = v;
        } else if (what == "src") {
          int s;
          in >> s;
          for (int ch = 0; ch < 12; ch++) in >> sim::scene.source[s][ch];
        }
      } else if (cmd == "stats") {
        flush();
        std::cout << "@@ stats t=" << sim::nowMs << " gainSets=" << sim::gainSets
                  << " flashWrites=" << sim::flashWrites << "\n";
      }
    } catch (sim::Halt &) {
      flush();
      std::cout << "@@ halted\n";
      return 0;
    }
    flush();
  }
  return 0;
}
