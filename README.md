# Transitorios AIoT – ESP-IDF firmware for ESP32-S3

Open-source firmware for a **two-board AIoT system** that monitors **hydraulic
transients (water hammer)** in pipes. Everything is written with the official
Espressif framework, **ESP-IDF v5.5.1**, without any Arduino layer.

The project is released to the community under the **AGPL-3.0-or-later**
license.

## System architecture

```
 ┌──────────────────────────────┐   ESP-NOW    ┌──────────────────────────────┐
 │  SENSOR NODE                 │ ───────────▶ │  COMMUNICATION NODE          │
 │  ESP32-S3 DevKit             │              │  CrowPanel 4.3" (ESP32-S3)   │
 │                              │              │                              │
 │  • 4-20 mA pressure (ADC)    │              │  • Touch HMI (LVGL 9 + EEZ)  │
 │  • Flow meter (pulse counter)│              │  • WiFi + MQTT to the server │
 │  • MPU-6050 accel+gyro (I2C) │              │  • Event logging (microSD)   │
 └──────────────────────────────┘              └──────────────────────────────┘
```

- The **sensor node** samples pressure and IMU together at 500 Hz and sends
  100 frames/s (5 samples each, CRC-16) defined in `Frame_AIoT.h`.
- The **communication node** receives them, shows them, stores them and
  forwards them to a broker / server for analysis.

## Repository layout

| Folder | Board | Role | Status |
|---|---|---|---|
| [`Transitorios_AIoT-IDF/`](Transitorios_AIoT-IDF/) | Elecrow CrowPanel 4.3" (ESP32-S3-WROOM-1-N4R2) | Communication node + touch HMI | Working |
| [`Sensores_AIoT-IDF/`](Sensores_AIoT-IDF/) | ESP32-S3 DevKit (ESP32-S3-WROOM-1) | Sensor node | Compiles, hardware test pending |

Each folder is an independent ESP-IDF project with its own `README.md`.

## Hardware libraries (`*_AIoT`)

Both projects share the same family of libraries. Every library follows one
design rule:

- the logic lives in a **C++ class** inside `src/*.cpp`,
- the header exposes a **C API** (`extern "C"`) with an **opaque handle**,
- life cycle: `create()` → `init()` → use → `destroy()`.

| Library | Purpose |
|---|---|
| `UARTn_AIoT` | UART ports, line reader, optional RS-485 half duplex |
| `I2C_AIoT` | I2C master bus and devices (new ESP-IDF driver) |
| `SPI_AIoT` | SPI master bus and devices, bus sharing |
| `ESPNOW_AIoT` | ESP32-to-ESP32 packets without a router |
| `ADC_AIoT` | Continuous ADC with DMA and calibration |
| `PCNT_AIoT` | Hardware pulse counter, flow and volume |
| `SD_AIoT` | microSD (SPI + FAT) logging |
| `MQTT_AIoT` | MQTT client with automatic reconnection |
| `MPU6050_AIoT` | MPU-6050 IMU driver (on top of `I2C_AIoT`) |
| `ADS1115_AIoT` | ADS1115 16-bit external ADC driver (on top of `I2C_AIoT`) |

`Frame_AIoT` (header only) is the ESP-NOW data frame shared by both boards:
it must be identical in both projects.

Each library has its own `README.md` inside its component folder.

## Requirements

| Tool | Version |
|---|---|
| ESP-IDF | v5.5.1 |
| LVGL | 9.6.0 (managed component, communication node) |
| EEZ Studio | LVGL 9 project (communication node UI) |
| Target | ESP32-S3 |

## Quick start

```bash
cd Transitorios_AIoT-IDF        # or: cd Sensores_AIoT-IDF
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

Private data (WiFi, tokens) is never committed: in the CrowPanel project copy
`components/token/include/token.h.example` to `token.h` and fill in your own
values. The sensor node needs no credentials.

## License

Copyright (C) 2026 Ernesto José Guerrero González

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU Affero General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your option) any
later version. See [`LICENSE`](LICENSE).

For use in proprietary or closed-source products, see
[`COMMERCIAL-LICENSE-NOTICE.md`](COMMERCIAL-LICENSE-NOTICE.md).

## Contributing

Issues and pull requests are welcome. By contributing you agree that your
contribution is distributed under the same AGPL-3.0-or-later license.

## Author

**Ing. Ernesto José Guerrero González**
Universidad Distrital Francisco José de Caldas – Bogotá, Colombia
GitHub: [G2EJ-IC](https://github.com/G2EJ-IC)
