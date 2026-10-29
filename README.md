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

Sensor VCC goes to 3.3V and sensor GND goes to GND.

## How it works

1. The ESP32 reads each IR sensor every 200 ms. When a reading changes and stays stable for 50 ms, it records the event.
2. **Entry:** the slot is set to `occupied` and the entry time is saved.
3. **Exit:** the exit time is read from the clock the moment the sensor stops detecting the car. The duration and charge are calculated and saved, and the visit is added to the slot's history.
4. The dashboard listens to the database and updates instantly. While a car is parked it shows a live running charge. After the car leaves, the final bill stays until **Done** is pressed.

### Billing

Charged per started minute: 1 to 60 seconds is ₹1, 61 to 120 seconds is ₹2, and so on.

## Tech stack

ESP32 (Arduino framework, PlatformIO), Firebase ESP32 Client library, Firebase Realtime Database, HTML, CSS and JavaScript.

## Author

Mayur
