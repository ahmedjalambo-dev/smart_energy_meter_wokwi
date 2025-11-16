## Smart Energy Meter (Firmware)

This repository contains the Arduino (C++) firmware for an ESP32-based Smart Energy Meter. The project is designed and simulated in the Wokwi online simulator.

This device monitors simulated electricity usage, calculates power and energy consumption, and synchronizes all data and alerts with a Firebase Realtime Database. It also listens for remote commands from the dashboard to control a relay.

### Simulation

This project can be run directly on Wokwi:
**Wokwi Project Link:** `https://wokwi.com/projects/437696214065838081`

### ⚡ Features

  * **Real-time Monitoring:** Reads simulated current from a potentiometer and calculates power (W) and total energy (kWh).
  * **Firebase Integration:**
      * Uploads sensor data (current, power, energy, timestamp) to the Firebase Realtime Database.
      * Pushes alert statuses (overdrawn, spike detected) to Firebase.
      * Listens for remote `isEnabled` commands from Firebase to control the load relay.
  * **Hardware Alerts:** Activates a red LED and a buzzer if the current exceeds `OVERDRAWN_THRESHOLD` (10.0A) or if a sudden power spike is detected.
  * **Local Control:** An emergency button (`EMERGENCY_BUTTON_PIN`) allows toggling the load relay manually.
  * **Local Display:** A 20x4 I2C LCD screen displays the current, power, and total energy consumption.
  * **Timekeeping:** Uses a DS1307 RTC (Real-Time Clock) module to timestamp all data entries.

### ⚙️ Hardware Components

  * ESP32 DevKit C V4
  * 20x4 I2C LCD
  * DS1307 RTC Module
  * Slide Potentiometer (to simulate current draw)
  * Relay Module
  * Red LED (Alert)
  * Green LED (Normal)
  * Buzzer
  * Push Button
  * Resistors

### 📚 Libraries Used

  * `WiFi.h`
  * `FirebaseESP32.h`
  * `LiquidCrystal_I2C.h`
  * `RTClib.h`

### 🔧 Configuration

To use this project, you must update the following credentials in `sketch.ino`:

1.  **Firebase API Key & URL:**
    ```cpp
    #define API_KEY "AIzaSyDrSS7eDfPxqKrNkx5s8mr4X6T5ZQRELWk"
    #define DATABASE_URL "https://smart-energy-meter-73045-default-rtdb.europe-west1.firebasedatabase.app/"
    ```
2.  **WiFi Credentials:**
    ```cpp
    const char* WIFI_SSID = "Wokwi-GUEST";
    const char* WIFI_PASSWORD = "";
    ```
