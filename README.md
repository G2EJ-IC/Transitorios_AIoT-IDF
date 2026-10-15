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
 │  • Pressure transducers (ADC)│              │  • Touch HMI (LVGL 9 + EEZ)  │
 │  • Flow meter (pulse counter)│              │  • WiFi + MQTT to the server │
 │  • IMU accel. + gyro (I2C)   │              │  • Event logging (microSD)   │
 └──────────────────────────────┘              └──────────────────────────────┘
```

- The **sensor node** samples fast and sends compact frames.
- The **communication node** receives them, shows them, stores them and
  forwards them to a broker / server for analysis.

## Repository layout

| Folder | Board | Role | Status |
|---|---|---|---|
| [`Transitorios_AIoT-IDF/`](Transitorios_AIoT-IDF/) | Elecrow CrowPanel 4.3" (ESP32-S3-WROOM-1-N4R2) | Communication node + touch HMI | Working |
| `Sensores_AIoT-IDF/` | ESP32-S3 DevKit | Sensor node | In development |

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
cd Transitorios_AIoT-IDF
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

Private data (WiFi, tokens) is never committed: copy
`components/token/include/token.h.example` to `token.h` inside each project and
fill in your own values.

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
