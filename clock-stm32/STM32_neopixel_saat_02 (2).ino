#include <RTClock.h>
#include <WS2812B.h>

#define NUM_LEDS 61 // LED şeridi sayısı
//#define LED_PIN PA7 // WS2812B veri pini (varsayılan, değiştirilebilir)

// Buton pin tanımlamaları
#define BUTTON_HOUR_UP   PA8   // Saat artırma butonu
#define BUTTON_HOUR_DOWN PB15  // Saat azaltma butonu
#define BUTTON_MIN_UP    PB14  // Dakika artırma butonu
#define BUTTON_MIN_DOWN  PB13  // Dakika azaltma butonu

RTClock rtclock(RTCSEL_LSE); // RTC modülünü düşük güç dış osilatör (LSE) ile başlat
time_t tt, tt1;              // Unix zaman damgaları
tm_t mtt;                    // Saat bileşenleri
char s[128];                 // sprintf için tampon

WS2812B strip = WS2812B(NUM_LEDS);
uint16_t j;

// Renk tanımları
long cBlank, cIsaret0, cIsaret1, cSaniye, cSaat, cDakika;
int saatx, dakika, saniye; // Zaman değişkenleri
int saat; // LED için saat konumu (0, 5, 10, ..., 55)
long x = 1; // Renk geçişi için

// RTC'den saat okuma fonksiyonu
void readRTCTime() {
    rtclock.breakTime(rtclock.now(), mtt);
    
    // Zamanı değişkenlere ayır
    saatx = mtt.hour;
    dakika = mtt.minute;
    saniye = mtt.second;
    
    // Seri monitörde göster
    sprintf(s, "Time: %02u:%02u:%02u\n", saatx, dakika, saniye);
    Serial.print(s);
}

// Saat ve dakika ayarlama fonksiyonu
void adjustTime(int hourChange, int minChange) {
    // Mevcut RTC zamanını al
    rtclock.breakTime(rtclock.now(), mtt);
    
    // tm_t yapısını sıfırla ve sadece gerekli alanları ayarla
    tm_t newTime = {0}; // Tüm alanları sıfırla
    newTime.hour = (mtt.hour + hourChange + 24) % 24; // Saat ayarı (0-23)
    newTime.minute = (mtt.minute + minChange + 60) % 60; // Dakika ayarı (0-59)
    newTime.second = 0; // Saniyeyi sıfırla
    newTime.day = 1;    // Tarih alanlarını sabitle (1 Ocak 1970)
    newTime.month = 1;
    newTime.year = 0;
    
    // Yeni zamanı Unix formatına çevir ve RTC'ye yaz
    tt = rtclock.makeTime(newTime);
    rtclock.setTime(tt);
    
    readRTCTime(); // Ayardan sonra zamanı yazdır
}

void setup() {
    Serial.begin(115200); // Hata ayıklama için seri port
    
    // Buton pinlerini giriş olarak ayarla (dahili pull-up direnci ile)
    pinMode(BUTTON_HOUR_UP, INPUT_PULLUP);
    pinMode(BUTTON_HOUR_DOWN, INPUT_PULLUP);
    pinMode(BUTTON_MIN_UP, INPUT_PULLUP);
    pinMode(BUTTON_MIN_DOWN, INPUT_PULLUP);
    
    // LED şeridini başlat
    strip.begin(); // PA7 pini
    strip.show();
    
    // Renk tanımları
    cBlank = strip.Color(0, 0, 0);
    cIsaret0 = strip.Color(60, 60, 60);
    cIsaret1 = strip.Color(5, 5, 5);
    cSaniye = strip.Color(10, 200, 10);
    cSaat = strip.Color(200, 200, 0);
    cDakika = strip.Color(60, 0, 60);

//    cBlank = strip.Color(1, 1, 1);
//    cIsaret0 = strip.Color(100, 100, 100);
//    cIsaret1 = strip.Color(20, 20, 20);
//    cSaniye = strip.Color(10, 200, 10);
//    cSaat = strip.Color(200, 200, 0);
//    cDakika = strip.Color(60, 0, 60);
    
    // Mevcut RTC zamanını oku
    tt = rtclock.now();
    rtclock.breakTime(tt, mtt);
    
    // Eğer RTC zamanı geçersizse (1971 öncesi), varsayılan zaman ayarla
    if (tt < 31536000) { // 1 yıl (1971 öncesi, yaklaşık 31536000 saniye)
        tm_t initTime = {0}; // Tüm alanları sıfırla
        initTime.hour = 0;   // Varsayılan başlangıç saati
        initTime.minute = 0;
        initTime.second = 0;
        initTime.day = 1;    // Tarih alanlarını sabitle (1 Ocak 1970)
        initTime.month = 1;
        initTime.year = 0;
        tt = rtclock.makeTime(initTime);
        rtclock.setTime(tt);
    }
    
    tt1 = tt;
    readRTCTime(); // Başlangıç zamanını yazdır
}

void loop() {
    // Buton kontrolleri
    if (digitalRead(BUTTON_HOUR_UP) == LOW) { // Saat + butonu
        adjustTime(1, 0);
        while (digitalRead(BUTTON_HOUR_UP) == LOW); // Buton bırakılana kadar bekle
        delay(200); // Debouncing için gecikme
    }
    if (digitalRead(BUTTON_HOUR_DOWN) == LOW) { // Saat - butonu
        adjustTime(-1, 0);
        while (digitalRead(BUTTON_HOUR_DOWN) == LOW); // Buton bırakılana kadar bekle
        delay(200); // Debouncing için gecikme
    }
    if (digitalRead(BUTTON_MIN_UP) == LOW) { // Dakika + butonu
        adjustTime(0, 1);
        while (digitalRead(BUTTON_MIN_UP) == LOW); // Buton bırakılana kadar bekle
        delay(200); // Debouncing için gecikme
    }
    if (digitalRead(BUTTON_MIN_DOWN) == LOW) { // Dakika - butonu
        adjustTime(0, -1);
        while (digitalRead(BUTTON_MIN_DOWN) == LOW); // Buton bırakılana kadar bekle
        delay(200); // Debouncing için gecikme
    }

    // Zaman değiştiğinde LED ve seri monitörü güncelle
    time_t currentTime = rtclock.now();
    if (tt1 != currentTime) {
        tt1 = currentTime;
        readRTCTime(); // RTC'den zamanı oku
        
        // Saatx'i saate çevir (0, 5, 10, ..., 55)
        if (saatx == 0 || saatx == 12) saat = 0;
        else if (saatx == 1 || saatx == 13) saat = 5;
        else if (saatx == 2 || saatx == 14) saat = 10;
        else if (saatx == 3 || saatx == 15) saat = 15;
        else if (saatx == 4 || saatx == 16) saat = 20;
        else if (saatx == 5 || saatx == 17) saat = 25;
        else if (saatx == 6 || saatx == 18) saat = 30;
        else if (saatx == 7 || saatx == 19) saat = 35;
        else if (saatx == 8 || saatx == 20) saat = 40;
        else if (saatx == 9 || saatx == 21) saat = 45;
        else if (saatx == 10 || saatx == 22) saat = 50;
        else if (saatx == 11 || saatx == 23) saat = 55;

        // LED şeridini güncelle
        for (j = 255; j > 0; j--) {
            // Tüm LED'leri temizle
            for (int i = 1; i <= 60; i++) {
                strip.setPixelColor(i, cBlank);
            }

            // Dakika LED'leri
            for (int i = 1; i <= dakika; i++) {
                strip.setPixelColor(i, strip.Color(1, 0, 1));
            }

            // İşaret LED'leri
            strip.setPixelColor(0, cIsaret0);
            strip.setPixelColor(5, cIsaret1);
            strip.setPixelColor(10, cIsaret1);
            strip.setPixelColor(15, cIsaret0);
            strip.setPixelColor(20, cIsaret1);
            strip.setPixelColor(25, cIsaret1);
            strip.setPixelColor(30, cIsaret0);
            strip.setPixelColor(35, cIsaret1);
            strip.setPixelColor(40, cIsaret1);
            strip.setPixelColor(45, cIsaret0);
            strip.setPixelColor(50, cIsaret1);
            strip.setPixelColor(55, cIsaret1);

            // Dakika ve saat LED'leri
            strip.setPixelColor(dakika, cDakika);
            strip.setPixelColor(saat, cSaat);

            // Saat için renk geçişi
            x++;
            if (x > 255 && x < 510) strip.setPixelColor(saat, strip.Color(20, 510 - x, 20));
            else if (x <= 255) strip.setPixelColor(saat, strip.Color(0, x, 20));
            if (x >= 510) x = 1;

            // Saniye için renk geçişi
            strip.setPixelColor(saniye, Wheel(j & 255));

            strip.show();
            delay(3);
        }
    }
}

// Renk geçişi fonksiyonu
uint32_t Wheel(byte WheelPos) {
    if (WheelPos < 85) {
        return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
    } else if (WheelPos < 170) {
        WheelPos -= 85;
        return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
    } else {
        WheelPos -= 170;
        return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
    }
}
