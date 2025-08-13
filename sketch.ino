#include <WiFi.h>
#include <FirebaseESP32.h>
#include <LiquidCrystal_I2C.h>
#include "RTClib.h"

// Configuration
#define API_KEY "AIzaSyDrSS7eDfPxqKrNkx5s8mr4X6T5ZQRELWk"
#define DATABASE_URL "https://smart-energy-meter-73045-default-rtdb.europe-west1.firebasedatabase.app/"
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

// Constants
const float VOLTAGE = 220.0, OVERDRAWN_THRESHOLD = 10.0;
const float SPIKE_THRESHOLD = 1.5;
const int POT_PIN = 34, LED_GREEN_PIN = 13, LED_RED_PIN = 12, BUZZER_PIN = 5;
const int RELAY_PIN = 4, EMERGENCY_BUTTON_PIN = 27;
const unsigned long MAIN_LOOP_INTERVAL = 3000, FIREBASE_CHECK_INTERVAL = 3000;
const unsigned long ALERT_TONE_INTERVAL = 40, ALERT_BEEP_DURATION = 200;
const int ALERT_FREQUENCY = 800;

// Objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
RTC_DS1307 rtc;
LiquidCrystal_I2C lcd(0x27, 20, 4);

// Variables
bool isLoadEnabled = true, lastButtonState = HIGH, isAlertToneActive = false;
float previousPower = 0.0, totalEnergyKWh = 0.0;
unsigned long lastMainLoopTime = 0, lastFirebaseCheckTime = 0, alertTimer = 0;

struct SensorReading {
  float current, power, energyKWh;
  unsigned long long timestamp;
};

struct AlertState {
  bool overdrawn, spikeDetected;
  bool hasAnyAlert() const {
    return overdrawn || spikeDetected;
  }
};

void initializeFirebase() {
  Serial.println("🔥 Initializing Firebase...");
  config.database_url = DATABASE_URL;
  config.api_key = API_KEY;
  config.timeout.serverResponse = 10 * 1000;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  for (int retryCount = 0; retryCount < 5; retryCount++) {
    Serial.printf("🔄 Attempting Firebase sign-up (attempt %d/5)...\n", retryCount + 1);
    if (Firebase.signUp(&config, &auth, "", "")) {
      Serial.println("✅ Firebase signed up successfully");
      return;
    }
    Serial.printf("❌ Sign-up failed: %s\n", config.signer.signupError.message.c_str());
    delay(3000);
  }
  Serial.println("⚠️ Firebase initialization failed. System will continue in offline mode.");
}

void checkFirebaseShutdownCommand() {
  static unsigned long lastLogTime = 0;
  if (!Firebase.ready()) {
    if (millis() - lastLogTime > 30000) {
      Serial.println("⚠️ Firebase not ready for remote commands");
      lastLogTime = millis();
    }
    return;
  }

  if (Firebase.getBool(fbdo, "/LoadControl/isEnabled")) {
    bool shouldShutdown = fbdo.boolData();
    if (shouldShutdown != !isLoadEnabled) {
      isLoadEnabled = !shouldShutdown;
      digitalWrite(RELAY_PIN, isLoadEnabled ? HIGH : LOW);
      Serial.printf("🔄 Remote load %s command received!\n", isLoadEnabled ? "enable" : "shutdown");
    }
  }
}

void uploadDataToFirebase(const SensorReading& reading, const AlertState& alerts) {
  static unsigned long lastOfflineLogTime = 0;
  if (WiFi.status() != WL_CONNECTED || !Firebase.ready()) {
    if (millis() - lastOfflineLogTime > 30000) {
      Serial.println("⚠️ Firebase not available, skipping upload");
      lastOfflineLogTime = millis();
    }
    return;
  }

  FirebaseJson json;
  json.set("timestamp", String(reading.timestamp));
  json.set("loadEnabled", isLoadEnabled);
  json.set("current", reading.current);
  json.set("power", reading.power);
  json.set("totalEnergy_kWh", reading.energyKWh);
  json.set("voltage", VOLTAGE);
  json.set("overdrawn", alerts.overdrawn);
  json.set("spikeDetected", alerts.spikeDetected);

  if (Firebase.pushJSON(fbdo, "/data", json)) {
    Serial.println("🔥 Firebase → ✅ Data uploaded successfully");
  } else {
    static unsigned long lastErrorLogTime = 0;
    if (millis() - lastErrorLogTime > 10000) {
      Serial.printf("🔥 Firebase → ❌ Upload failed: %s\n", fbdo.errorReason().c_str());
      lastErrorLogTime = millis();
    }
  }
}

void initializeHardware() {
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  pinMode(POT_PIN, INPUT);
  pinMode(LED_GREEN_PIN, OUTPUT); pinMode(LED_RED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT); pinMode(RELAY_PIN, OUTPUT);
  pinMode(EMERGENCY_BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(LED_GREEN_PIN, LOW); digitalWrite(LED_RED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW); digitalWrite(RELAY_PIN, HIGH);
  delay(100);
  Serial.printf("🔧 Initial ADC test reading: %d\n", analogRead(POT_PIN));
}

void handleEmergencyButton() {
  int currentButtonState = digitalRead(EMERGENCY_BUTTON_PIN);
  if (currentButtonState == LOW && lastButtonState == HIGH) {
    isLoadEnabled = !isLoadEnabled;
    digitalWrite(RELAY_PIN, isLoadEnabled ? HIGH : LOW);
    Serial.printf("🔴 Emergency button pressed! Load %s\n", isLoadEnabled ? "ENABLED" : "DISABLED");

    if (WiFi.status() == WL_CONNECTED && Firebase.ready()) {
      if (!Firebase.setBool(fbdo, "/LoadControl/isEnabled", !isLoadEnabled)) {
        Serial.printf("⚠️ Failed to update Firebase load control: %s\n", fbdo.errorReason().c_str());
      }
    }
  }
  lastButtonState = currentButtonState;
}

void controlAlerts(const AlertState& alerts) {
  digitalWrite(LED_RED_PIN, alerts.hasAnyAlert() ? HIGH : LOW);
  digitalWrite(LED_GREEN_PIN, alerts.hasAnyAlert() ? LOW : HIGH);

  if (alerts.hasAnyAlert()) {
    if (millis() - alertTimer > ALERT_TONE_INTERVAL) {
      isAlertToneActive = !isAlertToneActive;
      if (isAlertToneActive) tone(BUZZER_PIN, ALERT_FREQUENCY, ALERT_BEEP_DURATION);
      // if (isAlertToneActive) tone(BUZZER_PIN, ALERT_FREQUENCY);
      alertTimer = millis();
    }
  } else {
    noTone(BUZZER_PIN);
    isAlertToneActive = false;
  }
}

SensorReading takeSensorReading() {
  DateTime now = rtc.now();
  unsigned long long timestamp = (now.unixtime()) * 1000ULL;
  int potValue = analogRead(POT_PIN);

  float current = (isLoadEnabled && potValue > 0) ? (potValue / 4095.0) * 15.0 : 0.0;
  float power = VOLTAGE * current;
  float energyIncrement = power / 1000.0 / 60.0;
  totalEnergyKWh += energyIncrement;

  return {current, power, totalEnergyKWh, timestamp};
}

AlertState checkAlertConditions(const SensorReading& reading) {
  AlertState alerts = {false, false};

  if (reading.current > OVERDRAWN_THRESHOLD) {
    alerts.overdrawn = true;
    Serial.println("⚠️ ALERT: Overdrawn electricity!");
  }
  if (previousPower > 0 && reading.power / previousPower > SPIKE_THRESHOLD) {
    alerts.spikeDetected = true;
    Serial.println("⚠️ ALERT: Sudden spike in power usage!");
  }

  return alerts;
}

void updateLCDDisplay(const SensorReading& reading) {
  lcd.setCursor(0, 1); lcd.print("Current:"); lcd.print(reading.current, 1); lcd.print("A");
  lcd.setCursor(0, 2); lcd.print("Power:"); lcd.print(reading.power, 2); lcd.print("W");
  lcd.setCursor(0, 3); lcd.print("Energy:"); lcd.print(reading.energyKWh, 3); lcd.print("kWh");
}

void printStatus(const SensorReading& reading) {
  DateTime now = rtc.now();
  Serial.printf("Time: %d/%d/%d %d:%d:%d | Current: %.1fA, Power: %.0fW, Energy: %.3fkWh\n",
                now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second(),
                reading.current, reading.power, reading.energyKWh);
}

void setup() {
  Serial.begin(115200);

  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    abort();
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected! IP: " + WiFi.localIP().toString());

  lcd.init(); lcd.backlight(); lcd.setCursor(0, 0); lcd.print("Smart Energy Meter");
  initializeHardware();

  initializeFirebase();
  Serial.println("Smart Energy Meter initialized successfully!");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) return;

  handleEmergencyButton();

  if (millis() - lastFirebaseCheckTime >= FIREBASE_CHECK_INTERVAL) {
    lastFirebaseCheckTime = millis();
    if (WiFi.status() == WL_CONNECTED && Firebase.ready()) checkFirebaseShutdownCommand();
  }

  if (millis() - lastMainLoopTime >= MAIN_LOOP_INTERVAL) {
    lastMainLoopTime = millis();

    SensorReading reading = takeSensorReading();
    AlertState alerts = checkAlertConditions(reading);

    updateLCDDisplay(reading);
    printStatus(reading);
    controlAlerts(alerts);
    uploadDataToFirebase(reading, alerts);

    previousPower = reading.power;
    Serial.println("______________________________________________");
  }
}