#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <Wire.h>
#include <RTClib.h>
#include <Adafruit_NeoPixel.h>
#include <WebServer.h>
#include <time.h>

const char* ssid     = "wifi";
const char* password = "password";

#define LED_PIN     4
#define NUM_LEDS    60

#define BUTTON_HOUR_UP   25
#define BUTTON_HOUR_DOWN 26
#define BUTTON_MIN_UP    13
#define BUTTON_MIN_DOWN  14

#define NIGHT_START_HOUR  20
#define DAY_START_HOUR    8
#define BRIGHTNESS_DAY    80
#define BRIGHTNESS_NIGHT  40

#define NTP_SERVER    "pool.ntp.org"
#define UTC_OFFSET    10800
#define NTP_INTERVAL  86400000UL

#define ALARM_DURATION 15000UL

RTC_DS3231 rtc;
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
WebServer server(80);

uint8_t c_saat_r=0,  c_saat_g=255, c_saat_b=0;
uint8_t c_dol_r=50,  c_dol_g=15,   c_dol_b=0;
uint8_t c_uc_r=255,  c_uc_g=40,    c_uc_b=0;
uint8_t c_ana_r=200, c_ana_g=200,  c_ana_b=200;
uint8_t c_ara_r=30,  c_ara_g=30,   c_ara_b=30;
uint8_t brightness_day=80, brightness_night=40;

int  currentMode  = 0;
int  lastHour     = -1;
bool animRunning  = false;
int  animStep     = 0;
unsigned long lastAnimUpdate  = 0;
bool isNightMode  = false;
bool autoNight    = true;
unsigned long lastNtpSync = 0;

struct AlarmClock {
    int  hour    = 7;
    int  minute  = 0;
    bool enabled = false;
};
AlarmClock myAlarm;
bool  alarmFiring     = false;
unsigned long alarmStartTime  = 0;
unsigned long lastAlarmUpdate = 0;

unsigned long lastExtraAnimUpdate = 0;
int  meteorPos    = 0;
int  radarPos     = 0;
uint8_t radarTrail[60] = {0};

void adjustTime(int hourChange, int minChange) {
    DateTime now = rtc.now();
    int h = (now.hour()   + hourChange + 24) % 24;
    int m = (now.minute() + minChange  + 60) % 60;
    rtc.adjust(DateTime(now.year(), now.month(), now.day(), h, m, 0));
}

uint32_t Wheel(byte WheelPos) {
    WheelPos = 255 - WheelPos;
    if (WheelPos < 85)  return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
    if (WheelPos < 170) { WheelPos -= 85; return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3); }
    WheelPos -= 170;
    return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}

uint8_t breathCalc() {
    float t = (millis() % 3000) / 3000.0f;
    float b = 0.3f + 0.7f * (1.0f - cos(t * 2.0f * PI)) / 2.0f;
    return (uint8_t)(b * 255);
}

void updateClockDisplay(DateTime t) {
    strip.clear();
    int hr    = t.hour() % 12;
    int mn    = t.minute();
    int sc    = t.second();
    int hrPos = hr * 5;

    for (int i = 0; i <= mn; i++)
        strip.setPixelColor(i, strip.Color(c_dol_r, c_dol_g, c_dol_b));

    for (int i = 0; i < 60; i += 5) {
        if (i == 0 || i == 15 || i == 30 || i == 45)
            strip.setPixelColor(i, strip.Color(c_ana_r, c_ana_g, c_ana_b));
        else
            strip.setPixelColor(i, strip.Color(c_ara_r, c_ara_g, c_ara_b));
    }

    uint8_t boost = 1.3f;
    auto boosted = [](uint8_t v) -> uint8_t {
    return (uint8_t)min(255, (int)(v * 1.3f));
    };
    strip.setPixelColor(mn, strip.Color(boosted(c_uc_r), boosted(c_uc_g), boosted(c_uc_b)));

    if (millis() % 2000 < 1900)
        strip.setPixelColor(hrPos, strip.Color(c_saat_r, c_saat_g, c_saat_b));
    else
        strip.setPixelColor(hrPos, strip.Color(0, 0, 0));

    strip.setPixelColor(sc, Wheel((millis() / 15) & 255));
    strip.show();
}

void runHourAnimation() {
    if (millis() - lastAnimUpdate < 15) return;
    lastAnimUpdate = millis();
    strip.setBrightness(255);
    strip.clear();
    int ledPos = animStep % 60;
    for (int i = 0; i < 60; i++) strip.setPixelColor(i, strip.Color(5, 5, 5));
    strip.setPixelColor(ledPos,             Wheel((ledPos * 4) & 255));
    strip.setPixelColor((ledPos + 59) % 60, Wheel(((ledPos - 1) * 4) & 255));
    strip.setPixelColor((ledPos + 58) % 60, strip.Color(20, 20, 20));
    strip.show();
    animStep++;
    if (animStep >= 120) {
        animRunning  = false;
        animStep     = 0;
        currentMode  = 0;
        strip.setBrightness(isNightMode ? brightness_night : brightness_day);
    }
}

void runAlarmAnimation() {
    unsigned long now_ms  = millis();
    unsigned long elapsed = now_ms - alarmStartTime;

    if (elapsed >= ALARM_DURATION) {
        alarmFiring  = false;
        currentMode  = 0;
        strip.clear();
        strip.show();
        strip.setBrightness(isNightMode ? brightness_night : brightness_day);
        return;
    }

    if (now_ms - lastAlarmUpdate < 25) return;
    lastAlarmUpdate = now_ms;

    strip.setBrightness(255);
    uint8_t offset = (uint8_t)((now_ms / 8) & 255);
    uint8_t mix    = (uint8_t)(elapsed * 255UL / ALARM_DURATION);

    for (int i = 0; i < 60; i++) {
        uint8_t cw  = ((i * 4) + offset) & 255;
        uint8_t ccw = ((60 - i) * 4 + (uint8_t)(offset * 2)) & 255;
        uint32_t c1 = Wheel(cw);
        uint32_t c2 = Wheel(ccw);
        uint8_t r = (uint8_t)(((c1>>16&0xFF)*(255-mix) + (c2>>16&0xFF)*mix) >> 8);
        uint8_t g = (uint8_t)(((c1>> 8&0xFF)*(255-mix) + (c2>> 8&0xFF)*mix) >> 8);
        uint8_t b = (uint8_t)(((c1    &0xFF)*(255-mix) + (c2    &0xFF)*mix) >> 8);
        strip.setPixelColor(i, strip.Color(r, g, b));
    }
    strip.show();
}

void runBreathAnimation() {
    if (millis() - lastExtraAnimUpdate < 20) return;
    lastExtraAnimUpdate = millis();

    float t = (millis() % 4000) / 4000.0f;
    float b = (1.0f - cos(t * 2.0f * PI)) / 2.0f;
    b = 0.08f + 0.92f * b;
    uint8_t bright = (uint8_t)(b * 255);

    strip.setBrightness(255);
    uint8_t r = (uint8_t)(bright * 0.55f);
    uint8_t g = (uint8_t)(bright * 0.75f);
    uint8_t bv = bright;
    for (int i = 0; i < 60; i++)
        strip.setPixelColor(i, strip.Color(r, g, bv));
    strip.show();
}

void runMeteorAnimation() {
    if (millis() - lastExtraAnimUpdate < 30) return;
    lastExtraAnimUpdate = millis();

    strip.setBrightness(255);
    strip.clear();

    for (int i = 0; i < 60; i++) {
        uint32_t c = strip.getPixelColor(i);
        uint8_t r = (c >> 16) & 0xFF;
        uint8_t g = (c >>  8) & 0xFF;
        uint8_t b =  c        & 0xFF;
        r = r > 10 ? r * 3 / 4 : 0;
        g = g > 10 ? g * 3 / 4 : 0;
        b = b > 10 ? b * 3 / 4 : 0;
        strip.setPixelColor(i, strip.Color(r, g, b));
    }

    uint8_t colorPos = (meteorPos * 4) & 255;
    strip.setPixelColor(meteorPos,                   strip.Color(255, 255, 255));
    strip.setPixelColor((meteorPos + 59) % 60,       Wheel(colorPos));
    strip.setPixelColor((meteorPos + 58) % 60,       Wheel((colorPos + 20) & 255));

    strip.show();
    meteorPos = (meteorPos + 1) % 60;
}

void runRadarAnimation() {
    if (millis() - lastExtraAnimUpdate < 40) return;
    lastExtraAnimUpdate = millis();

    strip.setBrightness(255);

    for (int i = 0; i < 60; i++) {
        radarTrail[i] = radarTrail[i] > 15 ? radarTrail[i] - 15 : 0;
    }

    radarTrail[radarPos] = 255;

    for (int i = 0; i < 60; i++) {
        uint8_t v = radarTrail[i];
        if (v > 0) {
            strip.setPixelColor(i, strip.Color(0, v, v / 4));
        } else {
            strip.setPixelColor(i, strip.Color(0, 0, 0));
        }
    }

    strip.setPixelColor(radarPos, strip.Color(100, 255, 200));

    strip.show();
    radarPos = (radarPos + 1) % 60;
}

void hexToRgb(String hex, uint8_t &r, uint8_t &g, uint8_t &b) {
    if (hex.startsWith("#")) hex = hex.substring(1);
    r = strtol(hex.substring(0,2).c_str(), NULL, 16);
    g = strtol(hex.substring(2,4).c_str(), NULL, 16);
    b = strtol(hex.substring(4,6).c_str(), NULL, 16);
}

String rgbToHex(uint8_t r, uint8_t g, uint8_t b) {
    char buf[8]; sprintf(buf, "#%02x%02x%02x", r, g, b); return String(buf);
}

bool syncNTP() {
    if (WiFi.status() != WL_CONNECTED) return false;
    configTime(UTC_OFFSET, 0, NTP_SERVER);
    struct tm timeinfo;
    int retry = 0;
    while (!getLocalTime(&timeinfo) && retry < 10) { delay(500); retry++; }
    if (retry >= 10) return false;
    rtc.adjust(DateTime(timeinfo.tm_year+1900, timeinfo.tm_mon+1, timeinfo.tm_mday,
                        timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec));
    lastNtpSync = millis();
    return true;
}

void checkDayNight(int hour) {
    if (!autoNight) return;
    bool shouldBeNight = (hour >= NIGHT_START_HOUR || hour < DAY_START_HOUR);
    if (shouldBeNight && !isNightMode) {
        isNightMode = true;  strip.setBrightness(brightness_night);
    } else if (!shouldBeNight && isNightMode) {
        isNightMode = false; strip.setBrightness(brightness_day);
    }
}

void stopAlarmNow() {
    alarmFiring  = false;
    currentMode  = 0;
    animRunning  = false;
    animStep     = 0;
    strip.clear();
    strip.show();
    strip.setBrightness(isNightMode ? brightness_night : brightness_day);
}

void triggerAlarm() {
    alarmFiring     = true;
    alarmStartTime  = millis();
    lastAlarmUpdate = millis();
    currentMode     = 2;
}

void checkAlarm(DateTime now) {
    if (!myAlarm.enabled || alarmFiring) return;
    if (now.hour()==myAlarm.hour && now.minute()==myAlarm.minute && now.second()==0)
        triggerAlarm();
}

void startExtraAnim(int mode) {
    currentMode          = mode;
    lastExtraAnimUpdate  = 0;
    meteorPos            = 0;
    radarPos             = 0;
    memset(radarTrail, 0, sizeof(radarTrail));
    strip.setBrightness(255);
    strip.clear();
    strip.show();
}


const char HTML_PAGE[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="tr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Analog Saat</title>
<style>
@import url('https://fonts.googleapis.com/css2?family=DM+Mono:wght@300;400;500&display=swap');
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
:root{
  --bg:#0a0a0a;--surface:#141414;--surface2:#1c1c1c;
  --border:#252525;--text:#e0e0e0;--muted:#555;--accent:#fff;
  --green:#00e676;--red:#ff5252;--amber:#ffab40;
}
body{background:var(--bg);color:var(--text);font-family:'DM Mono',monospace;min-height:100vh}
.header{padding:1.5rem 1.5rem 0;display:flex;justify-content:space-between;align-items:flex-start}
.brand{font-size:0.6rem;letter-spacing:0.25em;color:var(--muted);text-transform:uppercase}
.chip{font-size:0.6rem;color:var(--muted);background:var(--surface);
      border:1px solid var(--border);padding:0.2rem 0.5rem}
.clock-wrap{padding:2rem 1.5rem 1.5rem;border-bottom:1px solid var(--border)}
.clock{font-size:4rem;font-weight:300;letter-spacing:-0.03em;color:var(--accent);
       font-variant-numeric:tabular-nums;line-height:1}
.clock-sub{margin-top:0.5rem;font-size:0.6rem;color:var(--muted);
           letter-spacing:0.1em;display:flex;gap:1.5rem;flex-wrap:wrap}
.dot{display:inline-block;width:5px;height:5px;border-radius:50%;margin-right:0.4rem;vertical-align:middle}
.dot.on{background:var(--green)}
.dot.off{background:var(--muted)}
.dot.red{background:var(--red);animation:dpulse 0.5s infinite alternate}
@keyframes dpulse{to{opacity:0.2}}
.alarm-badge{font-size:0.6rem;padding:0.15rem 0.45rem;border-radius:2px}
.alarm-badge.on{background:var(--green);color:#000}
.alarm-badge.firing{background:var(--red);color:#fff;animation:dpulse 0.4s infinite alternate}
.tabs{display:flex;border-bottom:1px solid var(--border)}
.tab{flex:1;padding:0.7rem 0.4rem;font-family:'DM Mono',monospace;font-size:0.58rem;
     letter-spacing:0.08em;text-transform:uppercase;color:var(--muted);background:none;
     border:none;border-bottom:2px solid transparent;cursor:pointer;transition:all 0.15s}
.tab.active{color:var(--accent);border-bottom-color:var(--accent)}
.tab-content{display:none;padding:1.25rem 1.5rem}
.tab-content.active{display:block}
.field{display:flex;align-items:center;justify-content:space-between;
       padding:0.6rem 0;border-bottom:1px solid var(--border)}
.field:last-child{border-bottom:none}
.field-label{font-size:0.65rem;color:var(--muted);letter-spacing:0.04em}
input[type="color"]{width:2.2rem;height:1.8rem;border:1px solid var(--border);
                    background:none;cursor:pointer;padding:2px;border-radius:2px}
input[type="range"]{width:110px;accent-color:var(--accent)}
input[type="number"]{background:var(--surface2);border:1px solid var(--border);
                     color:var(--text);padding:0.4rem 0.5rem;font-family:'DM Mono',monospace;
                     font-size:0.75rem;width:100%;text-align:center}
input[type="number"]:focus{outline:1px solid var(--accent);border-color:var(--accent)}
.range-val{font-size:0.65rem;color:var(--muted);width:2rem;text-align:right}
.slabel{font-size:0.58rem;letter-spacing:0.12em;text-transform:uppercase;color:var(--muted);
        margin-bottom:0.5rem;margin-top:1rem;display:block}
.slabel:first-child{margin-top:0}
.btn-row{display:flex;gap:1px;margin-bottom:1px}
.btn{background:var(--surface);border:1px solid var(--border);color:var(--muted);
     padding:0.65rem 0.75rem;font-family:'DM Mono',monospace;font-size:0.63rem;
     letter-spacing:0.06em;cursor:pointer;transition:all 0.12s;flex:1;text-align:center}
.btn:hover{background:var(--surface2);color:var(--text)}
.btn.active{border-color:var(--accent);color:var(--accent);background:var(--surface2)}
.btn.green{border-color:var(--green);color:var(--green)}
.btn.green:hover{background:var(--green);color:#000}
.btn.red{border-color:var(--red);color:var(--red)}
.btn.red:hover{background:var(--red);color:#000}
.btn.amber{border-color:var(--amber);color:var(--amber)}
.btn.amber:hover{background:var(--amber);color:#000}
.btn.full{width:100%;display:block;margin-bottom:1px;flex:none}
.btn:disabled{opacity:0.35;cursor:not-allowed;pointer-events:none}
.pair{display:flex;gap:1px;margin-bottom:0.75rem}
.pair input{flex:1;width:auto}
.alarm-section .pair input{transition:border-color 0.2s}
.alarm-section.editing .pair input{border-color:var(--amber);outline:1px solid var(--amber)}
.alarm-hint{font-size:0.6rem;color:var(--muted);margin-bottom:0.6rem;min-height:0.9rem}
.alarm-hint.active{color:var(--amber)}
.anim-grid{display:grid;grid-template-columns:1fr 1fr;gap:1px;margin-bottom:1px}
.anim-card{background:var(--surface);border:1px solid var(--border);
           padding:0.8rem 0.75rem;cursor:pointer;transition:all 0.12s;text-align:left}
.anim-card:hover{background:var(--surface2);border-color:var(--accent)}
.anim-card.running{border-color:var(--green);background:var(--surface2)}
.anim-card-title{font-size:0.65rem;color:var(--text);letter-spacing:0.05em;display:block;margin-bottom:0.2rem}
.anim-card-sub{font-size:0.58rem;color:var(--muted)}
.toast{position:fixed;bottom:1.5rem;right:1.5rem;background:var(--surface2);
       color:var(--text);border:1px solid var(--border);padding:0.5rem 1rem;
       font-size:0.65rem;letter-spacing:0.07em;opacity:0;transition:opacity 0.2s;
       pointer-events:none;max-width:220px;z-index:99}
.toast.show{opacity:1}
</style>
</head>
<body>

<div class="header">
  <div class="brand">analog saat</div>
  <div class="chip" id="ip-chip">—</div>
</div>

<div class="clock-wrap">
  <div class="clock" id="clock">--:--:--</div>
  <div class="clock-sub">
    <span><span class="dot off" id="dot-night"></span><span id="lbl-night">gündüz</span></span>
    <span><span class="dot off" id="dot-ntp"></span><span id="lbl-ntp">ntp —</span></span>
    <span><span class="dot on"  id="dot-mode"></span><span id="lbl-mode">normal</span></span>
    <span id="alarm-badge-wrap" style="display:none">
      <span class="alarm-badge on" id="alarm-badge">—</span>
    </span>
  </div>
</div>

<div class="tabs">
  <button class="tab active" onclick="showTab('renkler',this)">renkler</button>
  <button class="tab" onclick="showTab('parlak',this)">parlaklık</button>
  <button class="tab" onclick="showTab('kontrol',this)">kontrol</button>
  <button class="tab" onclick="showTab('saat',this)">saat</button>
</div>

<!-- TAB: RENKLER -->
<div class="tab-content active" id="tab-renkler">
  <div class="field"><span class="field-label">saat göstergesi</span><input type="color" id="c_saat" onchange="setColor('saat',this.value)"></div>
  <div class="field"><span class="field-label">dakika dolgu</span><input type="color" id="c_dol" onchange="setColor('dol',this.value)"></div>
  <div class="field"><span class="field-label">dakika ucu (nefes)</span><input type="color" id="c_uc" onchange="setColor('uc',this.value)"></div>
  <div class="field"><span class="field-label">ana işaret 12/3/6/9</span><input type="color" id="c_ana" onchange="setColor('ana',this.value)"></div>
  <div class="field"><span class="field-label">ara işaret</span><input type="color" id="c_ara" onchange="setColor('ara',this.value)"></div>
</div>

<!-- TAB: PARLAKLIK -->
<div class="tab-content" id="tab-parlak">
  <div class="field">
    <span class="field-label">gündüz</span>
    <input type="range" id="br_day" min="5" max="255"
           oninput="document.getElementById('br_day_val').textContent=this.value"
           onchange="setBrightness('day',this.value)">
    <span class="range-val" id="br_day_val">—</span>
  </div>
  <div class="field">
    <span class="field-label">gece</span>
    <input type="range" id="br_night" min="5" max="255"
           oninput="document.getElementById('br_night_val').textContent=this.value"
           onchange="setBrightness('night',this.value)">
    <span class="range-val" id="br_night_val">—</span>
  </div>
</div>

<!-- TAB: KONTROL -->
<div class="tab-content" id="tab-kontrol">

  <span class="slabel">mod</span>
  <div class="btn-row">
    <button class="btn active" id="btn_mode0" onclick="setMode(0)">normal saat</button>
  </div>

  <span class="slabel">gece modu</span>
  <div class="btn-row">
    <button class="btn" id="btn_night_on"  onclick="nightMode('on')">açık</button>
    <button class="btn" id="btn_night_off" onclick="nightMode('off')">kapalı</button>
  </div>
  <button class="btn full" id="btn_night_auto" onclick="nightMode('auto')">otomatik</button>

  <span class="slabel">animasyonlar</span>
  <div class="anim-grid">
    <div class="anim-card" id="anim_hour" onclick="triggerAnim('hour')">
      <span class="anim-card-title">saat başı</span>
      <span class="anim-card-sub">dönen gökkuşağı · 2 tur</span>
    </div>
    <div class="anim-card" id="anim_alarm" onclick="triggerAnim('alarm')">
      <span class="anim-card-title">alarm efekti</span>
      <span class="anim-card-sub">karşıt renk dalgası · 15sn</span>
    </div>
    <div class="anim-card" id="anim_breath" onclick="triggerAnim('breath')">
      <span class="anim-card-title">nefes</span>
      <span class="anim-card-sub">açılıp kapanan organik ışık</span>
    </div>
    <div class="anim-card" id="anim_meteor" onclick="triggerAnim('meteor')">
      <span class="anim-card-title">meteor</span>
      <span class="anim-card-sub">kuyruklu yıldız · solan iz</span>
    </div>
    <div class="anim-card" id="anim_radar" onclick="triggerAnim('radar')">
      <span class="anim-card-title">radar</span>
      <span class="anim-card-sub">dönen yeşil tarama</span>
    </div>
  </div>
  <button class="btn full red" id="btn_anim_stop" onclick="stopAnim()" style="display:none">
    animasyonu durdur → normal saat
  </button>

  <span class="slabel">diğer</span>
  <button class="btn full amber" id="btn_ntp" onclick="ntpSync()">ntp sync</button>

</div>

<!-- TAB: SAAT & ALARM -->
<div class="tab-content" id="tab-saat">

  <span class="slabel">saat ayarla</span>
  <div class="pair">
    <input type="number" id="set_hour" min="0" max="23" placeholder="saat">
    <input type="number" id="set_min"  min="0" max="59" placeholder="dakika">
  </div>
  <button class="btn full green" onclick="setTime()" style="margin-bottom:1.5rem">kaydet</button>

  <span class="slabel">alarm</span>
  <div class="alarm-section" id="alarm-section">
    <div class="alarm-hint" id="alarm-hint">saat ve dakika gir, aktif et'e bas</div>
    <div class="pair" id="alarm-pair">
      <input type="number" id="alarm_h" min="0" max="23" placeholder="saat"
             onfocus="onAlarmFocus()" onblur="onAlarmBlur()">
      <input type="number" id="alarm_m" min="0" max="59" placeholder="dakika"
             onfocus="onAlarmFocus()" onblur="onAlarmBlur()">
    </div>
    <div class="btn-row">
      <button class="btn green" onclick="setAlarm(true)">aktif et</button>
      <button class="btn"       onclick="setAlarm(false)">kapat</button>
    </div>
    <button class="btn full red" id="btn_alarm_stop" onclick="stopAlarmBtn()"
            style="display:none;margin-top:1px">alarmı durdur</button>
  </div>

</div>

<div class="toast" id="toast"></div>

<script>
const ANIM_TYPES = ['hour','alarm','breath','meteor','radar'];
let alarmEditing   = false;
let alarmBlurTimer = null;
let toastTimer;

function toast(msg, dur=2000) {
  const t = document.getElementById('toast');
  t.textContent = msg; t.classList.add('show');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => t.classList.remove('show'), dur);
}

function showTab(id, el) {
  document.querySelectorAll('.tab-content').forEach(e => e.classList.remove('active'));
  document.querySelectorAll('.tab').forEach(e => e.classList.remove('active'));
  document.getElementById('tab-'+id).classList.add('active');
  el.classList.add('active');
}

function safeSet(id, val) {
  const el = document.getElementById(id);
  if (el && document.activeElement !== el) el.value = val;
}

function onAlarmFocus() {
  clearTimeout(alarmBlurTimer);
  alarmEditing = true;
  document.getElementById('alarm-section').classList.add('editing');
  const hint = document.getElementById('alarm-hint');
  hint.textContent = 'düzenleniyor — bitince "aktif et"e bas';
  hint.classList.add('active');
}

function onAlarmBlur() {
  alarmBlurTimer = setTimeout(() => {
    alarmEditing = false;
    document.getElementById('alarm-section').classList.remove('editing');
    const hint = document.getElementById('alarm-hint');
    hint.textContent = 'saat ve dakika gir, aktif et\'e bas';
    hint.classList.remove('active');
  }, 200);
}

// mod_num → animasyon kartı eşleşmesi
const MODE_TO_ANIM = {1:'hour', 2:'alarm', 3:'breath', 4:'meteor', 5:'radar'};

function fetchStatus() {
  fetch('/api/status', {signal: AbortSignal.timeout(2000)})
    .then(r => r.json())
    .then(d => {
      document.getElementById('clock').textContent   = d.time;
      document.getElementById('ip-chip').textContent = d.ip;

      safeSet('c_saat', d.c_saat);
      safeSet('c_dol',  d.c_dol);
      safeSet('c_uc',   d.c_uc);
      safeSet('c_ana',  d.c_ana);
      safeSet('c_ara',  d.c_ara);

      if (document.activeElement !== document.getElementById('br_day')) {
        document.getElementById('br_day').value = d.br_day;
        document.getElementById('br_day_val').textContent = d.br_day;
      }
      if (document.activeElement !== document.getElementById('br_night')) {
        document.getElementById('br_night').value = d.br_night;
        document.getElementById('br_night_val').textContent = d.br_night;
      }

      if (!alarmEditing) {
        safeSet('alarm_h', d.alarm_h);
        safeSet('alarm_m', d.alarm_m);
      }

      const nightOn = d.night === 'on';
      document.getElementById('dot-night').className  = 'dot '+(nightOn?'on':'off');
      document.getElementById('lbl-night').textContent = nightOn ? 'gece' : 'gündüz';

      document.getElementById('dot-ntp').className  = 'dot '+(d.ntp_ok?'on':'off');
      document.getElementById('lbl-ntp').textContent = 'ntp '+d.last_ntp;

      document.getElementById('lbl-mode').textContent = d.mode;
      document.getElementById('btn_mode0').classList.toggle('active', d.mode_num === 0);

      ['night_on','night_off','night_auto'].forEach(id =>
        document.getElementById('btn_'+id).classList.remove('active'));
      if (!d.auto_night)
        document.getElementById(nightOn?'btn_night_on':'btn_night_off').classList.add('active');
      else
        document.getElementById('btn_night_auto').classList.add('active');

      // Animasyon kartları
      const animStop = document.getElementById('btn_anim_stop');
      ANIM_TYPES.forEach(t => document.getElementById('anim_'+t).classList.remove('running'));
      const runningAnim = MODE_TO_ANIM[d.mode_num];
      if (runningAnim) {
        document.getElementById('anim_'+runningAnim).classList.add('running');
        animStop.style.display = 'block';
      } else {
        animStop.style.display = 'none';
      }

      // Alarm badge
      const firing  = d.alarm_firing  === true || d.alarm_firing  === 'true';
      const enabled = d.alarm_enabled === true || d.alarm_enabled === 'true';
      const badge     = document.getElementById('alarm-badge');
      const badgeWrap = document.getElementById('alarm-badge-wrap');
      const stopBtn   = document.getElementById('btn_alarm_stop');

      if (firing) {
        badgeWrap.style.display = '';
        badge.textContent = '⚡ alarm!';
        badge.className   = 'alarm-badge firing';
        stopBtn.style.display = 'block';
        document.getElementById('dot-mode').className = 'dot red';
      } else if (enabled) {
        badgeWrap.style.display = '';
        badge.textContent = String(d.alarm_h).padStart(2,'0')+':'+String(d.alarm_m).padStart(2,'0');
        badge.className   = 'alarm-badge on';
        stopBtn.style.display = 'none';
        document.getElementById('dot-mode').className = 'dot on';
      } else {
        badgeWrap.style.display = 'none';
        stopBtn.style.display   = 'none';
        document.getElementById('dot-mode').className = 'dot on';
      }
    })
    .catch(() => {});
}

function api(url, successMsg, cb) {
  fetch(url, {signal: AbortSignal.timeout(3000)})
    .then(r => { if (!r.ok) toast('hata!'); else { if (successMsg) toast(successMsg); if (cb) cb(); } })
    .catch(() => toast('baglanti hatasi'));
}

function setColor(key, val) { api('/api/color?key='+key+'&val='+encodeURIComponent(val)); }
function setBrightness(type, val) { api('/api/brightness?type='+type+'&val='+val, 'parlaklik: '+val); }

function setMode(m) {
  document.getElementById('btn_mode0').classList.add('active');
  api('/api/mode?val='+m, 'normal moda donuldu');
}

function nightMode(val) {
  ['night_on','night_off','night_auto'].forEach(id =>
    document.getElementById('btn_'+id).classList.remove('active'));
  document.getElementById('btn_night_'+val).classList.add('active');
  api('/api/night?val='+val, 'gece: '+val);
}

function triggerAnim(type) {
  ANIM_TYPES.forEach(t => document.getElementById('anim_'+t).classList.remove('running'));
  document.getElementById('anim_'+type).classList.add('running');
  document.getElementById('btn_anim_stop').style.display = 'block';
  document.getElementById('btn_mode0').classList.remove('active');
  api('/api/anim?type='+type, type+' baslatildi', () => setTimeout(fetchStatus, 300));
}

function stopAnim() {
  ANIM_TYPES.forEach(t => document.getElementById('anim_'+t).classList.remove('running'));
  document.getElementById('btn_anim_stop').style.display = 'none';
  document.getElementById('btn_mode0').classList.add('active');
  api('/api/anim/stop', 'normal saate donuldu', () => setTimeout(fetchStatus, 200));
}

function ntpSync() {
  const btn = document.getElementById('btn_ntp');
  btn.textContent = 'syncing...'; btn.disabled = true;
  fetch('/api/ntp', {signal: AbortSignal.timeout(15000)})
    .then(() => { toast('ntp sync tamam'); fetchStatus(); })
    .catch(() => toast('ntp hatasi'))
    .finally(() => { btn.textContent = 'ntp sync'; btn.disabled = false; });
}

function setAlarm(enable) {
  alarmEditing = false;
  clearTimeout(alarmBlurTimer);
  document.getElementById('alarm-section').classList.remove('editing');
  const hint = document.getElementById('alarm-hint');
  hint.textContent = 'saat ve dakika gir, aktif et\'e bas';
  hint.classList.remove('active');
  const h = document.getElementById('alarm_h').value;
  const m = document.getElementById('alarm_m').value;
  if (enable && (h===''||m==='')) { toast('saat ve dakika girin'); return; }
  api('/api/alarm?h='+h+'&m='+m+'&en='+(enable?1:0),
    enable ? 'alarm: '+String(h).padStart(2,'0')+':'+String(m).padStart(2,'0') : 'alarm kapatildi',
    () => setTimeout(fetchStatus, 200));
}

function stopAlarmBtn() {
  api('/api/alarm/stop', 'alarm durduruldu', () => setTimeout(fetchStatus, 200));
}

function setTime() {
  const h = document.getElementById('set_hour').value;
  const m = document.getElementById('set_min').value;
  if (h===''||m==='') { toast('saat ve dakika girin'); return; }
  api('/api/settime?h='+h+'&m='+m, 'saat ayarlandi');
}

fetchStatus();
setInterval(fetchStatus, 2000);
</script>
</body>
</html>
)rawhtml";


void handleRoot() { server.send_P(200, "text/html", HTML_PAGE); }

void handleApiStatus() {
    DateTime now = rtc.now();
    char timeBuf[9];
    sprintf(timeBuf, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
    unsigned long sinceSync = lastNtpSync ? (millis() - lastNtpSync) / 60000 : 0;

    String modeName;
    switch(currentMode) {
        case 0: modeName = "normal";    break;
        case 1: modeName = "saat basi"; break;
        case 2: modeName = "alarm";     break;
        case 3: modeName = "nefes";     break;
        case 4: modeName = "meteor";    break;
        case 5: modeName = "radar";     break;
        default: modeName = "normal";
    }

    String json = "{";
    json += "\"time\":\""        + String(timeBuf)                   + "\",";
    json += "\"ip\":\""          + WiFi.localIP().toString()          + "\",";
    json += "\"mode_num\":"      + String(currentMode)                + ",";
    json += "\"mode\":\""        + modeName                           + "\",";
    json += "\"night\":\""       + String(isNightMode?"on":"off")     + "\",";
    json += "\"auto_night\":"    + String(autoNight?"true":"false")   + ",";
    json += "\"ntp_ok\":"        + String(lastNtpSync?"true":"false") + ",";
    json += "\"last_ntp\":\""    + (lastNtpSync ? String(sinceSync)+"dk" : String("-")) + "\",";
    json += "\"c_saat\":\""      + rgbToHex(c_saat_r,c_saat_g,c_saat_b) + "\",";
    json += "\"c_dol\":\""       + rgbToHex(c_dol_r, c_dol_g, c_dol_b)  + "\",";
    json += "\"c_uc\":\""        + rgbToHex(c_uc_r,  c_uc_g,  c_uc_b)   + "\",";
    json += "\"c_ana\":\""       + rgbToHex(c_ana_r, c_ana_g, c_ana_b)  + "\",";
    json += "\"c_ara\":\""       + rgbToHex(c_ara_r, c_ara_g, c_ara_b)  + "\",";
    json += "\"br_day\":"        + String(brightness_day)             + ",";
    json += "\"br_night\":"      + String(brightness_night)           + ",";
    json += "\"alarm_h\":"       + String(myAlarm.hour)               + ",";
    json += "\"alarm_m\":"       + String(myAlarm.minute)             + ",";
    json += "\"alarm_enabled\":" + String(myAlarm.enabled?"true":"false") + ",";
    json += "\"alarm_firing\":"  + String(alarmFiring?"true":"false");
    json += "}";
    server.send(200, "application/json", json);
}

void handleApiAnim() {
    if (!server.hasArg("type")) { server.send(400); return; }
    String type = server.arg("type");
    if      (type == "hour")   { currentMode=1; animRunning=true; animStep=0; lastAnimUpdate=millis(); }
    else if (type == "alarm")  { triggerAlarm(); }
    else if (type == "breath") { startExtraAnim(3); }
    else if (type == "meteor") { startExtraAnim(4); }
    else if (type == "radar")  { startExtraAnim(5); }
    server.send(200);
}

void handleApiAnimStop() {
    stopAlarmNow();
    server.send(200);
}

void handleApiAlarm() {
    if (!server.hasArg("en")) { server.send(400); return; }
    myAlarm.enabled = server.arg("en").toInt() == 1;
    if (myAlarm.enabled && server.hasArg("h") && server.hasArg("m")) {
        myAlarm.hour   = server.arg("h").toInt();
        myAlarm.minute = server.arg("m").toInt();
    }
    server.send(200);
}

void handleApiAlarmStop() {
    stopAlarmNow();
    myAlarm.enabled = false;
    server.send(200);
}

void handleApiColor() {
    if (!server.hasArg("key") || !server.hasArg("val")) { server.send(400); return; }
    uint8_t r, g, b; hexToRgb(server.arg("val"), r, g, b);
    String key = server.arg("key");
    if      (key=="saat") { c_saat_r=r; c_saat_g=g; c_saat_b=b; }
    else if (key=="dol")  { c_dol_r=r;  c_dol_g=g;  c_dol_b=b;  }
    else if (key=="uc")   { c_uc_r=r;   c_uc_g=g;   c_uc_b=b;   }
    else if (key=="ana")  { c_ana_r=r;  c_ana_g=g;  c_ana_b=b;  }
    else if (key=="ara")  { c_ara_r=r;  c_ara_g=g;  c_ara_b=b;  }
    server.send(200);
}

void handleApiBrightness() {
    if (!server.hasArg("type") || !server.hasArg("val")) { server.send(400); return; }
    uint8_t val = server.arg("val").toInt();
    if (server.arg("type")=="day") { brightness_day=val;   if (!isNightMode) strip.setBrightness(val); }
    else                           { brightness_night=val; if (isNightMode)  strip.setBrightness(val); }
    server.send(200);
}

void handleApiMode() {
    if (!server.hasArg("val")) { server.send(400); return; }
    int m = server.arg("val").toInt();
    if (m == 0) stopAlarmNow();
    else { currentMode=m; animRunning=true; animStep=0; lastAnimUpdate=millis(); }
    server.send(200);
}

void handleApiNight() {
    if (!server.hasArg("val")) { server.send(400); return; }
    String val = server.arg("val");
    if (val=="on")   { isNightMode=true;  autoNight=false; strip.setBrightness(brightness_night); }
    if (val=="off")  { isNightMode=false; autoNight=false; strip.setBrightness(brightness_day);   }
    if (val=="auto") { autoNight=true; checkDayNight(rtc.now().hour()); }
    server.send(200);
}

void handleApiNtp() { syncNTP(); server.send(200); }

void handleApiSetTime() {
    if (!server.hasArg("h") || !server.hasArg("m")) { server.send(400); return; }
    DateTime now = rtc.now();
    rtc.adjust(DateTime(now.year(), now.month(), now.day(),
                        server.arg("h").toInt(), server.arg("m").toInt(), 0));
    server.send(200);
}

// =====================================================================
// SETUP & LOOP
// =====================================================================

void setup() {
    Serial.begin(115200);

    pinMode(BUTTON_HOUR_UP,   INPUT_PULLUP);
    pinMode(BUTTON_HOUR_DOWN, INPUT_PULLUP);
    pinMode(BUTTON_MIN_UP,    INPUT_PULLUP);
    pinMode(BUTTON_MIN_DOWN,  INPUT_PULLUP);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.printf("\nIP: %s\n", WiFi.localIP().toString().c_str());

    ArduinoOTA.setHostname("analog-clock-ESP32");
    ArduinoOTA.onEnd([]()                { Serial.println("OTA tamam."); });
    ArduinoOTA.onError([](ota_error_t e) { Serial.printf("OTA hata [%u]\n", e); });
    ArduinoOTA.begin();

    server.on("/",               handleRoot);
    server.on("/api/status",     handleApiStatus);
    server.on("/api/anim",       handleApiAnim);
    server.on("/api/anim/stop",  handleApiAnimStop);
    server.on("/api/alarm",      handleApiAlarm);
    server.on("/api/alarm/stop", handleApiAlarmStop);
    server.on("/api/color",      handleApiColor);
    server.on("/api/brightness", handleApiBrightness);
    server.on("/api/mode",       handleApiMode);
    server.on("/api/night",      handleApiNight);
    server.on("/api/ntp",        handleApiNtp);
    server.on("/api/settime",    handleApiSetTime);
    server.begin();

    if (!rtc.begin()) { Serial.println("RTC yok!"); while (1); }
    if (rtc.lostPower()) Serial.println("RTC guc kaybetti.");

    strip.begin();
    strip.setBrightness(brightness_day);
    strip.show();

    syncNTP();
    lastHour = rtc.now().hour();
    checkDayNight(lastHour);

    Serial.println("Ready.");
}

void loop() {
    ArduinoOTA.handle();
    server.handleClient();

    DateTime now = rtc.now();

    if (millis() - lastNtpSync >= NTP_INTERVAL) syncNTP();

    checkAlarm(now);

    if (!alarmFiring && currentMode == 0 &&
        now.minute() == 0 && now.second() == 0 && now.hour() != lastHour) {
        lastHour       = now.hour();
        currentMode    = 1;
        animRunning    = true;
        animStep       = 0;
        lastAnimUpdate = millis();
    }
    if (now.hour() != lastHour && !(now.minute()==0 && now.second()==0))
        lastHour = now.hour();

    static unsigned long lastNightCheck = 0;
    if (millis() - lastNightCheck >= 60000) {
        lastNightCheck = millis();
        checkDayNight(now.hour());
    }

    if (currentMode == 0 && !alarmFiring) {
        static unsigned long lastButtonCheck = 0;
        if (millis() - lastButtonCheck >= 50) {
            lastButtonCheck = millis();
            if (digitalRead(BUTTON_HOUR_UP)   == LOW) adjustTime(1,  0);
            if (digitalRead(BUTTON_HOUR_DOWN) == LOW) adjustTime(-1, 0);
            if (digitalRead(BUTTON_MIN_UP)    == LOW) adjustTime(0,  1);
            if (digitalRead(BUTTON_MIN_DOWN)  == LOW) adjustTime(0, -1);
        }
    }

    // Öncelik sırası
    if (alarmFiring || currentMode == 2) {
        runAlarmAnimation();
    } else if (currentMode == 1 && animRunning) {
        runHourAnimation();
    } else if (currentMode == 3) {
        runBreathAnimation();
    } else if (currentMode == 4) {
        runMeteorAnimation();
    } else if (currentMode == 5) {
        runRadarAnimation();
    } else {
        currentMode = 0;
        static unsigned long lastUpdate = 0;
        if (millis() - lastUpdate >= 20) {
            lastUpdate = millis();
            updateClockDisplay(now);
        }
    }
}
