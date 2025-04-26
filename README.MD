# Smart Waste Monitoring - Arduino Firmware

This repository contains the Arduino firmware for the **Smart Waste Monitoring** project, part of a larger system developed for the course **Cloud and Fog Computing in the Internet of Things**.

This firmware is designed to run on microcontrollers like ESP32, responsible for collecting sensor data and securely transmitting it to AWS IoT Core.

> **Note**: This repository is part of a larger project, which also includes separate repositories for Deep Learning model training and AWS Lambda functions.

---

## Features

- Read real-time sensor data:
  - **Weight** sensor (HX711)
  - **Gas** sensors (MQ-135 for NH3, MQ-4 for CH4)
  - **Distance** sensor (HC-SR04) to calculate bin fill percentage
  - **Real Time Clock (RTC)** for timestamp generation
- **MQTT Communication** with AWS IoT Core (TLS secure connection)
- **Simulation Mode** for testing without physical sensors
- **Deep Sleep Mode** (available in the `main` branch, production-ready)

---

## Project Structure

- `main.cpp` - Main application logic
- `secrets.example.h` - Example of the credentials and certificates file (should be renamed to `secrets.h`)

---

## Setup Instructions

1. **Clone this repository**

2. **Rename `secrets.example.h` to `secrets.h`**

3. **Edit `secrets.h`** and fill in your:

   - WiFi SSID and password
   - AWS IoT Core endpoint
   - AWS Root CA, device certificate, and private key

4. **Install PlatformIO** (VSCode extension or CLI)

5. **Select your board** (e.g., ESP32 Dev Module) in `platformio.ini`

6. **Build and upload** the firmware to your device:

   ```bash
   pio run --target upload
   ```

7. **Monitor serial output**:
   ```bash
   pio device monitor
   ```

---

## Mode Selection

In `main.cpp`, the data source can be selected by changing the `MODE` constant:

```cpp
#define MODE 2 // 1 = Real sensor data, 2 = Simulated data
```

- `MODE 1` : Use actual sensors
- `MODE 2` : Use random simulated data (useful for development without hardware)

---

## Sensors and Components

| Sensor/Component | Purpose                         |
| ---------------- | ------------------------------- |
| HX711            | Weight measurement              |
| MQ-135           | NH3 gas detection               |
| MQ-4             | CH4 gas detection               |
| HC-SR04          | Distance measurement (bin fill) |
| DS3231           | Real-Time Clock (RTC)           |

---

## License

This project is intended for educational purposes.

---

## Author

Developed for the course **Cloud and Fog Computing in IoT**.

---
