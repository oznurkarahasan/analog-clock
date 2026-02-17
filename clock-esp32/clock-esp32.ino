#include <Wire.h>
#include <RTClib.h>
#include <Adafruit_NeoPixel.h>

#define LED_PIN     4
#define NUM_LEDS    60   
#define BRIGHTNESS  30 

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
  delay(20); 
}

void updateClockDisplay(DateTime t) {
  strip.clear();

  int hr = t.hour() % 12;
  int mn = t.minute();
  int sc = t.second();

  int hrPos = (hr * 5) + (mn / 12); 

  for (int i = 0; i <= mn; i++) {
    strip.setPixelColor(i, strip.Color(0, 50, 0)); 
  }

  for (int i = 0; i < 60; i += 5) {
    if (i == 0 || i == 15 || i == 30 || i == 45) {
      strip.setPixelColor(i, strip.Color(0, 0, 255)); 
    } else {
      strip.setPixelColor(i, strip.Color(0, 0, 30)); 
    }
  }

  strip.setPixelColor(mn, strip.Color(255, 0, 0));
  strip.setPixelColor(hrPos, strip.Color(255, 0, 0));
  strip.setPixelColor(sc, Wheel(((millis() / 10) & 255))); 
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
