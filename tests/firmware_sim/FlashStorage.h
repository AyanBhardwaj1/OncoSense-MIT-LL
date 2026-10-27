// Simulated FlashStorage: persists to the file named by $ONCOSENSE_FLASH so
// a second run of the harness behaves like a reboot.
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace sim {
extern int flashWrites;
}

template <class T>
class FlashStorageClass {
 public:
  explicit FlashStorageClass(const void *) {}
  T read() {
    T value;
    memset(&value, 0xFF, sizeof(value));  // erased flash reads as 0xFF
    const char *path = getenv("ONCOSENSE_FLASH");
    if (path) {
      FILE *f = fopen(path, "rb");
      if (f) {
        if (fread(&value, sizeof(value), 1, f) != 1) memset(&value, 0xFF, sizeof(value));
        fclose(f);
      }
    }
    return value;
  }
  void write(const T &value) {
    sim::flashWrites++;
    const char *path = getenv("ONCOSENSE_FLASH");
    if (!path) return;
    FILE *f = fopen(path, "wb");
    if (f) {
      fwrite(&value, sizeof(value), 1, f);
      fclose(f);
    }
  }
};

#define FlashStorage(name, T) \
  static const char name##_area[1] = {0}; \
  FlashStorageClass<T> name(name##_area);
