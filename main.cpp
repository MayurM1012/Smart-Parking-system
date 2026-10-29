#include <Arduino.h>
#include <WiFi.h>
#include <FirebaseESP32.h>
#include <time.h>
#include "secrets.h"

// ---------- Sensor pins ----------
#define IR_SENSOR_1_PIN 4
#define IR_SENSOR_2_PIN 18
#define IR_SENSOR_3_PIN 19
#define IR_SENSOR_4_PIN 21

// ---------- Time settings (IST = UTC+5:30) ----------
#define GMT_OFFSET_SEC 19800
#define DAYLIGHT_OFFSET_SEC 0
#define NTP_SERVER "pool.ntp.org"

// ---------- Billing: ₹1 per started minute ----------
#define RATE_PER_MINUTE 1.0

FirebaseData firebaseData;
FirebaseConfig config;
FirebaseAuth auth;

bool signupOK = false;

// ---------- Parking slot structure ----------
struct ParkingSlot {
  const char* name;
  int pin;
  bool lastState;
  time_t entryTime;
};

ParkingSlot slots[] = {
  { "slot1", IR_SENSOR_1_PIN, false, 0 },
  { "slot2", IR_SENSOR_2_PIN, false, 0 },
  { "slot3", IR_SENSOR_3_PIN, false, 0 },
  { "slot4", IR_SENSOR_4_PIN, false, 0 }
};

const int NUM_SLOTS = sizeof(slots) / sizeof(slots[0]);

// ---------- Format epoch time as YYYY-MM-DD HH:MM:SS ----------
String formatTime(time_t t) {
  if (t <= 0) return "";
  struct tm timeinfo;
  localtime_r(&t, &timeinfo);
  char buf[25];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buf);
}

// ---------- Format duration as 2h 15m 30s ----------
String formatDuration(long seconds) {
  if (seconds < 0) seconds = 0;
  long h = seconds / 3600;
  long m = (seconds % 3600) / 60;
  long s = seconds % 60;

  String out = String(h);
out += "h ";
out += String(m);
out += "m ";
out += String(s);
out += "s";
return out;
}

// ---------- Charge per started minute ----------
// 1–60 seconds = ₹1; 61–120 seconds = ₹2, etc.
float calculateCharge(long seconds) {
  if (seconds <= 0) return 0.0;
  long minutesStarted = (seconds + 59) / 60;
  return minutesStarted * RATE_PER_MINUTE;
}

// ---------- Write a slot's initial status without clearing history ----------
bool initialiseSlotIfMissing(ParkingSlot &slot) {
  String base = "/";
  base += slot.name;
  String statusPath = base;
  statusPath += "/status";

  if (Firebase.getString(firebaseData, statusPath)) {
    // Existing status found. Do not overwrite it during startup.
    return true;
  }

  // If status doesn't exist yet, create an initial free status.
  if (Firebase.setString(firebaseData, statusPath, "free")) {
    return true;
  }

  Serial.print("[");
  Serial.print(slot.name);
  Serial.print("] Could not read/create status: ");
  Serial.println(firebaseData.errorReason());
  return false;
}

// ---------- Handle a car entering a slot ----------
void handleEntry(ParkingSlot &slot) {
  slot.entryTime = time(nullptr);
  if (slot.entryTime <= 0) {
    Serial.println("Clock is not valid; entry timestamp was not recorded.");
    return;
  }

  String entryStr = formatTime(slot.entryTime);
  String base = "/";
  base += slot.name;
  Serial.print("[");
  Serial.print(slot.name);
  Serial.print("] OCCUPIED at ");
  Serial.println(entryStr);

  FirebaseJson json;
  json.set("status", "occupied");
  json.set("entryTime", entryStr);
  json.set("entryEpoch", (int)slot.entryTime);

  if (Firebase.updateNode(firebaseData, base, json)) {
    Serial.println("  -> Entry recorded in Firebase");
  } else {
    Serial.print("  -> Firebase entry write failed: ");
    Serial.println(firebaseData.errorReason());
  }
}

// ---------- Handle a car leaving a slot ----------
void handleExit(ParkingSlot &slot) {
  time_t exitTime = time(nullptr);

  long durationSec = 0;
  if (slot.entryTime > 0 && exitTime >= slot.entryTime) {
    durationSec = (long)(exitTime - slot.entryTime);
  }

  float charge = calculateCharge(durationSec);
  String entryStr = formatTime(slot.entryTime);
  String exitStr = formatTime(exitTime);
  String durStr = formatDuration(durationSec);
  String base = "/";
  base += slot.name;
  Serial.print("[");
  Serial.print(slot.name);
  Serial.print("] FREE at ");
  Serial.println(exitStr);
  Serial.print("  Duration: ");
  Serial.println(durStr);
  Serial.print("  Charge: ");
  Serial.println(charge);

  // Update current slot status and most recent completed session details.
  FirebaseJson live;
  live.set("status", "free");
  live.set("lastExitTime", exitStr);
  live.set("lastDuration", durStr);
  live.set("lastCharge", charge);

  if (Firebase.updateNode(firebaseData, base, live)) {
    Serial.println("  -> Live slot status updated");
  } else {
    Serial.print("  -> Firebase live update failed: ");
    Serial.println(firebaseData.errorReason());
  }

  // Store a permanent history record under /slotN/sessions.
  String historyPath = base;
  historyPath += "/sessions";
  FirebaseJson session;
  session.set("entryTime", entryStr);
  session.set("exitTime", exitStr);
  session.set("entryEpoch", (int)slot.entryTime);
  session.set("exitEpoch", (int)exitTime);
  session.set("durationSeconds", (int)durationSec);
  session.set("duration", durStr);
  session.set("ratePerMinute", RATE_PER_MINUTE);
  session.set("charge", charge);

  if (Firebase.pushJSON(firebaseData, historyPath, session)) {
    Serial.println("  -> Session saved to history");
  } else {
    Serial.print("  -> Firebase history write failed: ");
    Serial.println(firebaseData.errorReason());
  }

  slot.entryTime = 0;
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  delay(200);

  for (int i = 0; i < NUM_SLOTS; i++) {
    // INPUT assumes the sensor board provides a defined HIGH/LOW output.
    pinMode(slots[i].pin, INPUT);
  }

  // Connect to Wi-Fi.
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("Wi-Fi connection failed. Status: ");
    Serial.println(WiFi.status());
    return;
  }

  Serial.print("Connected. ESP32 IP: ");
  Serial.println(WiFi.localIP());

  // Sync clock from NTP.
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  Serial.print("Synchronizing clock");
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo, 1000)) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  Serial.print("Time synchronized: ");
  Serial.println(formatTime(time(nullptr)));

  // Configure Firebase using private values from secrets.h.
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.println("Firebase initialized; waiting for authentication...");
  unsigned long authStart = millis();
  while (auth.token.uid.length() == 0 && millis() - authStart < 15000) {
    delay(300);
  }

  if (auth.token.uid.length() == 0) {
    Serial.println("Firebase authentication failed. Check secrets.h, Authentication email/password, API key and database URL.");
    return;
  }

  signupOK = true;
  Serial.println("Firebase sign-in successful.");
  Serial.print("ESP32 Firebase UID: ");
  Serial.println(auth.token.uid.c_str());

  // Create status only if missing; don't reset active slots or erase history.
  for (int i = 0; i < NUM_SLOTS; i++) {
    initialiseSlotIfMissing(slots[i]);
  }

  // Seed sensor states so the first loop doesn't falsely create an entry/exit.
  // If a sensor is already occupied on boot, restore its entry epoch from Firebase.
  for (int i = 0; i < NUM_SLOTS; i++) {
    ParkingSlot &slot = slots[i];
    bool occupiedNow = (digitalRead(slot.pin) == LOW);
    slot.lastState = occupiedNow;

String base = "/";
base += slot.name;
    if (occupiedNow) {
String entryEpochPath = base;
entryEpochPath += "/entryEpoch";

if (Firebase.getInt(firebaseData, entryEpochPath)) {        slot.entryTime = (time_t)firebaseData.intData();
      }
      if (slot.entryTime <= 0) {
        // The car is detected but there is no stored start time.
        // Start a new session now so duration/billing work from this point.
        slot.entryTime = time(nullptr);
        FirebaseJson json;
        json.set("status", "occupied");
        json.set("entryTime", formatTime(slot.entryTime));
        json.set("entryEpoch", (int)slot.entryTime);
        Firebase.updateNode(firebaseData, base, json);
        Serial.print("[");
        Serial.print(slot.name);
        Serial.println("] Occupied at boot; started a new session now.");
      } else {
        Serial.print("[");
        Serial.print(slot.name);
        Serial.println("] Occupied at boot; restored entry timestamp.");
      }
String statusPath = base;
statusPath += "/status";
Firebase.setString(firebaseData, statusPath, "occupied");    } else {
      // Sensor says empty. If Firebase had stale occupied status, set it free.
String path = base;
path += "/status";
Firebase.setString(firebaseData, path, "free");      slot.entryTime = 0;
    }
  }
}

// ---------- Main loop ----------
void loop() {
  if (!signupOK) {
    delay(1000);
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    delay(500);
    return;
  }

  for (int i = 0; i < NUM_SLOTS; i++) {
    ParkingSlot &slot = slots[i];
    bool occupied = (digitalRead(slot.pin) == LOW);

    if (occupied != slot.lastState) {
      delay(50); // debounce
      bool confirm = (digitalRead(slot.pin) == LOW);
      if (confirm != occupied) continue;

      if (occupied) {
        handleEntry(slot);
      } else {
        handleExit(slot);
      }

      slot.lastState = occupied;
    }
  }

  delay(200);
}
