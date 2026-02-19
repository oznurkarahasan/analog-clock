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
#define BRIGHTNESS_DAY    20
#define BRIGHTNESS_NIGHT  30

#define NTP_SERVER    "pool.ntp.org"
#define UTC_OFFSET    10800
#define NTP_INTERVAL  86400000

RTC_DS3231 rtc;
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
WebServer server(80);

uint8_t c_saat_r = 0,   c_saat_g = 255, c_saat_b = 0;
uint8_t c_dol_r  = 50,  c_dol_g  = 15,  c_dol_b  = 0;
uint8_t c_uc_r   = 255, c_uc_g   = 40,  c_uc_b   = 0;
uint8_t c_ana_r  = 200, c_ana_g  = 200, c_ana_b  = 200;
uint8_t c_ara_r  = 30,  c_ara_g  = 30,  c_ara_b  = 30;
uint8_t brightness_day   = 20;
uint8_t brightness_night = 30;

int  currentMode  = 0;
int  lastHour     = -1;
bool animRunning  = false;
int  animStep     = 0;
unsigned long lastAnimUpdate = 0;
bool isNightMode  = false;
bool autoNight    = true;
unsigned long lastNtpSync = 0;

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
    int hrPos = (hr * 5) + (mn / 12);

    for (int i = 0; i <= mn; i++)
        strip.setPixelColor(i, strip.Color(c_dol_r, c_dol_g, c_dol_b));

    for (int i = 0; i < 60; i += 5) {
        if (i == 0 || i == 15 || i == 30 || i == 45)
            strip.setPixelColor(i, strip.Color(c_ana_r, c_ana_g, c_ana_b));
        else
            strip.setPixelColor(i, strip.Color(c_ara_r, c_ara_g, c_ara_b));
    }

    uint8_t br = breathCalc();
    float ratio_g = c_uc_g / (float)c_uc_r;
    float ratio_b = c_uc_b / (float)(c_uc_r + 1);
    strip.setPixelColor(mn, strip.Color(br, (uint8_t)(br * ratio_g), (uint8_t)(br * ratio_b)));

    if (millis() % 2000 < 1900)
        strip.setPixelColor(hrPos, strip.Color(c_saat_r, c_saat_g, c_saat_b));
    else
        strip.setPixelColor(hrPos, strip.Color(0, 0, 0));

    strip.setPixelColor(sc, Wheel((millis() / 15) & 255));
    strip.show();
}

bool syncNTP() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("NTP: WiFi bagli degil.");
        return false;
    }
    Serial.println("NTP sync...");
    configTime(UTC_OFFSET, 0, NTP_SERVER);
    struct tm timeinfo;
    int retry = 0;
    while (!getLocalTime(&timeinfo) && retry < 10) { delay(500); retry++; }
    if (retry >= 10) { Serial.println("NTP basarisiz!"); return false; }
    rtc.adjust(DateTime(timeinfo.tm_year+1900, timeinfo.tm_mon+1, timeinfo.tm_mday,
                        timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec));
    lastNtpSync = millis();
    Serial.printf("NTP OK → %02d:%02d:%02d\n", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    return true;
}

void checkDayNight(int hour) {
    if (!autoNight) return;
    bool shouldBeNight = (hour >= NIGHT_START_HOUR || hour < DAY_START_HOUR);
    if (shouldBeNight && !isNightMode) {
        isNightMode = true;
        strip.setBrightness(brightness_night);
        Serial.println("Gece moduna gecildi.");
    } else if (!shouldBeNight && isNightMode) {
        isNightMode = false;
        strip.setBrightness(brightness_day);
        Serial.println("Gunduz moduna gecildi.");
    }
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
        animRunning = false; animStep = 0; currentMode = 0;
        strip.setBrightness(isNightMode ? brightness_night : brightness_day);
        Serial.println("Animasyon bitti.");
    }
}

void hexToRgb(String hex, uint8_t &r, uint8_t &g, uint8_t &b) {
    if (hex.startsWith("#")) hex = hex.substring(1);
    r = strtol(hex.substring(0,2).c_str(), NULL, 16);
    g = strtol(hex.substring(2,4).c_str(), NULL, 16);
    b = strtol(hex.substring(4,6).c_str(), NULL, 16);
}

String rgbToHex(uint8_t r, uint8_t g, uint8_t b) {
    char buf[8];
    sprintf(buf, "#%02x%02x%02x", r, g, b);
    return String(buf);
}

void handleRoot() {
    DateTime now = rtc.now();
    String html = R"rawhtml(
<!DOCTYPE html>
<html lang="tr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Analog Saat</title>
<style>
  @import url('https://fonts.googleapis.com/css2?family=DM+Mono:wght@300;400;500&display=swap');
  *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
  :root {
    --bg: #0e0e0e; --surface: #161616; --border: #2a2a2a;
    --text: #e8e8e8; --muted: #666; --accent: #fff;
  }
  body { background: var(--bg); color: var(--text); font-family: 'DM Mono', monospace;
         min-height: 100vh; padding: 2rem 1rem; }
  .container { max-width: 480px; margin: 0 auto; }
  h1 { font-size: 0.75rem; letter-spacing: 0.2em; color: var(--muted);
       text-transform: uppercase; margin-bottom: 2rem; }
  .time-display { font-size: 3.5rem; font-weight: 300; letter-spacing: -0.02em;
                  margin-bottom: 2.5rem; color: var(--accent); }
  .section { border: 1px solid var(--border); margin-bottom: 1px; }
  .section-header { padding: 0.75rem 1rem; font-size: 0.65rem; letter-spacing: 0.15em;
                    text-transform: uppercase; color: var(--muted); background: var(--surface); }
  .section-body { padding: 1rem; display: flex; flex-direction: column; gap: 0.75rem; }
  .row { display: flex; align-items: center; justify-content: space-between; gap: 1rem; }
  .label { font-size: 0.7rem; color: var(--muted); letter-spacing: 0.05em; flex: 1; }
  input[type="color"] { width: 2.5rem; height: 2rem; border: 1px solid var(--border);
                        background: none; cursor: pointer; padding: 2px; border-radius: 2px; }
  input[type="range"] { flex: 1; accent-color: var(--accent); }
  .range-val { font-size: 0.7rem; color: var(--muted); width: 2rem; text-align: right; }
  .btn-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 1px; }
  button { background: var(--surface); border: 1px solid var(--border); color: var(--text);
           padding: 0.6rem 1rem; font-family: 'DM Mono', monospace; font-size: 0.7rem;
           letter-spacing: 0.08em; cursor: pointer; transition: background 0.15s; }
  button:hover { background: var(--border); }
  button.active { border-color: var(--accent); color: var(--accent); }
  button.full { grid-column: 1 / -1; }
  .status { font-size: 0.65rem; color: var(--muted); margin-top: 1.5rem;
            letter-spacing: 0.05em; line-height: 1.8; }
  .time-inputs { display: grid; grid-template-columns: 1fr 1fr; gap: 1px; }
  input[type="number"] { background: var(--surface); border: 1px solid var(--border);
                         color: var(--text); padding: 0.5rem; font-family: 'DM Mono', monospace;
                         font-size: 0.8rem; width: 100%; text-align: center; }
  input[type="number"]:focus { outline: 1px solid var(--accent); }
  .toast { position: fixed; bottom: 1.5rem; left: 50%; transform: translateX(-50%);
           background: var(--accent); color: var(--bg); padding: 0.5rem 1.2rem;
           font-size: 0.7rem; letter-spacing: 0.1em; opacity: 0; transition: opacity 0.3s;
           pointer-events: none; }
  .toast.show { opacity: 1; }
</style>
</head>
<body>
<div class="container">
  <h1>analog saat kontrolü</h1>
  <div class="time-display" id="clock">)rawhtml";

    html += String(now.hour() < 10 ? "0" : "") + now.hour() + ":" +
            String(now.minute() < 10 ? "0" : "") + now.minute() + ":" +
            String(now.second() < 10 ? "0" : "") + now.second();

    html += R"rawhtml(</div>

  <div class="section">
    <div class="section-header">Renkler</div>
    <div class="section-body">
      <div class="row">
        <span class="label">saat göstergesi</span>
        <input type="color" id="c_saat" onchange="setColor('saat',this.value)">
      </div>
      <div class="row">
        <span class="label">dakika dolgu</span>
        <input type="color" id="c_dol" onchange="setColor('dol',this.value)">
      </div>
      <div class="row">
        <span class="label">dakika ucu</span>
        <input type="color" id="c_uc" onchange="setColor('uc',this.value)">
      </div>
      <div class="row">
        <span class="label">ana işaret (12/3/6/9)</span>
        <input type="color" id="c_ana" onchange="setColor('ana',this.value)">
      </div>
      <div class="row">
        <span class="label">ara işaret</span>
        <input type="color" id="c_ara" onchange="setColor('ara',this.value)">
      </div>
    </div>
  </div>

  <div class="section">
    <div class="section-header">Parlaklık</div>
    <div class="section-body">
      <div class="row">
        <span class="label">gündüz</span>
        <input type="range" id="br_day" min="5" max="255" onchange="setBrightness('day',this.value)">
        <span class="range-val" id="br_day_val">—</span>
      </div>
      <div class="row">
        <span class="label">gece</span>
        <input type="range" id="br_night" min="5" max="255" onchange="setBrightness('night',this.value)">
        <span class="range-val" id="br_night_val">—</span>
      </div>
    </div>
  </div>

  <div class="section">
    <div class="section-header">Saat Ayarla</div>
    <div class="section-body">
      <div class="time-inputs">
        <input type="number" id="set_hour" min="0" max="23" placeholder="saat">
        <input type="number" id="set_min"  min="0" max="59" placeholder="dakika">
      </div>
      <button onclick="setTime()" style="margin-top:1px">kaydet</button>
    </div>
  </div>

  <div class="section">
    <div class="section-header">Kontroller</div>
    <div class="section-body">
      <div class="btn-grid">
        <button id="btn_mode0" onclick="setMode(0)">normal saat</button>
        <button id="btn_mode1" onclick="setMode(1)">animasyon</button>
        <button id="btn_night_on"  onclick="nightMode('on')">gece: açık</button>
        <button id="btn_night_off" onclick="nightMode('off')">gece: kapalı</button>
        <button id="btn_night_auto" onclick="nightMode('auto')" class="full">gece: otomatik</button>
        <button onclick="ntpSync()" class="full">ntp sync</button>
      </div>
    </div>
  </div>

  <div class="status" id="status">yükleniyor...</div>
</div>
<div class="toast" id="toast"></div>

<script>
function toast(msg) {
  const t = document.getElementById('toast');
  t.textContent = msg;
  t.classList.add('show');
  setTimeout(() => t.classList.remove('show'), 2000);
}

function fetchStatus() {
  fetch('/api/status').then(r => r.json()).then(d => {
    document.getElementById('clock').textContent = d.time;
    document.getElementById('c_saat').value = d.c_saat;
    document.getElementById('c_dol').value  = d.c_dol;
    document.getElementById('c_uc').value   = d.c_uc;
    document.getElementById('c_ana').value  = d.c_ana;
    document.getElementById('c_ara').value  = d.c_ara;
    document.getElementById('br_day').value   = d.br_day;
    document.getElementById('br_night').value = d.br_night;
    document.getElementById('br_day_val').textContent   = d.br_day;
    document.getElementById('br_night_val').textContent = d.br_night;
    document.getElementById('status').innerHTML =
      'mod: ' + d.mode + ' &nbsp;|&nbsp; gece: ' + d.night +
      ' &nbsp;|&nbsp; son ntp: ' + d.last_ntp;
    ['mode0','mode1'].forEach(id => document.getElementById('btn_'+id).classList.remove('active'));
    document.getElementById('btn_mode'+d.mode_num).classList.add('active');
  });
}

function setColor(key, val) {
  fetch('/api/color?key=' + key + '&val=' + encodeURIComponent(val))
    .then(() => toast('renk güncellendi'));
}

function setBrightness(type, val) {
  document.getElementById('br_'+type+'_val').textContent = val;
  fetch('/api/brightness?type=' + type + '&val=' + val)
    .then(() => toast('parlaklık: ' + val));
}

function setMode(m) {
  fetch('/api/mode?val=' + m).then(() => { toast('mod: ' + m); fetchStatus(); });
}

function nightMode(val) {
  fetch('/api/night?val=' + val).then(() => { toast('gece: ' + val); fetchStatus(); });
}

function ntpSync() {
  toast('ntp sync...');
  fetch('/api/ntp').then(() => { toast('sync tamam'); fetchStatus(); });
}

function setTime() {
  const h = document.getElementById('set_hour').value;
  const m = document.getElementById('set_min').value;
  if (h === '' || m === '') { toast('saat ve dakika girin'); return; }
  fetch('/api/settime?h=' + h + '&m=' + m).then(() => { toast('saat ayarlandı'); fetchStatus(); });
}

fetchStatus();
setInterval(fetchStatus, 5000);
</script>
</body>
</html>
)rawhtml";

    server.send(200, "text/html", html);
}

void handleApiStatus() {
    DateTime now = rtc.now();
    char timeBuf[9];
    sprintf(timeBuf, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());

    unsigned long sinceSync = lastNtpSync ? (millis() - lastNtpSync) / 60000 : 0;

    String json = "{";
    json += "\"time\":\"" + String(timeBuf) + "\",";
    json += "\"mode_num\":" + String(currentMode) + ",";
    json += "\"mode\":\"" + String(currentMode == 0 ? "normal" : "animasyon") + "\",";
    json += "\"night\":\"" + String(isNightMode ? "açık" : "kapalı") + "\",";
    json += "\"last_ntp\":\"" + (lastNtpSync ? String(sinceSync) + " dk önce" : String("henüz yok")) + "\",";
    json += "\"c_saat\":\"" + rgbToHex(c_saat_r, c_saat_g, c_saat_b) + "\",";
    json += "\"c_dol\":\""  + rgbToHex(c_dol_r,  c_dol_g,  c_dol_b)  + "\",";
    json += "\"c_uc\":\""   + rgbToHex(c_uc_r,   c_uc_g,   c_uc_b)   + "\",";
    json += "\"c_ana\":\""  + rgbToHex(c_ana_r,  c_ana_g,  c_ana_b)  + "\",";
    json += "\"c_ara\":\""  + rgbToHex(c_ara_r,  c_ara_g,  c_ara_b)  + "\",";
    json += "\"br_day\":"   + String(brightness_day)   + ",";
    json += "\"br_night\":" + String(brightness_night);
    json += "}";

    server.send(200, "application/json", json);
}

void handleApiColor() {
    if (!server.hasArg("key") || !server.hasArg("val")) { server.send(400); return; }
    String key = server.arg("key");
    String val = server.arg("val");
    uint8_t r, g, b;
    hexToRgb(val, r, g, b);
    if      (key == "saat") { c_saat_r=r; c_saat_g=g; c_saat_b=b; }
    else if (key == "dol")  { c_dol_r=r;  c_dol_g=g;  c_dol_b=b;  }
    else if (key == "uc")   { c_uc_r=r;   c_uc_g=g;   c_uc_b=b;   }
    else if (key == "ana")  { c_ana_r=r;  c_ana_g=g;  c_ana_b=b;  }
    else if (key == "ara")  { c_ara_r=r;  c_ara_g=g;  c_ara_b=b;  }
    server.send(200);
}

void handleApiBrightness() {
    if (!server.hasArg("type") || !server.hasArg("val")) { server.send(400); return; }
    uint8_t val = server.arg("val").toInt();
    if (server.arg("type") == "day") {
        brightness_day = val;
        if (!isNightMode) strip.setBrightness(brightness_day);
    } else {
        brightness_night = val;
        if (isNightMode) strip.setBrightness(brightness_night);
    }
    server.send(200);
}

void handleApiMode() {
    if (!server.hasArg("val")) { server.send(400); return; }
    int m = server.arg("val").toInt();
    currentMode = m;
    if (m == 1) { animRunning = true; animStep = 0; }
    else { animRunning = false; strip.setBrightness(isNightMode ? brightness_night : brightness_day); }
    server.send(200);
}

void handleApiNight() {
    if (!server.hasArg("val")) { server.send(400); return; }
    String val = server.arg("val");
    if (val == "on")   { isNightMode=true;  autoNight=false; strip.setBrightness(brightness_night); }
    if (val == "off")  { isNightMode=false; autoNight=false; strip.setBrightness(brightness_day);   }
    if (val == "auto") { autoNight=true; checkDayNight(rtc.now().hour()); }
    server.send(200);
}

void handleApiNtp() {
    syncNTP();
    server.send(200);
}

void handleApiSetTime() {
    if (!server.hasArg("h") || !server.hasArg("m")) { server.send(400); return; }
    int h = server.arg("h").toInt();
    int m = server.arg("m").toInt();
    DateTime now = rtc.now();
    rtc.adjust(DateTime(now.year(), now.month(), now.day(), h, m, 0));
    Serial.printf("Web'den saat ayarlandi: %02d:%02d\n", h, m);
    server.send(200);
}

void handleSerialCommands() {
    if (!Serial.available()) return;
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if      (cmd == "mode0")      { currentMode=0; animRunning=false; strip.setBrightness(isNightMode?brightness_night:brightness_day); Serial.println("Mod: Normal"); }
    else if (cmd == "mode1")      { currentMode=1; animRunning=true; animStep=0; Serial.println("Mod: Animasyon"); }
    else if (cmd == "night on")   { isNightMode=true;  autoNight=false; strip.setBrightness(brightness_night); Serial.println("Gece: ACIK"); }
    else if (cmd == "night off")  { isNightMode=false; autoNight=false; strip.setBrightness(brightness_day);   Serial.println("Gece: KAPALI"); }
    else if (cmd == "night auto") { autoNight=true; checkDayNight(rtc.now().hour()); Serial.println("Gece: OTOMATIK"); }
    else if (cmd == "ntp sync")   { syncNTP(); }
    else if (cmd == "status") {
        DateTime now = rtc.now();
        Serial.printf("Saat: %02d:%02d:%02d | Mod: %d | Gece: %s | IP: %s\n",
            now.hour(), now.minute(), now.second(), currentMode,
            isNightMode?"ACIK":"KAPALI", WiFi.localIP().toString().c_str());
    }
    else Serial.println("Komutlar: mode0, mode1, night on/off/auto, ntp sync, status");
}

void setup() {
    Serial.begin(115200);

    pinMode(BUTTON_HOUR_UP,   INPUT_PULLUP);
    pinMode(BUTTON_HOUR_DOWN, INPUT_PULLUP);
    pinMode(BUTTON_MIN_UP,    INPUT_PULLUP);
    pinMode(BUTTON_MIN_DOWN,  INPUT_PULLUP);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.printf("\nWiFi baglandi! IP: %s\n", WiFi.localIP().toString().c_str());

    ArduinoOTA.setHostname("analog-clock-ESP32");
    ArduinoOTA.onEnd([]() { Serial.println("\nOTA tamamlandi."); });
    ArduinoOTA.onError([](ota_error_t error) { Serial.printf("OTA Hata [%u]\n", error); });
    ArduinoOTA.begin();

    // Web server rotaları
    server.on("/",                handleRoot);
    server.on("/api/status",      handleApiStatus);
    server.on("/api/color",       handleApiColor);
    server.on("/api/brightness",  handleApiBrightness);
    server.on("/api/mode",        handleApiMode);
    server.on("/api/night",       handleApiNight);
    server.on("/api/ntp",         handleApiNtp);
    server.on("/api/settime",     handleApiSetTime);
    server.begin();
    Serial.println("Web sunucu baslatildi.");

    if (!rtc.begin()) { Serial.println("RTC bulunamadi!"); while (1); }
    if (rtc.lostPower()) Serial.println("RTC guc kaybetti, NTP ile ayarlaniyor...");

    strip.begin();
    strip.setBrightness(brightness_day);
    strip.show();

    syncNTP();
    lastHour = rtc.now().hour();
    checkDayNight(lastHour);

    Serial.println("System ready!");
}

void loop() {
    ArduinoOTA.handle();
    server.handleClient();
    handleSerialCommands();

    DateTime now = rtc.now();

    if (millis() - lastNtpSync >= NTP_INTERVAL) syncNTP();

    if (now.minute() == 0 && now.second() == 0 && now.hour() != lastHour) {
        lastHour = now.hour(); currentMode=1; animRunning=true; animStep=0;
        Serial.printf("Saat basi: %02d:00\n", lastHour);
    }

    static unsigned long lastNightCheck = 0;
    if (millis() - lastNightCheck >= 60000) { lastNightCheck=millis(); checkDayNight(now.hour()); }

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
        if (millis() - lastUpdate >= 20) { lastUpdate=millis(); updateClockDisplay(now); }
    }
}
