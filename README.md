# Smart Parking System

A 4-slot smart parking system built with an **ESP32**, **IR obstacle sensors** and **Firebase Realtime Database**. The ESP32 detects whether each slot is occupied, records entry and exit times, calculates the parking charge, and a web dashboard shows everything live.

## Features

- Live status for 4 parking slots (free or occupied)
- Entry and exit times recorded from the IR sensor reading, using real clock time from NTP (IST)
- Automatic billing: ₹1 per started minute (easy to change)
- Separate bill for each slot, with a live running amount while a car is parked
- Final bill stays on the dashboard until **Done** is pressed, which resets it
- UPI QR code on the dashboard for payment
- Permanent visit history saved in Firebase for every slot

## Hardware

| Part | Quantity |
|---|---|
| ESP32 development board | 1 |
| IR obstacle sensor module (3-pin) | 4 |
| Jumper wires and breadboard | as needed |

### Wiring

All four sensors share 3.3V and GND.

| Slot | Sensor OUT pin |
|---|---|
| Slot 1 | GPIO 4 |
| Slot 2 | GPIO 18 |
| Slot 3 | GPIO 19 |
| Slot 4 | GPIO 21 |

Sensor VCC goes to 3.3V and sensor GND goes to GND. Most modules output LOW when an object is detected. If yours is reversed, change `== LOW` to `== HIGH` in `main.cpp`.

## Project structure

```
smart-parking-system/
├── platformio.ini
├── src/
│   ├── main.cpp              # ESP32 firmware
│   └── secrets.example.h     # template for your private settings
└── web/
    └── index.html            # live dashboard
```

## Setup

### 1. Firebase

1. Create a project at [console.firebase.google.com](https://console.firebase.google.com).
2. Create a **Realtime Database** and note its URL.
3. Under **Authentication**, enable **Email/Password** and create one user for the ESP32.
4. Enable **Anonymous** sign-in so the dashboard can read data without a login screen.
5. Set the database rules (replace the UID with your ESP32 user's UID from the Authentication tab):

```json
{
  "rules": {
    ".read": "auth != null",
    ".write": "auth.uid == 'PASTE_ESP32_UID_HERE'",
    "$slot": {
      "lastExitTime":  { ".write": "auth != null && !newData.exists()" },
      "lastEntryTime": { ".write": "auth != null && !newData.exists()" },
      "lastDuration":  { ".write": "auth != null && !newData.exists()" },
      "lastCharge":    { ".write": "auth != null && !newData.exists()" }
    }
  }
}
```

Only the ESP32 can write parking data. Dashboard visitors can read it and can only delete the finished-bill fields (this is what the Done button does).

### 2. ESP32 firmware

1. Install [VS Code](https://code.visualstudio.com/) with the **PlatformIO IDE** extension.
2. Open this project folder in VS Code.
3. In the `src` folder, copy `secrets.example.h` to a new file named `secrets.h` and fill in your real values:

```cpp
#define WIFI_SSID "your-wifi-name"
#define WIFI_PASSWORD "your-wifi-password"
#define API_KEY "your-firebase-web-api-key"
#define DATABASE_URL "your-project-default-rtdb.region.firebasedatabase.app"
#define USER_EMAIL "esp32-account@example.com"
#define USER_PASSWORD "esp32-account-password"
```

`DATABASE_URL` must have no `https://` and no trailing slash. `secrets.h` is private, so never upload it to GitHub.

4. Connect the ESP32 by USB, then click **Upload** in PlatformIO.
5. Open **Serial Monitor** at 115200 baud. You should see Wi-Fi connected, time synchronized, Firebase sign-in successful, and a sensor status line every 5 seconds.

The ESP32 connects to 2.4 GHz WiFi only.

### 3. Web dashboard

1. Open `web/index.html` and fill in `firebaseConfig` from Firebase Console, Project settings, General, Your web app.
2. Set `UPI_ID` near the top of the script to your own UPI ID to show the payment QR code.
3. Open the file in a browser to test it, or deploy it with **Firebase Hosting**:

```
npm install -g firebase-tools
firebase login
firebase init hosting
firebase deploy
```

When asked for the public directory, enter `web`.

## How it works

1. The ESP32 reads each IR sensor every 200 ms. When a reading changes and stays stable for 50 ms, it records the event.
2. **Entry:** the slot is set to `occupied` and the entry time is saved.
3. **Exit:** the exit time is read from the clock the moment the sensor stops detecting the car. The duration and charge are calculated and saved, and the visit is added to the slot's history.
4. The dashboard listens to the database and updates instantly. While a car is parked it shows a live running charge. After the car leaves, the final bill stays until **Done** is pressed.

### Billing

Charged per started minute: 1 to 60 seconds is ₹1, 61 to 120 seconds is ₹2, and so on. To change the rate, edit `RATE_PER_MINUTE` in `src/main.cpp` and the same value in `web/index.html`. Both must match.

### Database structure

```
slot1/
  status: "free" | "occupied"
  entryTime, entryEpoch
  lastEntryTime, lastExitTime, lastDuration, lastCharge
  sessions/
    <auto-id>/ { entryTime, exitTime, duration, durationSeconds, charge, ... }
slot2/ slot3/ slot4/ ...
```

## Troubleshooting

| Problem | What to check |
|---|---|
| Serial Monitor shows only dots | Wrong WiFi name or password, or a 5 GHz network |
| `ld returned 1 exit status` with "multiple definition" | Only one `.cpp` file is allowed in `src/` |
| SSL or Firebase connection errors | Weak WiFi, or a library version mismatch. Pin `platform = espressif32@6.9.0` |
| A sensor never changes | Check the OUT wire, shared GND, and the module's sensitivity dial |
| Dashboard shows a permission error | Check the rules and that Anonymous sign-in is enabled |
| Slot shows occupied with a very long time | Old data in Firebase. Set that slot's `status` to `free` and `entryEpoch` to `0` |

## Security notes

- Keep `src/secrets.h` out of GitHub. Add it to `.gitignore`.
- The Firebase web config in `index.html` is public by design. Your database rules are what protect the data.
- Change any password that was ever shared or uploaded by mistake.

## Tech stack

ESP32 (Arduino framework, PlatformIO), Firebase ESP32 Client library, Firebase Realtime Database, HTML, CSS and JavaScript.

## Author

Your Name
