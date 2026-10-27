#include "Storage.h"

#include <string.h>

namespace oncosense {

#if defined(ARDUINO_ARCH_SAMD) || defined(ARDUINO_ARCH_ESP32)
static const uint32_t kMagic = 0x4F4E4353;  // "ONCS"
static const uint16_t kVersion = 3;

static bool valid(const Persisted &p) { return p.magic == kMagic && p.version == kVersion; }
#endif

}  // namespace oncosense

#if defined(ARDUINO_ARCH_SAMD)
#include <FlashStorage.h>
FlashStorage(gStateFlash, oncosense::Persisted);

namespace oncosense {
const char *storageBackend() { return "flash"; }
bool loadState(Persisted &out) {
  out = gStateFlash.read();
  return valid(out);
}
bool saveState(const Persisted &in) {
  Persisted p = in;
  p.magic = kMagic;
  p.version = kVersion;
  gStateFlash.write(p);
  return true;
}
void eraseState() {
  Persisted p;
  memset(&p, 0, sizeof(p));
  gStateFlash.write(p);
}
}  // namespace oncosense

#elif defined(ARDUINO_ARCH_ESP32)
#include <Preferences.h>

namespace oncosense {
static Preferences prefs;
const char *storageBackend() { return "nvs"; }
bool loadState(Persisted &out) {
  prefs.begin("oncosense", true);
  size_t n = prefs.getBytes("state", &out, sizeof(out));
  prefs.end();
  return n == sizeof(out) && valid(out);
}
bool saveState(const Persisted &in) {
  Persisted p = in;
  p.magic = kMagic;
  p.version = kVersion;
  prefs.begin("oncosense", false);
  size_t n = prefs.putBytes("state", &p, sizeof(p));
  prefs.end();
  return n == sizeof(p);
}
void eraseState() {
  prefs.begin("oncosense", false);
  prefs.clear();
  prefs.end();
}
}  // namespace oncosense

#else
namespace oncosense {
const char *storageBackend() { return "ram"; }
bool loadState(Persisted &) { return false; }
bool saveState(const Persisted &) { return false; }
void eraseState() {}
}  // namespace oncosense
#endif
