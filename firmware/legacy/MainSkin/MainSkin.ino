#include <Adafruit_AS7341.h>

#define IR2_LED_PIN   A2        // your 660 nm LED
Adafruit_AS7341    as7341;

// ———— Thresholds (tune these!) ————
const uint16_t THRESH_VERY_DRY  = 150;   // below this = Very Dry
const uint16_t THRESH_DRY       = 350;   // 150–349 = Dry
const uint16_t THRESH_HEALTHY   = 700;   // 350–699 = Healthy
// 700+ = Vaseline-like

void setup() {
  Serial.begin(9600);
  while (!Serial);

  // turn on 660 nm LED
  pinMode(IR2_LED_PIN, OUTPUT);
  digitalWrite(IR2_LED_PIN, HIGH);

  // init spectrometer
  if (!as7341.begin()) {
    Serial.println("AS7341 not detected.");
    while (1);
  }
  as7341.setATIME(100);
  as7341.setASTEP(999);
  as7341.setGain(AS7341_GAIN_256X);

  Serial.println("Skin State Detector Ready");
}

void loop() {
  // read all channels
  if (!as7341.readAllChannels()) {
    Serial.println("Read error");
    delay(500);
    return;
  }

  // F8 is 680 nm – closest to your 660 nm LED
  uint16_t refl660 = as7341.getChannel(AS7341_CHANNEL_680nm_F8);

  // classification
  const char* state;
  if      (refl660 <  THRESH_VERY_DRY) state = "Very Dry";
  else if (refl660 <  THRESH_DRY)      state = "Dry";
  else if (refl660 <  THRESH_HEALTHY)  state = "Healthy";
  else                                 state = "Vaseline-like";

  // output
  Serial.print("660 nm refl: ");
  Serial.print(refl660);
  Serial.print("  →  ");
  Serial.println(state);

  delay(500);
}

