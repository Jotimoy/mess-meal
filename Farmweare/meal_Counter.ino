#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <time.h>

// ================= কনফিগারেশন =================
const char* ssid        = "Xpark X";      // আপনার ওয়াইফাই নাম
const char* password    = "xpark@iub";  // আপনার ওয়াইফাই পাসওয়ার্ড
const char* firebaseUrl = "https://meal-display-default-rtdb.asia-southeast1.firebasedatabase.app";

// স্ক্যান ইন্টারভাল
const unsigned long ACTIVE_INTERVAL  = 60000UL;    // অ্যাক্টিভ টাইমে (৭-১১ ও ২-৫): ১ মিনিট
const unsigned long IDLE_INTERVAL    = 3600000UL;  // বাকি অন-টাইমে (১১-২ ও ৫-১২): ১ ঘণ্টা
const unsigned long BOOT_PREVIEW_MS  = 30000UL;    // রাত ১২-৭টার মধ্যে রিস্টার্ট দিলে ৩০ সেকেন্ড টেস্ট ভিউ

// ================= OLED ডিসপ্লে (SH1106 1.3") =================
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

enum SystemMode { MODE_UNKNOWN, MODE_ACTIVE, MODE_IDLE, MODE_SLEEP };
SystemMode currentMode = MODE_UNKNOWN;

int morningCount = 0;
int nightCount   = 0;

int lastDisplayedCount  = -1;
int lastDisplayedPeriod = -1; // 1 = SOKAL, 0 = RAT

bool wifiIsOn    = false;
bool displayIsOn = true;
bool timeSynced  = false;

unsigned long bootMillis        = 0;
unsigned long lastSyncMillis    = 0;
unsigned long lastNtpSyncMillis = 0;

// ================= বাংলাদেশ সময় (UTC+6) =================
int getCurrentBDHour() {
  time_t utcNow = time(nullptr);
  if (utcNow < 1700000000) return -1;
  time_t bdNow = utcNow + (6 * 3600);
  struct tm* t = gmtime(&bdNow);
  return t->tm_hour;
}

// কোন সময়ে কোন মোড চলবে
SystemMode determineMode(int hour) {
  if (hour < 0) return MODE_ACTIVE; // সময় সিঙ্ক না হওয়া পর্যন্ত অ্যাক্টিভ থাকবে

  // রাত ১২:০০ – সকাল ৭:০০ (০ থেকে ৬): সম্পূর্ণ অফ
  if (hour >= 0 && hour < 7) {
    // রিস্টার্ট/আপলোডের পর ৩০ সেকেন্ড ডিসপ্লে অন রাখবে যেন রাতে টেস্ট করতে পারেন
    if (millis() - bootMillis < BOOT_PREVIEW_MS) {
      return MODE_IDLE;
    }
    return MODE_SLEEP;
  }

  // সকাল ৭:০০ – ১১:০০ এবং দুপুর ২:০০ (১৪) – বিকাল ৫:০০ (১৭): অ্যাক্টিভ মোড
  if ((hour >= 7 && hour < 11) || (hour >= 14 && hour < 17)) {
    return MODE_ACTIVE;
  }

  // সকাল ১১:০০ – দুপুর ২:০০ এবং বিকাল ৫:০০ – রাত ১২:০০: আইডল মোড (ডিসপ্লে অন, ১ ঘণ্টা পরপর সিঙ্ক)
  return MODE_IDLE;
}

// দুপুর ১২টার আগে SOKAL, দুপুর ১২টা থেকে রাত ১২টা পর্যন্ত RAT
bool isMorningDisplay(int hour) {
  if (hour < 0) return true;
  return (hour < 12);
}

// ================= ডিসপ্লে পাওয়ার কন্ট্রোল =================
void setDisplayPower(bool on) {
  if (displayIsOn == on) return;
  displayIsOn = on;
  if (on) {
    u8g2.setPowerSave(0); // ডিসপ্লে অন
    lastDisplayedCount = -1; // ফোর্স রিড্র
  } else {
    u8g2.clearBuffer();
    u8g2.sendBuffer();
    u8g2.setPowerSave(1); // OLED প্যানেল সম্পূর্ণ বন্ধ
  }
}

// ================= Wi-Fi কন্ট্রোল =================
bool ensureWifiConnected() {
  if (wifiIsOn && WiFi.status() == WL_CONNECTED) {
    return true;
  }

  WiFi.forceSleepWake();
  delay(1);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleepMode(WIFI_MODEM_SLEEP); // অন থাকা অবস্থাতেও পাওয়ার ও হিট কম রাখবে
  WiFi.setOutputPower(14.0);           // হিটিং কমানোর জন্য অপ্টিমাল সিগন্যাল পাওয়ার
  WiFi.begin(ssid, password);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(250);
  }

  wifiIsOn = (WiFi.status() == WL_CONNECTED);
  return wifiIsOn;
}

void turnWifiOff() {
  if (!wifiIsOn && WiFi.getMode() == WIFI_OFF) return;
  WiFi.disconnect(false);
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  delay(1);
  wifiIsOn = false;
}

// ================= ইন্টারনেট থেকে সময় সিঙ্ক =================
void syncNTP() {
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  unsigned long start = millis();
  while (time(nullptr) < 1700000000 && millis() - start < 8000) {
    delay(200);
  }
  if (time(nullptr) >= 1700000000) {
    timeSynced = true;
    lastNtpSyncMillis = millis();
  }
}

// ================= বিশাল ফন্টে ডিসপ্লে আপডেট =================
void updateOLEDIfNeeded() {
  if (!displayIsOn) return;

  int hour = getCurrentBDHour();
  bool showMorning = isMorningDisplay(hour);
  int countToShow  = showMorning ? morningCount : nightCount;
  int periodCode   = showMorning ? 1 : 0;

  // কোনো পরিবর্তন না থাকলে অযথা রিফ্রেশ করবে না
  if (countToShow == lastDisplayedCount && periodCode == lastDisplayedPeriod) {
    return;
  }

  lastDisplayedCount  = countToShow;
  lastDisplayedPeriod = periodCode;

  u8g2.clearBuffer();

  // ওপরে শুধু SOKAL বা RAT
  const char* label = showMorning ? "SOKAL" : "RAT";
  u8g2.setFont(u8g2_font_helvB12_tr);
  int lw = u8g2.getStrWidth(label);
  u8g2.drawStr((128 - lw) / 2, 13, label);

  // নিচে বিশাল ফন্টে শুধু সংখ্যা
  String numStr = String(countToShow);
  u8g2.setFont(u8g2_font_logisoso42_tn);
  int nw = u8g2.getStrWidth(numStr.c_str());
  u8g2.drawStr((128 - nw) / 2, 62, numStr.c_str());

  u8g2.sendBuffer();
}

// ================= JSON পার্সার =================
int extractInt(const String& json, const String& key) {
  String search = "\"" + key + "\":";
  int idx = json.indexOf(search);
  if (idx == -1) return -1;
  idx += search.length();
  while (idx < (int)json.length() && json[idx] == ' ') idx++;
  int end = idx;
  while (end < (int)json.length() && isDigit(json[end])) end++;
  if (end == idx) return -1;
  return json.substring(idx, end).toInt();
}

// ================= Firebase থেকে ডেটা রিড =================
void fetchFirebaseData() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = String(firebaseUrl) + "/summary.json";
  http.setTimeout(8000);

  if (http.begin(client, url)) {
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      payload.trim();
      if (payload == "null") {
        morningCount = 0;
        nightCount = 0;
      } else {
        int m = extractInt(payload, "morning");
        int n = extractInt(payload, "night");
        if (m >= 0) morningCount = m;
        if (n >= 0) nightCount = n;
      }
    }
    http.end();
  }
}

// ================= Setup & Loop =================
void setup() {
  Serial.begin(115200);
  bootMillis = millis();
  WiFi.persistent(false);

  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_helvB12_tr);
  u8g2.drawStr(24, 38, "STARTING");
  u8g2.sendBuffer();

  if (ensureWifiConnected()) {
    syncNTP();
    fetchFirebaseData();
  }

  lastSyncMillis = millis();
  updateOLEDIfNeeded();
}

void loop() {
  int hour = getCurrentBDHour();
  SystemMode targetMode = determineMode(hour);

  // ১. মোড পরিবর্তন হলে সাথে সাথে অ্যাকশন নেওয়া
  if (targetMode != currentMode) {
    currentMode = targetMode;

    if (currentMode == MODE_SLEEP) {
      // রাত ১২:০০ – সকাল ৭:০০: ডিসপ্লে ও Wi-Fi দুটোই অফ
      turnWifiOff();
      setDisplayPower(false);
    } 
    else if (currentMode == MODE_ACTIVE) {
      // সকাল ৭টা বা দুপুর ২টা: ডিসপ্লে অন + Wi-Fi একটানা অন + সাথে সাথে সিঙ্ক
      setDisplayPower(true);
      if (ensureWifiConnected()) {
        if (!timeSynced || (millis() - lastNtpSyncMillis >= 86400000UL)) {
          syncNTP();
        }
        fetchFirebaseData();
      }
      lastSyncMillis = millis();
      updateOLEDIfNeeded();
    } 
    else if (currentMode == MODE_IDLE) {
      // সকাল ১১টা বা বিকাল ৫টা: ডিসপ্লে অন থাকবে, Wi-Fi অফ হয়ে রেস্টে যাবে
      setDisplayPower(true);
      turnWifiOff();
      updateOLEDIfNeeded();
    }
  }

  // ২. বর্তমান মোড অনুযায়ী নিয়মিত কাজ
  if (currentMode == MODE_ACTIVE) {
    // Wi-Fi কন্টিনিউয়াসলি অন থাকবে, প্রতি ১ মিনিটে ডেটা রিড করবে
    if (millis() - lastSyncMillis >= ACTIVE_INTERVAL) {
      if (ensureWifiConnected()) {
        if (!timeSynced) syncNTP();
        fetchFirebaseData();
      }
      lastSyncMillis = millis();
      updateOLEDIfNeeded();
    }
  } 
  else if (currentMode == MODE_IDLE) {
    // প্রতি ১ ঘণ্টা পরপর শুধু কয়েক সেকেন্ডের জন্য Wi-Fi অন হয়ে ডেটা আনবে
    if (millis() - lastSyncMillis >= IDLE_INTERVAL) {
      if (ensureWifiConnected()) {
        fetchFirebaseData();
        turnWifiOff();
      }
      lastSyncMillis = millis();
      updateOLEDIfNeeded();
    }
  }

  // দুপুর ১২টা বাজলে অটোমেটিক SOKAL থেকে RAT-এ সুইচ হবে
  if (currentMode != MODE_SLEEP) {
    updateOLEDIfNeeded();
  }

  delay(1000);
}