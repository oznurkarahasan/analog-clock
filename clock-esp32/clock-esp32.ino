#include <Wire.h>
#include <RTClib.h>
#include <Adafruit_NeoPixel.h>

#define LED_PIN     4
#define NUM_LEDS    60   
#define BRIGHTNESS  50  

RTC_DS3231 rtc;
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
  Serial.begin(115200);
  if (!rtc.begin()) {
    while (1);
  }
  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.show(); 
}

void loop() {
  DateTime now = rtc.now();
  updateClockDisplay(now);
  delay(50);
}

void updateClockDisplay(DateTime t) {
  strip.clear();

  int hr = t.hour() % 12;
  int mn = t.minute();
  int sc = t.second();
  int hrPos = (hr * 5);

  for (int i = 0; i <= mn; i++) {
    strip.setPixelColor(i, strip.Color(5, 0, 15));
  }

  for (int i = 0; i < 60; i += 5) {
    strip.setPixelColor(i, strip.Color(0, 200, 0)); 
  }

  strip.setPixelColor(mn, strip.Color(60, 0, 60)); 
  strip.setPixelColor(hrPos, strip.Color(200, 200, 0));
  strip.setPixelColor(sc, Wheel((sc * 4) & 255)); 
  strip.show();
}

uint32_t Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if(WheelPos < 85) {
    return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  }
  if(WheelPos < 170) {
    WheelPos -= 85;
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}
