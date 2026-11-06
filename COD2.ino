#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <DFRobot_BMI160.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "time.h"
#include <math.h>

#define LM35_PIN 34     // پایه سنسور دمای LM35
#define BATTERY_PIN 35  // پایه خواندن ولتاژ باتری
#define BUZZER_PIN 25   // پایه بازر پسیو (Passive Buzzer)
#define VIBRO_PIN 26    // پایه موتور ویبره برای ضربان قلب و شوک بازی

#ifdef S
#undef S
#endif

#include <FluxGarage_RoboEyes.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

RoboEyes<Adafruit_SSD1306> roboEyes(display);
DFRobot_BMI160 bmi160;

WebServer server(80);
DNSServer dnsServer;
bool isAPRunning = false;

const int touchPin = 4;
unsigned long touchStartTime = 0;
bool isHoldingTouch = false;
int currentMode = 0; // 0: Eyes, 1: Clock & Alarm, 2: Telemetry, 3: Dino Game, 4: Car Game

unsigned long lastMoveTime = 0;
unsigned long jumpTimer = 0;
int moveInterval = 6000;
int currentStep = 0;
int lastDir = 0;
bool wasTouched = false;
bool wasAngrySoundPlayed = false;

const char* ntpServer = "pool.ntp.org";

// ================= متغیرهای حالت اسلیپ و مدیریت مصرف انرژی =================
const unsigned long INACTIVITY_TIMEOUT = 120000UL; // ۲ دقیقه (۱۲۰ ثانیه) عدم فعالیت
unsigned long lastActivityTime = 0;
bool isSleeping = false;
float prevAx = 0, prevAy = 0, prevAz = 1.0;

// ================= متغیرهای تشخیص ربات جفت، ضربان قلب و دلتنگی =================
const char* PEER_SSID = "PHOENIX_LINK";
const int PROXIMITY_RSSI = -48; 
bool isFriendNearby = false;
bool wasFriendNearby = false;
unsigned long lastPeerScan = 0;
unsigned long lastHeartFrame = 0;
unsigned long peerCloseStartTime = 0;
unsigned long sadModeEndTime = 0;
unsigned long lastWinkTime = 0;
bool winkLeft = false;

// ================= متغیرهای آلارم و وب‌پورتال =================
bool alarmActive = false;
bool alarmRinging = false;
unsigned long alarmTargetMillis = 0;
unsigned long lastAlarmBeep = 0;
bool alarmBeepState = false;

// لوگوی دقیق نئونی GAMEPHOENIX مطابق تصویر آپلود شده شما
const char* PHOENIX_LOGO_SVG = 
  "<div class='logo-box'>"
  "<svg viewBox='0 0 240 240' width='135' height='135' xmlns='http://www.w3.org/2000/svg'>"
  "<defs>"
  "<linearGradient id='nG' x1='0%' y1='100%' x2='100%' y2='0%'>"
  "<stop offset='0%' stop-color='#00d2ff'/>"
  "<stop offset='28%' stop-color='#d836ff'/>"
  "<stop offset='58%' stop-color='#ff2a2a'/>"
  "<stop offset='82%' stop-color='#ff7b00'/>"
  "<stop offset='100%' stop-color='#ffe600'/>"
  "</linearGradient>"
  "<linearGradient id='tG' x1='0%' y1='0%' x2='100%' y2='0%'>"
  "<stop offset='0%' stop-color='#00d2ff'/>"
  "<stop offset='35%' stop-color='#b836ff'/>"
  "<stop offset='68%' stop-color='#ff3300'/>"
  "<stop offset='100%' stop-color='#ffcc00'/>"
  "</linearGradient>"
  "<filter id='glow' x='-25%' y='-25%' width='150%' height='150%'>"
  "<feGaussianBlur stdDeviation='3.5' result='b'/>"
  "<feMerge><feMergeNode in='b'/><feMergeNode in='b'/><feMergeNode in='SourceGraphic'/></feMerge>"
  "</filter>"
  "</defs>"
  "<g filter='url(#glow)' fill='none' stroke='url(#nG)' stroke-width='4.5' stroke-linecap='round' stroke-linejoin='round'>"
  "<path d='M 120 185 C 78 162, 50 128, 48 82 C 62 96, 75 102, 86 102 C 66 84, 55 60, 58 38 C 72 56, 85 68, 96 72 C 78 48, 76 26, 86 12 C 92 40, 108 66, 128 84'/>"
  "<path d='M 86 102 C 75 112, 68 118, 86 120 C 78 136, 92 156, 120 185 C 154 162, 188 126, 188 70 C 180 86, 172 94, 164 98 C 178 104, 182 110, 168 122'/>"
  "<path d='M 128 84 C 110 68, 114 48, 126 38 C 118 36, 110 30, 106 20 C 122 26, 146 24, 162 38 C 170 46, 174 54, 178 64 C 166 56, 152 58, 145 72 C 138 92, 164 115, 146 144 C 138 158, 126 168, 118 172'/>"
  "<polygon points='148,45 157,50 150,52' fill='url(#nG)' stroke='none'/>"
  "</g>"
  "<text x='120' y='222' text-anchor='middle' font-family='Arial,Tahoma,sans-serif' font-weight='900' font-size='23' letter-spacing='1.5' fill='url(#tG)' filter='url(#glow)'>GAMEPHOENIX</text>"
  "</svg></div>";

// ================= متغیرهای بازی دایناسور (مود ۴) =================
float dinoY = 42;
float dinoVy = 0;
const float gravity = 0.65;
const float jumpPower = -7.5;
bool isJumping = false;
float obstacleX = 128;
int obstacleType = 0;
float gameSpeed = 3.5;
unsigned long dinoScore = 0;
unsigned long dinoHighScore = 0;
bool dinoGameOver = false;
unsigned long lastGameTick = 0;
bool lastTouchForDino = false;

const unsigned char PROGMEM dinoSprite[] = {
  0x00, 0xfe, 0x01, 0xff, 0x01, 0xdf, 0x01, 0xff, 0x01, 0xff, 0x01, 0xc0, 0x01, 0xf8, 0x83, 0xc0,
  0x87, 0xf0, 0xcf, 0xd0, 0xff, 0xc0, 0x7f, 0xc0, 0x3f, 0x80, 0x1f, 0x00, 0x12, 0x00, 0x1b, 0x00
};

const unsigned char PROGMEM bellIcon[] = {
  0x18, 0x3c, 0x3c, 0x3c, 0x7e, 0xff, 0x18, 0x00
};

// ================= متغیرهای ماشین‌بازی با ژیروسکوپ (مود ۵) =================
float carX = 58;
float enemyX = 58;
float enemyY = -15;
float carSpeed = 2.8;
int roadOffset = 0;
unsigned long carScore = 0;
unsigned long carHighScore = 0;
bool carGameOver = false;
unsigned long lastCarTick = 0;
bool lastTouchForCar = false;

// ================= توابع افکت‌های صوتی و ویبره =================
void playTone(int freq, int duration) {
  tone(BUZZER_PIN, freq, duration);
}

void soundBoot() {
  tone(BUZZER_PIN, 523, 80); delay(90);
  tone(BUZZER_PIN, 659, 80); delay(90);
  tone(BUZZER_PIN, 784, 80); delay(90);
  tone(BUZZER_PIN, 1046, 150); delay(150);
  noTone(BUZZER_PIN);
}

void soundModeSwitch() {
  tone(BUZZER_PIN, 1200, 50);
  delay(60);
  tone(BUZZER_PIN, 1800, 70);
}

void soundHappyChirp() {
  for (int f = 800; f < 2200; f += 250) {
    tone(BUZZER_PIN, f, 15);
    delay(15);
  }
  for (int f = 2200; f > 1200; f -= 300) {
    tone(BUZZER_PIN, f, 15);
    delay(15);
  }
  noTone(BUZZER_PIN);
}

void soundAngry() {
  tone(BUZZER_PIN, 220, 90);
  delay(100);
  tone(BUZZER_PIN, 160, 140);
}

void soundJump() {
  tone(BUZZER_PIN, 950, 45);
}

void soundScore() {
  tone(BUZZER_PIN, 1600, 40);
}

void soundGameOver() {
  digitalWrite(VIBRO_PIN, HIGH);
  tone(BUZZER_PIN, 500, 110); delay(120);
  tone(BUZZER_PIN, 380, 110); delay(120);
  digitalWrite(VIBRO_PIN, LOW);
  tone(BUZZER_PIN, 260, 250); delay(250);
  noTone(BUZZER_PIN);
}

void soundCrash() {
  digitalWrite(VIBRO_PIN, HIGH);
  for (int i = 0; i < 10; i++) {
    tone(BUZZER_PIN, random(120, 450), 25);
    delay(25);
  }
  digitalWrite(VIBRO_PIN, LOW);
  noTone(BUZZER_PIN);
}

void soundAlarmSet() {
  tone(BUZZER_PIN, 880, 70); delay(80);
  tone(BUZZER_PIN, 1320, 120);
}

// ================= توابع سنسورها و وضعیت =================
float readPreciseLM35() {
  long sumMilliVolts = 0;
  for (int i = 0; i < 20; i++) {
    sumMilliVolts += analogReadMilliVolts(LM35_PIN);
    delay(2);
  }
  return (sumMilliVolts / 20.0) / 10.0; 
}

int getBatteryPercentage() {
  long sumMilliVolts = 0;
  for (int i = 0; i < 10; i++) {
    sumMilliVolts += analogReadMilliVolts(BATTERY_PIN);
  }
  float pinMilliVolts = sumMilliVolts / 10.0;
  if (pinMilliVolts < 500) return 100;

  float batteryMilliVolts = pinMilliVolts * 2.0; 
  int percent = map((long)batteryMilliVolts, 3300, 4200, 0, 100);
  if (percent > 100) percent = 100;
  if (percent < 0) percent = 0;
  return percent;
}

int getWiFiSignalPercent() {
  if (WiFi.status() != WL_CONNECTED) return 0;
  int rssi = WiFi.RSSI();
  int percent = map(rssi, -100, -50, 0, 100);
  if (percent > 100) percent = 100;
  if (percent < 0) percent = 0;
  return percent;
}

// بررسی حرکت یا تکان خوردن ربات با سنسور BMI160
bool checkMotionActivity() {
  int16_t accelGyro[6] = {0};
  if (bmi160.getAccelGyroData(accelGyro) == BMI160_OK) {
    float ax = accelGyro[3] / 16384.0;
    float ay = accelGyro[4] / 16384.0;
    float az = accelGyro[5] / 16384.0;
    float diff = fabs(ax - prevAx) + fabs(ay - prevAy) + fabs(az - prevAz);
    prevAx = ax; prevAy = ay; prevAz = az;
    if (diff > 0.22) { // آستانه تشخیص تکان خوردن ربات
      return true;
    }
  }
  return false;
}

// ================= گرافیک‌های تجاری نوار وضعیت OLED =================
void drawWiFiIcon(int x, int y, int percent) {
  int bars = 0;
  if (percent > 75) bars = 4;
  else if (percent > 50) bars = 3;
  else if (percent > 25) bars = 2;
  else if (percent > 0) bars = 1;

  for (int i = 0; i < 4; i++) {
    int h = (i + 1) * 2;
    if (i < bars) display.fillRect(x + (i * 4), y + (8 - h), 2, h, WHITE);
    else display.drawRect(x + (i * 4), y + (8 - h), 2, h, WHITE);
  }
}

void drawBatteryIcon(int x, int y, int percent) {
  display.drawRect(x, y, 16, 8, WHITE);
  display.fillRect(x + 16, y + 2, 2, 4, WHITE);
  int fillW = map(percent, 0, 100, 0, 12);
  if (fillW > 0) display.fillRect(x + 2, y + 2, fillW, 4, WHITE);
}

void drawTopBar(const char* title) {
  drawWiFiIcon(2, 1, getWiFiSignalPercent());
  display.setTextSize(1);
  int textWidth = strlen(title) * 6;
  display.setCursor((128 - textWidth) / 2, 1);
  display.print(title);
  drawBatteryIcon(108, 1, getBatteryPercentage());
  display.drawFastHLine(0, 11, 128, WHITE);
}

// ================= توابع تعامل با ربات جفت =================
void startPeerBeacon() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(PEER_SSID, "phoenix123", 1, 1, 2);
}

void drawSingleHeart(int cx, int cy, int r) {
  display.fillCircle(cx - r / 2, cy - r / 3, r / 2, WHITE);
  display.fillCircle(cx + r / 2, cy - r / 3, r / 2, WHITE);
  display.fillTriangle(cx - r, cy - r / 6, cx + r, cy - r / 6, cx, cy + r, WHITE);
}

void handlePeerInteractionStages(unsigned long now, unsigned long durationTogether) {
  noTone(BUZZER_PIN);

  if (durationTogether < 3000) {
    digitalWrite(VIBRO_PIN, LOW);
    if (now - lastHeartFrame > 45) {
      lastHeartFrame = now;
      display.clearDisplay();
      bool pulse = ((now / 180) % 2 == 0);
      
      display.drawRoundRect(6, 6, 116, 52, 8, WHITE);
      if (pulse) {
        display.fillRoundRect(16, 16, 96, 32, 6, WHITE);
        display.setTextColor(BLACK);
      } else {
        display.drawRoundRect(16, 16, 96, 32, 6, WHITE);
        display.setTextColor(WHITE);
      }
      display.setTextSize(3);
      display.setCursor(29, 21);
      display.print("LOVE");
      display.setTextColor(WHITE);
      display.display();
    }
  }
  else if (durationTogether < 15000) {
    unsigned long phase = now % 1000;
    bool heartZoom = false;

    if (phase < 120) {
      digitalWrite(VIBRO_PIN, HIGH);
      heartZoom = true;
    } 
    else if (phase >= 120 && phase < 230) {
      digitalWrite(VIBRO_PIN, LOW);
      heartZoom = false;
    } 
    else if (phase >= 230 && phase < 350) {
      digitalWrite(VIBRO_PIN, HIGH);
      heartZoom = true;
    } 
    else {
      digitalWrite(VIBRO_PIN, LOW);
      heartZoom = false;
    }

    if (now - lastHeartFrame > 35) {
      lastHeartFrame = now;
      display.clearDisplay();
      int radius = heartZoom ? 18 : 13;
      drawSingleHeart(36, 32, radius);
      drawSingleHeart(92, 32, radius);
      display.display();
    }
  }
  else if (durationTogether < 120000) {
    digitalWrite(VIBRO_PIN, LOW);
    roboEyes.setMood(HAPPY);
    roboEyes.setPosition(DEFAULT);

    unsigned long winkCycle = now - lastWinkTime;
    if (winkCycle > 2200) {
      lastWinkTime = now;
      winkLeft = !winkLeft;
    }

    if (winkCycle < 280) {
      if (winkLeft) roboEyes.setHeight(4, 32);
      else roboEyes.setHeight(32, 4);
      roboEyes.setWidth(40, 40);
    } else {
      roboEyes.setWidth(38, 38);
      roboEyes.setHeight(32, 32);
    }
    roboEyes.update();
  }
}

void checkPeerRobotProximity(unsigned long now) {
  int n = WiFi.scanComplete();
  
  if (n >= 0) {
    bool foundClosePeer = false;
    for (int i = 0; i < n; i++) {
      if (WiFi.SSID(i) == PEER_SSID && WiFi.RSSI(i) > PROXIMITY_RSSI) {
        foundClosePeer = true;
        break;
      }
    }

    if (wasFriendNearby && !foundClosePeer) {
      sadModeEndTime = now + 60000UL;
      digitalWrite(VIBRO_PIN, LOW);
      peerCloseStartTime = 0;
      lastActivityTime = now;
    } 
    else if (!wasFriendNearby && foundClosePeer) {
      peerCloseStartTime = now;
      sadModeEndTime = 0;
      lastWinkTime = now;
      lastActivityTime = now;
    }

    if (foundClosePeer) lastActivityTime = now; // بیدار ماندن هنگام حضور جفت

    wasFriendNearby = foundClosePeer;
    isFriendNearby = foundClosePeer;
    WiFi.scanDelete();
    lastPeerScan = now;
  } 
  else if (n == WIFI_SCAN_FAILED && (now - lastPeerScan > 1500)) {
    WiFi.scanNetworks(true, true);
    lastPeerScan = now;
  }
}

// ================= وب‌پورتال آلارم با لوگوی اختصاصی GAMEPHOENIX =================
String getAlarmPortalHTML(String message = "") {
  long remSec = 0;
  if (alarmActive) {
    remSec = (long)(alarmTargetMillis - millis()) / 1000;
    if (remSec < 0) remSec = 0;
  }

  String html = "<!DOCTYPE html><html lang='fa' dir='rtl'><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no'>";
  html += "<title>PHOENIX SMART ROBOT | Alarm Portal</title>";
  html += "<style>";
  html += "*{box-sizing:border-box;margin:0;padding:0;font-family:Tahoma,system-ui,-apple-system,sans-serif;-webkit-tap-highlight-color:transparent;}";
  html += "body{background:radial-gradient(circle at 50% 0%,#160d29,#06080e 75%);color:#f0f6fc;min-height:100vh;display:flex;justify-content:center;align-items:center;padding:16px;}";
  html += ".app{background:rgba(13,17,24,0.95);backdrop-filter:blur(16px);border:1px solid rgba(255,100,0,0.28);border-radius:28px;width:100%;max-width:400px;padding:22px 20px;box-shadow:0 25px 60px rgba(0,0,0,0.9),0 0 35px rgba(216,54,255,0.12);text-align:center;}";
  html += ".logo-box{margin:0 auto 6px auto;width:140px;height:140px;display:flex;align-items:center;justify-content:center;background:#05070b;border-radius:22px;border:1px solid rgba(255,255,255,0.06);box-shadow:inset 0 0 20px rgba(0,0,0,0.9);}";
  html += ".brand{font-size:12px;letter-spacing:2px;background:linear-gradient(90deg,#00d2ff,#d836ff,#ff5100,#ffe600);-webkit-background-clip:text;-webkit-text-fill-color:transparent;font-weight:900;text-transform:uppercase;margin-bottom:4px;}";
  html += ".title{font-size:17px;font-weight:bold;color:#fff;margin-bottom:16px;text-shadow:0 2px 10px rgba(0,0,0,0.5);}";
  html += ".status-box{background:linear-gradient(180deg,#090c12,#0f141d);border:1px solid #21262d;border-radius:20px;padding:15px;margin-bottom:16px;position:relative;overflow:hidden;}";
  html += ".status-active{border-color:rgba(0,242,254,0.5);box-shadow:inset 0 0 25px rgba(0,242,254,0.1);}";
  html += ".badge{display:inline-block;padding:5px 14px;border-radius:20px;font-size:11px;font-weight:bold;margin-bottom:8px;}";
  html += ".badge-on{background:rgba(0,255,136,0.15);color:#00ff88;border:1px solid rgba(0,255,136,0.35);}";
  html += ".badge-off{background:rgba(139,148,158,0.12);color:#8b949e;}";
  html += ".countdown{font-size:34px;font-weight:bold;color:#00f2fe;font-family:monospace;letter-spacing:2px;margin:4px 0;text-shadow:0 0 12px rgba(0,242,254,0.4);}";
  html += ".msg-toast{background:rgba(0,255,136,0.12);border:1px solid #00ff88;color:#00ff88;padding:10px;border-radius:14px;font-size:12px;margin-bottom:16px;font-weight:bold;}";
  html += ".section-lbl{font-size:12px;color:#8b949e;text-align:right;margin:14px 4px 8px 4px;font-weight:bold;}";
  html += ".drum-wrap{display:flex;justify-content:center;align-items:center;gap:10px;background:#090c12;padding:14px;border-radius:18px;border:1px solid #30363d;}";
  html += ".drum-col{flex:1;display:flex;flex-direction:column;align-items:center;}";
  html += ".drum-col span{font-size:11px;color:#8b949e;margin-top:6px;}";
  html += "input[type=number]{width:100%;background:#141923;border:1px solid #30363d;color:#00f2fe;font-size:28px;font-weight:bold;text-align:center;padding:10px 4px;border-radius:12px;outline:none;font-family:monospace;}";
  html += "input[type=number]:focus{border-color:#d836ff;box-shadow:0 0 12px rgba(216,54,255,0.3);}";
  html += ".colon{font-size:28px;font-weight:bold;color:#8b949e;padding-bottom:18px;}";
  html += ".presets{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;margin-top:8px;}";
  html += ".chip{background:#141923;border:1px solid #30363d;color:#c9d1d9;padding:10px 4px;border-radius:12px;font-size:11px;cursor:pointer;transition:0.2s;font-weight:bold;}";
  html += ".chip:active{background:#00f2fe;color:#000;border-color:#00f2fe;transform:scale(0.95);}";
  html += ".exact-box{display:flex;gap:8px;align-items:center;background:#090c12;padding:10px 12px;border-radius:16px;border:1px solid #30363d;}";
  html += "input[type=time]{flex:1;background:#141923;border:1px solid #30363d;color:#fff;font-size:18px;padding:8px;border-radius:10px;text-align:center;outline:none;font-family:monospace;}";
  html += ".btn-exact{background:linear-gradient(135deg,#21262d,#30363d);color:#00f2fe;border:1px solid rgba(0,242,254,0.3);padding:10px 14px;border-radius:10px;font-size:12px;font-weight:bold;cursor:pointer;}";
  html += ".btn-main{background:linear-gradient(90deg,#00d2ff 0%,#d836ff 45%,#ff5100 80%,#ffe600 100%);color:#050811;border:none;padding:15px;width:100%;border-radius:16px;font-weight:900;font-size:15px;cursor:pointer;margin-top:18px;box-shadow:0 8px 25px rgba(216,54,255,0.35);transition:0.2s;}";
  html += ".btn-main:active{transform:scale(0.98);}";
  html += ".btn-cancel{background:rgba(255,77,77,0.12);color:#ff4d4d;border:1px solid rgba(255,77,77,0.4);padding:13px;width:100%;border-radius:16px;font-weight:bold;font-size:14px;cursor:pointer;margin-top:10px;}";
  html += "</style></head><body><div class='app'>";
  
  html += String(PHOENIX_LOGO_SVG);
  html += "<div class='brand'>PHOENIX SMART ROBOT</div>";
  html += "<div class='title'>پورتال هوشمند آلارم گیم فونیکس</div>";

  if (message != "") {
    html += "<div class='msg-toast'>" + message + "</div>";
  }

  if (alarmActive) {
    html += "<div class='status-box status-active'>";
    html += "<span class='badge badge-on'>● آلارم فعال است</span>";
    html += "<div class='countdown' id='cd'>--:--:--</div>";
    html += "<div style='font-size:11px;color:#8b949e;'>زمان باقی‌مانده تا بیدارباش</div></div>";
  } else {
    html += "<div class='status-box'>";
    html += "<span class='badge badge-off'>○ آلارم غیرفعال</span>";
    html += "<div style='font-size:13px;color:#8b949e;margin-top:4px;'>تایمر بیدارباش تنظیم نشده است</div></div>";
  }

  html += "<form action='/set' method='GET' id='af'>";
  html += "<div class='section-lbl'>تنظیم فاصله زمانی تا بیدارباش:</div>";
  html += "<div class='drum-wrap'>";
  html += "<div class='drum-col'><input type='number' id='ih' name='h' min='0' max='72' value='0' required><span>ساعت</span></div>";
  html += "<div class='colon'>:</div>";
  html += "<div class='drum-col'><input type='number' id='im' name='m' min='0' max='59' value='30' required><span>دقیقه</span></div>";
  html += "</div>";

  html += "<div class='section-lbl'>انتخاب سریع (تایمر آماده):</div>";
  html += "<div class='presets'>";
  html += "<button type='button' class='chip' onclick='setP(0,20)'>۲۰ دقیقه</button>";
  html += "<button type='button' class='chip' onclick='setP(1,0)'>۱ ساعت</button>";
  html += "<button type='button' class='chip' onclick='setP(6,0)'>۶ ساعت</button>";
  html += "<button type='button' class='chip' onclick='setP(8,0)'>۸ ساعت</button>";
  html += "</div>";

  html += "<div class='section-lbl'>یا انتخاب ساعت دقیق بیدار شدن:</div>";
  html += "<div class='exact-box'>";
  html += "<input type='time' id='et' value='07:30'>";
  html += "<button type='button' class='btn-exact' onclick='calcExact()'>محاسبه و ثبت</button>";
  html += "</div>";

  html += "<button type='submit' class='btn-main'>فعال‌سازی آلارم بیدارباش</button>";
  html += "</form>";

  if (alarmActive) {
    html += "<form action='/cancel' method='GET'><button type='submit' class='btn-cancel'>خاموش کردن آلارم</button></form>";
  }

  html += "<script>";
  html += "function setP(h,m){document.getElementById('ih').value=h;document.getElementById('im').value=m;document.getElementById('af').submit();}";
  html += "function calcExact(){var v=document.getElementById('et').value;if(!v)return;var p=v.split(':');var now=new Date();var t=new Date();t.setHours(parseInt(p[0]),parseInt(p[1]),0,0);if(t<=now)t.setDate(t.getDate()+1);var diff=Math.round((t-now)/60000);document.getElementById('ih').value=Math.floor(diff/60);document.getElementById('im').value=diff%60;document.getElementById('af').submit();}";
  html += "var rem=" + String(remSec) + ";";
  html += "if(rem>0){function tick(){var h=Math.floor(rem/3600);var m=Math.floor((rem%3600)/60);var s=rem%60;document.getElementById('cd').innerText=(h<10?'0'+h:h)+':'+(m<10?'0'+m:m)+':'+(s<10?'0'+s:s);if(rem>0)rem--;}tick();setInterval(tick,1000);}";
  html += "</script></div></body></html>";
  return html;
}

void setupAlarmWebServer() {
  server.on("/", HTTP_GET, []() {
    lastActivityTime = millis();
    server.send(200, "text/html", getAlarmPortalHTML());
  });

  server.on("/set", HTTP_GET, []() {
    lastActivityTime = millis();
    int h = 0;
    int m = 0;
    if (server.hasArg("h")) h = server.arg("h").toInt();
    if (server.hasArg("m")) m = server.arg("m").toInt();
    
    if (server.hasArg("hours")) {
      float fh = server.arg("hours").toFloat();
      h = (int)fh;
      m = (int)((fh - h) * 60);
    }

    unsigned long totalMillis = ((unsigned long)h * 3600000UL) + ((unsigned long)m * 60000UL);
    if (totalMillis > 0) {
      alarmTargetMillis = millis() + totalMillis;
      alarmActive = true;
      alarmRinging = false;
      soundAlarmSet();
      String msg = "آلارم برای ";
      if (h > 0) msg += String(h) + " ساعت ";
      if (h > 0 && m > 0) msg += "و ";
      if (m > 0) msg += String(m) + " دقیقه ";
      msg += "دیگر تنظیم شد!";
      server.send(200, "text/html", getAlarmPortalHTML(msg));
      return;
    }
    server.send(200, "text/html", getAlarmPortalHTML("لطفاً زمان معتبری وارد کنید!"));
  });

  server.on("/cancel", HTTP_GET, []() {
    lastActivityTime = millis();
    alarmActive = false;
    alarmRinging = false;
    noTone(BUZZER_PIN);
    playTone(500, 100);
    server.send(200, "text/html", getAlarmPortalHTML("آلارم با موفقیت غیرفعال شد."));
  });

  server.onNotFound([]() {
    lastActivityTime = millis();
    server.send(200, "text/html", getAlarmPortalHTML());
  });
}

void startAlarmAP() {
  if (!isAPRunning) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("PHOENIX-ALARM");
    dnsServer.start(53, "*", WiFi.softAPIP());
    server.begin();
    isAPRunning = true;
  }
}

void stopAlarmAP() {
  if (isAPRunning) {
    dnsServer.stop();
    server.stop();
    isAPRunning = false;
    startPeerBeacon();
  }
}

// ================= توابع ورود به حالت اسلیپ کم‌مصرف و بیدار شدن =================
void enterSleepMode() {
  if (isSleeping) return;
  isSleeping = true;
  noTone(BUZZER_PIN);
  digitalWrite(VIBRO_PIN, LOW);

  // خاموش کردن کامل نمایشگر OLED
  display.clearDisplay();
  display.display();
  display.ssd1306_command(SSD1306_DISPLAYOFF);

  // خاموش کردن وای‌فای و کاهش فرکانس پردازنده برای کمترین مصرف باتری
  if (isAPRunning) {
    dnsServer.stop();
    server.stop();
    isAPRunning = false;
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  setCpuFrequencyMhz(80);
}

void wakeUpFromSleep() {
  if (!isSleeping) return;
  setCpuFrequencyMhz(240);
  isSleeping = false;
  lastActivityTime = millis();

  // روشن کردن مجدد نمایشگر OLED
  display.ssd1306_command(SSD1306_DISPLAYON);
  display.clearDisplay();
  display.display();

  // راه‌اندازی مجدد سرویس‌های بی‌سیم بر اساس مود فعلی
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin();
  if (currentMode == 1) {
    startAlarmAP();
  } else {
    startPeerBeacon();
  }
  playTone(1200, 40);
}

// ================= توابع ساعت و تاریخ شمسی و رابط کاربری OLED =================
String getShamsiDate(int gY, int gM, int gD) {
  int gy = gY - 1600; int gm = gM - 1; int gd = gD - 1;
  long g_day_no = 365 * gy + (gy + 3) / 4 - (gy + 99) / 100 + (gy + 399) / 400;
  int i; int g_days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (gM > 2 && ((gY % 4 == 0 && gY % 100 != 0) || (gY % 400 == 0))) g_day_no++;
  for (i = 0; i < gm; ++i) g_day_no += g_days_in_month[i];
  g_day_no += gd;
  long j_day_no = g_day_no - 79;
  long j_np = j_day_no / 12053; j_day_no %= 12053;
  int jy = 979 + 33 * j_np + 4 * (j_day_no / 1461); j_day_no %= 1461;
  if (j_day_no >= 366) { jy += (j_day_no - 1) / 365; j_day_no = (j_day_no - 1) % 365; }
  int jm, jd;
  for (i = 0; i < 11 && j_day_no >= (i < 6 ? 31 : 30); ++i) { j_day_no -= (i < 6 ? 31 : 30); }
  jm = i + 1; jd = j_day_no + 1;
  return String(jy) + "/" + (jm < 10 ? "0" : "") + String(jm) + "/" + (jd < 10 ? "0" : "") + String(jd);
}

void printLocalTime() {
  drawTopBar("PHOENIX-ALARM");
  
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    display.drawRoundRect(14, 22, 100, 26, 4, WHITE);
    display.setTextSize(1);
    display.setCursor(25, 31);
    display.print("Syncing Time...");
    return;
  }
  
  char hStr[3], mStr[3];
  strftime(hStr, 3, "%H", &timeinfo);
  strftime(mStr, 3, "%M", &timeinfo);
  
  display.setTextSize(2);
  display.setCursor(26, 16);
  display.print(hStr);
  
  if ((millis() / 500) % 2 == 0) {
    display.setCursor(58, 15);
    display.print(":");
  }
  
  display.setCursor(74, 16);
  display.print(mStr);

  String shamsi = getShamsiDate(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
  display.setTextSize(1);
  display.setCursor(34, 36);
  display.print(shamsi);

  if (alarmActive) {
    long remMin = ((long)(alarmTargetMillis - millis()) / 60000L) + 1;
    if (remMin < 0) remMin = 0;
    int rh = remMin / 60;
    int rm = remMin % 60;
    
    display.fillRoundRect(8, 49, 112, 14, 4, WHITE);
    display.setTextColor(BLACK);
    display.drawBitmap(14, 52, bellIcon, 8, 8, BLACK);
    display.setCursor(26, 52);
    display.print("ON (");
    if (rh > 0) { display.print(rh); display.print("h "); }
    display.print(rm); display.print("m)");
    display.setTextColor(WHITE);
  } else {
    display.drawRoundRect(16, 49, 96, 14, 4, WHITE);
    display.drawBitmap(24, 52, bellIcon, 8, 8, WHITE);
    display.setCursor(38, 52);
    display.print("ALARM: OFF");
  }
}

void displayWeather() {
  float t = readPreciseLM35();
  int batPercent = getBatteryPercentage();
  int wifiPercent = getWiFiSignalPercent();

  drawTopBar("SYSTEM INFO");

  display.drawRoundRect(2, 15, 102, 24, 4, WHITE);
  display.setTextSize(2);
  display.setCursor(10, 20);
  display.print(t, 1);
  display.setTextSize(1);
  display.cp437(true);
  display.write(248);
  display.setTextSize(2);
  display.print("C");

  display.setTextSize(1);
  display.setCursor(2, 43);
  display.print("WIFI");
  display.drawRect(30, 44, 48, 6, WHITE);
  int wFill = map(wifiPercent, 0, 100, 0, 44);
  if (wFill > 0) display.fillRect(32, 46, wFill, 2, WHITE);
  display.setCursor(82, 43);
  display.print(wifiPercent); display.print("%");

  display.setCursor(2, 54);
  display.print("BATT");
  display.drawRect(30, 55, 48, 6, WHITE);
  int bFill = map(batPercent, 0, 100, 0, 44);
  if (bFill > 0) display.fillRect(32, 57, bFill, 2, WHITE);
  display.setCursor(82, 54);
  display.print(batPercent); display.print("%");

  display.drawRoundRect(112, 15, 10, 47, 4, WHITE);
  int fillLevel = map((int)t, 0, 50, 0, 41);
  if(fillLevel > 41) fillLevel = 41;
  if(fillLevel < 0) fillLevel = 0;
  display.fillRoundRect(114, 60 - fillLevel, 6, fillLevel, 2, WHITE);
}

// ================= منطق بازی دایناسور (مود ۴) =================
void resetDinoGame() {
  dinoY = 42; dinoVy = 0; isJumping = false;
  obstacleX = 128; gameSpeed = 3.5; dinoScore = 0; dinoGameOver = false;
  playTone(1000, 60);
}

void handleDinoGame(unsigned long now, int touchState) {
  bool touchJustPressed = (touchState == HIGH && !lastTouchForDino);
  lastTouchForDino = (touchState == HIGH);

  if (dinoGameOver) {
    if (touchJustPressed) resetDinoGame();
    return;
  }

  if (now - lastGameTick < 25) return;
  lastGameTick = now;

  if (touchState == HIGH && !isJumping) {
    dinoVy = jumpPower; 
    isJumping = true;
    soundJump();
  }

  dinoY += dinoVy; dinoVy += gravity;
  if (dinoY >= 42) { dinoY = 42; dinoVy = 0; isJumping = false; }

  obstacleX -= gameSpeed;
  if (obstacleX < -15) {
    obstacleX = 128 + random(0, 40);
    obstacleType = random(0, 3);
    dinoScore++;
    soundScore();
    if (dinoScore > dinoHighScore) dinoHighScore = dinoScore;
    if (gameSpeed < 7.5) gameSpeed += 0.15;
  }

  int obsW = (obstacleType == 2) ? 12 : 6;
  int obsH = (obstacleType == 1) ? 14 : 10;
  int obsY = 58 - obsH;

  if (obstacleX < 26 && (obstacleX + obsW) > 16) {
    if ((dinoY + 14) > obsY) {
      dinoGameOver = true;
    }
  }

  display.clearDisplay();
  
  display.fillRect(0, 0, 128, 10, WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(1);
  display.setCursor(4, 1); display.print("HI:"); display.print(dinoHighScore);
  display.setCursor(74, 1); display.print("SC:"); display.print(dinoScore);
  display.setTextColor(WHITE);
  
  display.drawFastHLine(0, 58, 128, WHITE);

  display.drawBitmap(14, (int)dinoY, dinoSprite, 16, 16, WHITE);
  if (!isJumping) {
    if ((now / 100) % 2 == 0) display.fillRect(16, 56, 3, 2, BLACK);
    else display.fillRect(20, 56, 3, 2, BLACK);
  }

  int ox = (int)obstacleX;
  if (obstacleType == 0) {
    display.fillRect(ox, 48, 5, 10, WHITE);
    display.fillRect(ox - 2, 50, 2, 4, WHITE);
    display.fillRect(ox + 5, 49, 2, 4, WHITE);
  } else if (obstacleType == 1) {
    display.fillRect(ox, 44, 6, 14, WHITE);
    display.fillRect(ox - 2, 47, 2, 5, WHITE);
    display.fillRect(ox + 6, 46, 2, 5, WHITE);
  } else {
    display.fillRect(ox, 48, 4, 10, WHITE);
    display.fillRect(ox + 7, 46, 5, 12, WHITE);
  }

  if (dinoGameOver) {
    display.fillRoundRect(16, 15, 96, 34, 4, BLACK);
    display.drawRoundRect(16, 15, 96, 34, 4, WHITE);
    display.fillRoundRect(16, 15, 96, 13, 4, WHITE);
    display.setTextColor(BLACK);
    display.setCursor(37, 18); display.print("GAME OVER");
    display.setTextColor(WHITE);
    display.setCursor(25, 34); display.print("Touch to Play");
    display.display();
    soundGameOver();
    return;
  }
  display.display();
}

// ================= منطق ماشین‌بازی با سنسور BMI160 (مود ۵) =================
void drawCarShape(int x, int y) {
  display.fillRoundRect(x + 2, y, 8, 14, 2, WHITE);
  display.fillRect(x, y + 2, 2, 3, WHITE);
  display.fillRect(x + 10, y + 2, 2, 3, WHITE);
  display.fillRect(x, y + 9, 2, 3, WHITE);
  display.fillRect(x + 10, y + 9, 2, 3, WHITE);
  display.fillRect(x + 3, y + 4, 6, 3, BLACK);
}

void resetCarGame() {
  carX = 58;
  enemyY = -15;
  int lanes[3] = {35, 58, 81};
  enemyX = lanes[random(0, 3)];
  carSpeed = 2.8;
  carScore = 0;
  carGameOver = false;
  playTone(1000, 60);
}

void handleCarGame(unsigned long now, int touchState) {
  bool touchJustPressed = (touchState == HIGH && !lastTouchForCar);
  lastTouchForCar = (touchState == HIGH);

  if (carGameOver) {
    if (touchJustPressed) resetCarGame();
    return;
  }

  if (now - lastCarTick < 25) return;
  lastCarTick = now;

  int16_t accelGyro[6] = {0};
  if (bmi160.getAccelGyroData(accelGyro) == BMI160_OK) {
    float ax = accelGyro[3] / 16384.0;
    float ay = accelGyro[4] / 16384.0;
    
    float tilt = (abs(ay) > abs(ax)) ? ay : ax;
    if (abs(tilt) > 0.08) {
      carX -= tilt * 8.5;
    }
  }

  if (carX < 30) carX = 30;
  if (carX > 86) carX = 86;

  enemyY += carSpeed;
  roadOffset = (roadOffset + (int)carSpeed) % 16;

  if (enemyY > 64) {
    enemyY = -16;
    int lanes[3] = {34, 58, 82};
    enemyX = lanes[random(0, 3)];
    carScore++;
    soundScore();
    if (carScore > carHighScore) carHighScore = carScore;
    if (carSpeed < 6.5) carSpeed += 0.15;
  }

  if (enemyY + 13 >= 47 && enemyY <= 61) {
    if (abs((int)carX - (int)enemyX) < 10) {
      carGameOver = true;
    }
  }

  display.clearDisplay();
  display.drawFastVLine(28, 0, 64, WHITE);
  display.drawFastVLine(100, 0, 64, WHITE);

  for (int i = -16; i < 64; i += 16) {
    display.drawFastVLine(52, i + roadOffset, 8, WHITE);
    display.drawFastVLine(76, i + roadOffset, 8, WHITE);
  }

  display.drawRoundRect(0, 2, 25, 26, 3, WHITE);
  display.setTextSize(1);
  display.setCursor(6, 5);  display.print("SC");
  display.setCursor(6, 16); display.print(carScore);

  display.drawRoundRect(103, 2, 25, 26, 3, WHITE);
  display.setCursor(109, 5);  display.print("HI");
  display.setCursor(109, 16); display.print(carHighScore);

  drawCarShape((int)carX, 47);
  drawCarShape((int)enemyX, (int)enemyY);

  if (carGameOver) {
    display.fillRoundRect(16, 15, 96, 34, 4, BLACK);
    display.drawRoundRect(16, 15, 96, 34, 4, WHITE);
    display.fillRoundRect(16, 15, 96, 13, 4, WHITE);
    display.setTextColor(BLACK);
    display.setCursor(40, 18); display.print("CRASHED!");
    display.setTextColor(WHITE);
    display.setCursor(25, 34); display.print("Touch to Play");
    display.display();
    soundCrash();
    return;
  }

  display.display();
}

// ================= تابع مدیریت حالت چشم‌ها و تشخیص جفت =================
void handleEyesMode(unsigned long now, int touchState) {
  checkPeerRobotProximity(now);

  if (isFriendNearby) {
    if (peerCloseStartTime == 0) peerCloseStartTime = now;
    unsigned long durationTogether = now - peerCloseStartTime;

    if (durationTogether < 120000UL) {
      handlePeerInteractionStages(now, durationTogether);
      return;
    } else {
      digitalWrite(VIBRO_PIN, LOW);
    }
  }

  if (!isFriendNearby && sadModeEndTime > 0) {
    if ((long)(sadModeEndTime - now) > 0) {
      roboEyes.setMood(TIRED);
      roboEyes.setPosition(S);
      roboEyes.setWidth(38, 38);
      roboEyes.setHeight(26, 26);
      roboEyes.update();
      return;
    } else {
      sadModeEndTime = 0;
      roboEyes.setMood(DEFAULT);
      roboEyes.setPosition(DEFAULT);
      roboEyes.setWidth(38, 38);
      roboEyes.setHeight(32, 32);
    }
  }

  float ax = prevAx, ay = prevAy, az = prevAz;

  roboEyes.update();
  if (touchState == HIGH) {
    if (!wasTouched) {
      soundHappyChirp();
    }
    roboEyes.setMood(HAPPY); roboEyes.setPosition(N); roboEyes.setWidth(45, 45);
    wasTouched = true; currentStep = 0; lastMoveTime = now;
  }
  else {
    if (wasTouched) {
      roboEyes.setMood(DEFAULT); roboEyes.setPosition(DEFAULT);
      roboEyes.setWidth(38, 38); roboEyes.setHeight(32, 32);
      wasTouched = false;
    }
    if (currentStep == 0 && (now - lastMoveTime > moveInterval)) {
      lastDir = random(0, 2); currentStep = 1; jumpTimer = now;
    }
    if (currentStep == 1) {
      if (lastDir == 0) { roboEyes.setPosition(E); roboEyes.setWidth(38, 46); roboEyes.setHeight(32, 38); }
      else { roboEyes.setPosition(W); roboEyes.setWidth(46, 38); roboEyes.setHeight(38, 32); }
      if (now - jumpTimer > 800) { currentStep = 2; jumpTimer = now; }
    }
    else if (currentStep == 2) {
      if (lastDir == 0) { roboEyes.setPosition(W); roboEyes.setWidth(46, 38); roboEyes.setHeight(38, 32); }
      else { roboEyes.setPosition(E); roboEyes.setWidth(38, 46); roboEyes.setHeight(32, 38); }
      if (now - jumpTimer > 800) {
        roboEyes.setPosition(DEFAULT); roboEyes.setWidth(38, 38); roboEyes.setHeight(32, 32);
        currentStep = 0; lastMoveTime = now; moveInterval = random(5000, 12000);
      }
    }

    if (az < -0.6 || abs(ax) > 0.6) {
      roboEyes.setMood(ANGRY);
      if (!wasAngrySoundPlayed) {
        soundAngry();
        wasAngrySoundPlayed = true;
      }
    }
    else if (abs(ay) > 0.6) {
      roboEyes.setMood(TIRED);
      wasAngrySoundPlayed = false;
    }
    else if (currentStep == 0) {
      roboEyes.setMood(DEFAULT);
      wasAngrySoundPlayed = false;
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(touchPin, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(VIBRO_PIN, OUTPUT);
  digitalWrite(VIBRO_PIN, LOW);
  
  analogReadResolution(12);
  analogSetPinAttenuation(LM35_PIN, ADC_11db);
  analogSetPinAttenuation(BATTERY_PIN, ADC_11db);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) for(;;);

  display.clearDisplay();
  display.drawRoundRect(2, 2, 124, 60, 6, WHITE);
  display.fillRoundRect(18, 10, 92, 15, 3, WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(1);
  display.setCursor(28, 14);
  display.print("GAME PHOENIX");
  display.setTextColor(WHITE);
  display.setCursor(31, 29);
  display.print("DESKTOP BOT");
  display.drawRoundRect(14, 43, 100, 10, 3, WHITE);
  display.display();
  
  for(int i = 0; i <= 96; i+=8) { 
    display.fillRect(16, 45, i, 6, WHITE); 
    display.display(); 
    delay(50); 
  }
  soundBoot();

  WiFiManager wm;

  if (digitalRead(touchPin) == HIGH) {
    display.clearDisplay();
    display.drawRoundRect(8, 14, 112, 36, 5, WHITE);
    display.setCursor(22, 24); display.print("FACTORY RESET");
    display.setCursor(25, 36); display.print("Clearing WiFi...");
    display.display();
    playTone(400, 500);
    wm.resetSettings();
    delay(2000);
    ESP.restart();
  }

  // استایل گیمینگ برای پورتال WiFiManager همراه با لوگوی نئونی آپلود شده
  const char* custom_head = 
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<style>"
    "body{background:radial-gradient(circle at 50% 0%,#160d29,#06080e 75%);color:#f0f6fc;font-family:Tahoma,sans-serif;text-align:center;padding:20px;margin:0;}"
    ".wrap{background:rgba(13,17,24,0.95);border:1px solid rgba(255,100,0,0.3);border-radius:26px;padding:24px 20px;max-width:390px;margin:20px auto;box-shadow:0 25px 60px rgba(0,0,0,0.9),0 0 30px rgba(216,54,255,0.12);}"
    ".logo-box{margin:0 auto 8px auto;width:140px;height:140px;display:flex;align-items:center;justify-content:center;background:#05070b;border-radius:22px;border:1px solid rgba(255,255,255,0.06);box-shadow:inset 0 0 20px rgba(0,0,0,0.9);}"
    "h1{background:linear-gradient(90deg,#00d2ff,#d836ff,#ff5100,#ffe600);-webkit-background-clip:text;-webkit-text-fill-color:transparent;font-size:18px;letter-spacing:1.5px;margin-bottom:6px;font-weight:900;}"
    "h3{color:#8b949e;font-size:12px;font-weight:normal;margin-top:0;margin-bottom:18px;}"
    "button,input[type='submit'],.btn{background:linear-gradient(90deg,#00d2ff 0%,#d836ff 45%,#ff5100 80%,#ffe600 100%);color:#050811 !important;border:none;padding:14px;width:100%;border-radius:14px;font-weight:900;font-size:14px;cursor:pointer;margin:8px 0;box-shadow:0 6px 20px rgba(216,54,255,0.3);}"
    "input[type='text'],input[type='password']{background:#090c12;border:1px solid #30363d;color:#fff;padding:12px;width:100%;box-sizing:border-box;border-radius:12px;margin:8px 0 14px 0;font-size:14px;text-align:center;outline:none;}"
    "input:focus{border-color:#d836ff;box-shadow:0 0 12px rgba(216,54,255,0.25);}"
    "div a{color:#00f2fe;text-decoration:none;font-weight:bold;}"
    "</style>";
  wm.setCustomHeadElement(custom_head);
  
  String wmTitle = String(PHOENIX_LOGO_SVG) + "<h1>PHOENIX SMART ROBOT</h1><h3>پورتال اختصاصی تنظیمات شبکه گیم فونیکس</h3>";
  WiFiManagerParameter custom_title(wmTitle.c_str());
  wm.addParameter(&custom_title);

  display.clearDisplay();
  drawTopBar("WIFI SETUP");
  display.drawRoundRect(2, 14, 124, 48, 4, WHITE);
  display.setCursor(7, 19); display.print("PHOENIX SMART ROBOT");
  display.drawFastHLine(8, 29, 112, WHITE);
  display.setCursor(10, 34); display.print("PWD: gamephoenix");
  display.setCursor(10, 46); display.print("IP : 192.168.4.1");
  display.display();

  if (!wm.autoConnect("PHOENIX SMART ROBOT", "gamephoenix")) {
    delay(3000);
    ESP.restart();
  }

  setupAlarmWebServer();
  startPeerBeacon();

  if (bmi160.I2cInit(0x69) != BMI160_OK) {
    if (bmi160.I2cInit(0x68) != BMI160_OK) {
      Serial.println("BMI160 initialization error");
    }
  }

  configTime(12600, 0, ntpServer);
  roboEyes.begin(SCREEN_WIDTH, SCREEN_HEIGHT, 100);
  roboEyes.setAutoblinker(ON, 2, 4);
  roboEyes.setWidth(38, 38); roboEyes.setHeight(32, 32);

  lastActivityTime = millis();
}

void loop() {
  int touchState = digitalRead(touchPin);
  unsigned long now = millis();

  // ۱. بررسی رسیدن زمان آلارم (حتی اگر در حالت اسلیپ باشد، بلافاصله بیدار می‌شود و زنگ می‌زند)
  if (alarmActive && (long)(now - alarmTargetMillis) >= 0) {
    if (isSleeping) {
      wakeUpFromSleep();
    }
    alarmActive = false;
    alarmRinging = true;
    digitalWrite(VIBRO_PIN, LOW);
  }

  // ۲. بررسی تکان خوردن یا لمس شدن ربات
  static unsigned long lastMotionPoll = 0;
  bool motionDetected = false;
  if (now - lastMotionPoll > 80) {
    lastMotionPoll = now;
    motionDetected = checkMotionActivity();
  }

  if (touchState == HIGH || motionDetected || alarmRinging) {
    lastActivityTime = now;
    if (isSleeping) {
      wakeUpFromSleep();
      delay(150);
      return;
    }
  }

  // ۳. ورود به حالت اسلیپ در صورت ۲ دقیقه عدم فعالیت
  if (!isSleeping && !alarmRinging && (now - lastActivityTime > INACTIVITY_TIMEOUT)) {
    enterSleepMode();
  }

  // اگر در حالت اسلیپ هستیم، صفحه خاموش و مصرف در کمترین حالت بماند ولی آماده آلارم یا بیدار شدن باشد
  if (isSleeping) {
    delay(60);
    return;
  }

  // ۴. اگر آلارم در حال زنگ زدن است
  if (alarmRinging) {
    if (now - lastAlarmBeep > 250) {
      lastAlarmBeep = now;
      alarmBeepState = !alarmBeepState;
      if (alarmBeepState) {
        tone(BUZZER_PIN, 2000, 200);
        digitalWrite(VIBRO_PIN, HIGH);
      } else {
        tone(BUZZER_PIN, 1200, 200);
        digitalWrite(VIBRO_PIN, LOW);
      }

      display.clearDisplay();
      display.drawRoundRect(2, 2, 124, 60, 6, WHITE);
      if (alarmBeepState) {
        display.fillRoundRect(8, 8, 112, 28, 4, WHITE);
        display.setTextColor(BLACK);
      } else {
        display.drawRoundRect(8, 8, 112, 28, 4, WHITE);
        display.setTextColor(WHITE);
      }
      display.setTextSize(2);
      display.setCursor(18, 15);
      display.print("WAKE UP!");
      display.setTextColor(WHITE);
      display.setTextSize(1);
      display.setCursor(25, 44);
      display.print("Touch to Dismiss");
      display.display();
    }

    if (touchState == HIGH) {
      alarmRinging = false;
      noTone(BUZZER_PIN);
      digitalWrite(VIBRO_PIN, LOW);
      lastActivityTime = now;
      display.clearDisplay();
      delay(500);
    }
    return;
  }

  // ۵. تغییر بین ۵ مود با نگه داشتن تاچ به مدت ۱.۲ ثانیه
  if (touchState == HIGH) {
    if (!isHoldingTouch) { touchStartTime = now; isHoldingTouch = true; }
    if (now - touchStartTime > 1200) {
      digitalWrite(VIBRO_PIN, LOW);
      isFriendNearby = false;
      wasFriendNearby = false;
      peerCloseStartTime = 0;
      sadModeEndTime = 0;
      soundModeSwitch();
      currentMode++; 
      if (currentMode > 4) currentMode = 0;
      
      if (currentMode == 1) startAlarmAP();
      else stopAlarmAP();

      if (currentMode == 3) resetDinoGame();
      if (currentMode == 4) resetCarGame();
      isHoldingTouch = false; 
      lastActivityTime = now;
      display.clearDisplay(); 
      delay(400);
      return;
    }
  } else { 
    isHoldingTouch = false; 
  }

  // ۶. اجرای مود فعلی
  if (currentMode == 0) {
    handleEyesMode(now, touchState);
  }
  else if (currentMode == 1) {
    dnsServer.processNextRequest();
    server.handleClient();
    
    static unsigned long lastT = 0;
    if(now - lastT > 500) {
      display.clearDisplay(); 
      printLocalTime(); 
      display.display(); 
      lastT = now; 
    }
  }
  else if (currentMode == 2) {
    static unsigned long lastW = 0;
    if(now - lastW > 1000) {
      display.clearDisplay();
      display.setTextColor(WHITE);
      displayWeather();
      display.display();
      lastW = now;
    }
  }
  else if (currentMode == 3) {
    handleDinoGame(now, touchState);
  }
  else if (currentMode == 4) {
    handleCarGame(now, touchState);
  }
}