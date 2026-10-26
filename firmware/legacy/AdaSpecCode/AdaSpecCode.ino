#include "Wire.h"
#include "Adafruit_AS7341.h"

#define IR_LED_PIN  A1   // your 850 nm LED
Adafruit_AS7341 spectro;

// how many samples to build/calibrate and then to read each cycle
const int CALIB_SAMPLES = 15;
const int READ_SAMPLES  = 10;

// these will be set in setup()  
float baselineNIR;  
float threshVaseline;  
float threshRash;  
float threshScar;  

void setup() {
  Wire.begin(4,5);
  Serial.begin(115200);
  pinMode(IR_LED_PIN, OUTPUT);

  // init sensor  
  if (!spectro.begin()) {
    Serial.println("ERROR: AS7341 not found");
    while (1);
  }

  // ——— calibrate on clean, dry skin ———  
  Serial.println("Calibrating baseline (clean skin) ...");
  digitalWrite(IR_LED_PIN, HIGH);
  delay(1000);

  long sum = 0;
  for (int i = 0; i < CALIB_SAMPLES; i++) {
    spectro.readAllChannels();
    sum += spectro.getChannel(AS7341_CHANNEL_NIR);
    delay(150);
  }
  digitalWrite(IR_LED_PIN, LOW);

  baselineNIR   = sum / float(CALIB_SAMPLES);
  // auto thresholds:
  threshVaseline = baselineNIR * 1.20;  // +20% → Vaseline/oil
  threshRash     = baselineNIR * 0.90;  // –10% → rash/acne
  threshScar     = baselineNIR * 0.75;  // –25% → scar/dry lesion

  Serial.print("Baseline NIR: "); Serial.println(baselineNIR);
  Serial.print("Vaseline ≥ ");  Serial.println(threshVaseline);
  Serial.print("Rash  ≤ ");     Serial.println(threshRash);
  Serial.print("Scar  ≤ ");     Serial.println(threshScar);
  Serial.println("Setup done. Starting detection...");
}

void loop() {
  // turn on LED and settle
  digitalWrite(IR_LED_PIN, HIGH);
  delay(500);

  // read & average
  long sum = 0;
  for (int i = 0; i < READ_SAMPLES; i++) {
    spectro.readAllChannels();
    sum += spectro.getChannel(AS7341_CHANNEL_NIR);
    delay(100);
  }
  digitalWrite(IR_LED_PIN, LOW);

  float current = sum / float(READ_SAMPLES);
  Serial.print("NIR reflectance: "); Serial.println(current);

  // classify
  if (current >= threshVaseline) {
    Serial.println(">> Vaseline (oil) detected");
  } 
  else if (current <= threshScar) {
    Serial.println(">> Scar/dry lesion detected");
  } 
  else if (current <= threshRash) {
    Serial.println(">> Rash/acne detected");
  } 
  else {
    Serial.println(">> Skin normal");
  }
  Serial.println();

  delay(1000);
}
