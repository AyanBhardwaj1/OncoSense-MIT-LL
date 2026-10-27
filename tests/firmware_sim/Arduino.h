// Host stand-in for the Arduino core, used to run the real firmware on a
// laptop. Time is virtual: delay() and sensor reads advance the clock.
#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <string>

class __FlashStringHelper;
#define F(s) (reinterpret_cast<const __FlashStringHelper *>(s))

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define A1 15
#define A2 16

namespace sim {
extern unsigned long nowMs;
extern int pinState[64];
extern long delayBudget;  // throws Halt when exhausted (stops infinite loops)
struct Halt {};
void advance(unsigned long ms);
}  // namespace sim

inline unsigned long millis() { return sim::nowMs; }
inline void delay(unsigned long ms) { sim::advance(ms); }
inline void pinMode(int, int) {}
inline void digitalWrite(int pin, int v) { sim::pinState[pin & 63] = v; }

class Print {
 public:
  virtual ~Print() {}
  virtual void put(const char *s) = 0;

  size_t print(const __FlashStringHelper *s) { return print(reinterpret_cast<const char *>(s)); }
  size_t print(const char *s) { put(s); return strlen(s); }
  size_t print(char c) { char b[2] = {c, 0}; put(b); return 1; }
  size_t print(int v) { return printf_("%d", v); }
  size_t print(unsigned int v) { return printf_("%u", v); }
  size_t print(long v) { return printf_("%ld", v); }
  size_t print(unsigned long v) { return printf_("%lu", v); }
  size_t print(double v, int digits = 2) { return printFloat(v, digits); }

  size_t println() { put("\r\n"); return 2; }
  template <class T> size_t println(T v) { size_t n = print(v); return n + println(); }
  size_t println(double v, int digits) { size_t n = print(v, digits); return n + println(); }

 private:
  size_t printf_(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
  // Mirrors Arduino's Print::printFloat, including its "nan"/"inf"/"ovf" output.
  size_t printFloat(double number, uint8_t digits);
};

class HardwareSerialSim : public Print {
 public:
  void begin(long) {}
  explicit operator bool() const { return true; }
  int available() { return (int)(input.size() - readPos); }
  int read() { return readPos < input.size() ? (unsigned char)input[readPos++] : -1; }
  void put(const char *s) override { output += s; }

  std::string input;
  size_t readPos = 0;
  std::string output;
};

extern HardwareSerialSim Serial;
