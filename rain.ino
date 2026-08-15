// Tipping bucket rain gauge — read tips, print to serial.
// Wiring: black->GND, red->3V3, yellow->D5   (ESP32: use pin 27)

#define RAIN_PIN D5

// mm of rain per bucket tip.
// Vendor label says 0.70, but their own numbers give 0.735:
// (100mL / 70 tips) / 19.44cm2 * 10 = 0.735
const float MM_PER_TIP = 0.735;

volatile unsigned long tips = 0;      // volatile: changed inside the ISR
volatile unsigned long lastTip = 0;

// Runs the instant the bucket tips. IRAM_ATTR keeps it in RAM (required on ESP).
void IRAM_ATTR onTip() {
  // Bucket rocks as it settles and fires extra edges. Ignore anything
  // within 250ms of the last accepted tip.
  if (millis() - lastTip < 250) return;
  lastTip = millis();
  tips++;
}

unsigned long shown = 0;              // last count we printed

void setup() {
  Serial.begin(115200);

  // Sensor output is open-collector: it pulls the pin LOW, so we need
  // the pin held HIGH by default.
  pinMode(RAIN_PIN, INPUT_PULLUP);

  // Interrupt, not polling — a tip pulse is only a few ms long and
  // any delay() in loop() would make you miss most of them.
  attachInterrupt(digitalPinToInterrupt(RAIN_PIN), onTip, FALLING);
}

void loop() {
  // Only print when the count actually changes.
  if (tips != shown) {
    shown = tips;
    Serial.print(shown);
    Serial.print(" tips  ");
    Serial.print(shown * MM_PER_TIP, 2);
    Serial.println(" mm");
  }
}
