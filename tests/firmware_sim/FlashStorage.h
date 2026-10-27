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
    // Erased flash reads as 0xFF. Work on raw bytes, then copy into the
    // (trivially copyable) struct, which is how the real library behaves.
    unsigned char bytes[sizeof(T)];
    memset(bytes, 0xFF, sizeof(bytes));
    const char *path = getenv("ONCOSENSE_FLASH");
    if (path) {
      FILE *f = fopen(path, "rb");
      if (f) {
        if (fread(bytes, sizeof(bytes), 1, f) != 1) memset(bytes, 0xFF, sizeof(bytes));
        fclose(f);
      }
    }
    T value;
    memcpy(static_cast<void *>(&value), bytes, sizeof(value));
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
