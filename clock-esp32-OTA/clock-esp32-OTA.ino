#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <Wire.h>
#include <RTClib.h>
#include <Adafruit_NeoPixel.h>

const char* ssid     = "wifi";
const char* password = "password";

#define LED_PIN     4
#define NUM_LEDS    60
#define BRIGHTNESS  20

#define BUTTON_HOUR_UP   25
#define BUTTON_HOUR_DOWN 26
#define BUTTON_MIN_UP    13
#define BUTTON_MIN_DOWN  14

#define NIGHT_START_HOUR  20
#define DAY_START_HOUR    8
#define BRIGHTNESS_DAY    70
#define BRIGHTNESS_NIGHT  30

RTC_DS3231 rtc;
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

#define C_BLANK      strip.Color(0,   0,   0)
#define C_ISARET_ANA strip.Color(200, 200, 200)
#define C_ISARET_ARA strip.Color(30,  30,  30)
#define C_DAKIKA_DOL strip.Color(50,  15,  0)
#define C_DAKIKA_UC  strip.Color(255, 40,  0)
#define C_SAAT       strip.Color(0,   255, 0)

int  currentMode  = 0;
int  lastHour     = -1;
bool animRunning  = false;
int  animStep     = 0;
unsigned long lastAnimUpdate = 0;
bool isNightMode  = false;
bool autoNight    = true;

void adjustTime(int hourChange, int minChange) {
    DateTime now = rtc.now();
    int h = (now.hour()   + hourChange + 24) % 24;
    int m = (now.minute() + minChange  + 60) % 60;
    rtc.adjust(DateTime(now.year(), now.month(), now.day(), h, m, 0));
    Serial.printf("Yeni zaman: %02d:%02d:00\n", h, m);
}

uint32_t Wheel(byte WheelPos) {
    WheelPos = 255 - WheelPos;
    if (WheelPos < 85)  return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
    if (WheelPos < 170) { WheelPos -= 85; return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3); }
    WheelPos -= 170;
    return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}

void updateClockDisplay(DateTime t) {
    strip.clear();

    int hr    = t.hour() % 12;
    int mn    = t.minute();
    int sc    = t.second();
    int hrPos = (hr * 5) + (mn / 12);

    for (int i = 0; i <= mn; i++) {
        strip.setPixelColor(i, C_DAKIKA_DOL);
    }
    for (int i = 0; i < 60; i += 5) {
        if (i == 0 || i == 15 || i == 30 || i == 45)
            strip.setPixelColor(i, C_ISARET_ANA);
        else
            strip.setPixelColor(i, C_ISARET_ARA);
    }

    strip.setPixelColor(mn, C_DAKIKA_UC);

    if (millis() % 2000 < 1900)
        strip.setPixelColor(hrPos, C_SAAT);
    else
        strip.setPixelColor(hrPos, C_BLANK);

    strip.setPixelColor(sc, Wheel((millis() / 15) & 255));

    strip.show();
}

void checkDayNight(int hour) {
    if (!autoNight) return;

    bool shouldBeNight = (hour >= NIGHT_START_HOUR || hour < DAY_START_HOUR);

    if (shouldBeNight && !isNightMode) {
        isNightMode = true;
        strip.setBrightness(BRIGHTNESS_NIGHT);
        Serial.println("Gece moduna gecildi.");
    } else if (!shouldBeNight && isNightMode) {
        isNightMode = false;
        strip.setBrightness(BRIGHTNESS_DAY);
        Serial.println("Gunduz moduna gecildi.");
    }
}

void runHourAnimation() {
    if (millis() - lastAnimUpdate < 15) return;
    lastAnimUpdate = millis();

    strip.setBrightness(255);
    strip.clear();

    int ledPos = animStep % 60;

    for (int i = 0; i < 60; i++) {
        strip.setPixelColor(i, strip.Color(5, 5, 5));
    }
    strip.setPixelColor(ledPos,             Wheel((ledPos * 4) & 255));
    strip.setPixelColor((ledPos + 59) % 60, Wheel(((ledPos - 1) * 4) & 255));
    strip.setPixelColor((ledPos + 58) % 60, strip.Color(20, 20, 20));

    strip.show();
    animStep++;

    if (animStep >= 120) {
        animRunning = false;
        animStep    = 0;
        currentMode = 0;
        strip.setBrightness(isNightMode ? BRIGHTNESS_NIGHT : BRIGHTNESS_DAY);
        Serial.println("Saat basi animasyonu bitti, normal moda donuldu.");
    }
}

void handleSerialCommands() {
    if (!Serial.available()) return;
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd == "mode0") {
        currentMode = 0;
        animRunning = false;
        animStep    = 0;
        strip.setBrightness(isNightMode ? BRIGHTNESS_NIGHT : BRIGHTNESS_DAY);
        Serial.println("Mod: Normal saat");
    }
    else if (cmd == "mode1") {
        currentMode = 1;
        animRunning = true;
        animStep    = 0;
        Serial.println("Mod: Saat basi animasyonu (manuel)");
    }
    else if (cmd == "night on") {
        isNightMode = true;
        autoNight   = false;
        strip.setBrightness(BRIGHTNESS_NIGHT);
        Serial.println("Gece modu: ACIK (manuel)");
    }
    else if (cmd == "night off") {
        isNightMode = false;
        autoNight   = false;
        strip.setBrightness(BRIGHTNESS_DAY);
        Serial.println("Gece modu: KAPALI (manuel)");
    }
    else if (cmd == "night auto") {
        autoNight = true;
        checkDayNight(rtc.now().hour());
        Serial.println("Gece modu: OTOMATIK");
    }
    else if (cmd == "status") {
        DateTime now = rtc.now();
        Serial.printf("Saat: %02d:%02d:%02d\n", now.hour(), now.minute(), now.second());
        Serial.printf("Mod: %d | Gece: %s | Otomatik: %s\n",
            currentMode,
            isNightMode ? "ACIK" : "KAPALI",
            autoNight   ? "EVET" : "HAYIR");
    }
    else {
        Serial.println("Komutlar: mode0, mode1, night on, night off, night auto, status");
    }
}

void setup() {
    Serial.begin(115200);

    pinMode(BUTTON_HOUR_UP,   INPUT_PULLUP);
    pinMode(BUTTON_HOUR_DOWN, INPUT_PULLUP);
    pinMode(BUTTON_MIN_UP,    INPUT_PULLUP);
    pinMode(BUTTON_MIN_DOWN,  INPUT_PULLUP);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi baglandi!");

    ArduinoOTA.setHostname("analog-clock-ESP32");
    ArduinoOTA.onEnd([]() {
        Serial.println("\nOTA tamamlandi, yeniden baslatiliyor...");
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("OTA Hata [%u]: ", error);
        if      (error == OTA_AUTH_ERROR)    Serial.println("Auth hatasi");
        else if (error == OTA_BEGIN_ERROR)   Serial.println("Baslama hatasi");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Baglanti hatasi");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Alma hatasi");
        else if (error == OTA_END_ERROR)     Serial.println("Bitis hatasi");
    });
    ArduinoOTA.begin();

    if (!rtc.begin()) {
        Serial.println("RTC bulunamadi!");
        while (1);
    }
    if (rtc.lostPower()) {
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }

    strip.begin();
    strip.setBrightness(BRIGHTNESS);
    strip.show();

    lastHour = rtc.now().hour();
    checkDayNight(lastHour);

    Serial.println("System ready!");
    Serial.println("Komutlar: mode0, mode1, night on, night off, night auto, status");
}

void loop() {
    ArduinoOTA.handle();
    handleSerialCommands();

    DateTime now = rtc.now();

    if (now.minute() == 0 && now.second() == 0 && now.hour() != lastHour) {
        lastHour    = now.hour();
        currentMode = 1;
        animRunning = true;
        animStep    = 0;
        Serial.printf("Saat basi: %02d:00 - Animasyon basliyor!\n", lastHour);
    }

    static unsigned long lastNightCheck = 0;
    if (millis() - lastNightCheck >= 60000) {
        lastNightCheck = millis();
        checkDayNight(now.hour());
    }

    if (currentMode == 0) {
        static unsigned long lastButtonCheck = 0;
        if (millis() - lastButtonCheck >= 50) {
            lastButtonCheck = millis();
            if (digitalRead(BUTTON_HOUR_UP)   == LOW) adjustTime(1,  0);
            if (digitalRead(BUTTON_HOUR_DOWN) == LOW) adjustTime(-1, 0);
            if (digitalRead(BUTTON_MIN_UP)    == LOW) adjustTime(0,  1);
            if (digitalRead(BUTTON_MIN_DOWN)  == LOW) adjustTime(0, -1);
        }
    }

    if (currentMode == 1 && animRunning) {
        runHourAnimation();
    } else {
        static unsigned long lastUpdate = 0;
        if (millis() - lastUpdate >= 20) {
            lastUpdate = millis();
            updateClockDisplay(now);
        }
    }
}
