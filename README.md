# 🌊 AIoT Hydraulic Transients Prediction using CNNs

This project develops an advanced IoT monitoring system and a Human-Machine Interface (HMI) to predict **Water Hammer** (hydraulic transients) in scale models. The system integrates Deep Learning (CNNs) with real-time data acquisition from a network of 16-channel sensor nodes.

Built for the **Universidad Distrital Francisco José de Caldas** as part of a Master’s in Civil Engineering research.

---

## 👨‍🏫 Project Leadership & Authors

* **Principal Investigator:** Ernesto José Guerrero González, IC, Esp.
    * *Affiliation:* Professor at Universidad Distrital Francisco José de Caldas, Bogotá, Colombia.
    * *Focus:* Strength of Materials & Concrete Technology.
* **Co-Author:** Juan Paulo Delgado Ordoñez, IC.
* **Director:** Edgar Orlando Ladino Moreno, IC, PhD, MSc.

---

## 🔬 Scientific & Research Framework

The project implements a hybrid approach combining classical hydraulic theory with Artificial Intelligence:

1. **Stationary Regime:** Solved via the **Gradient Method** (Todini & Pilati), expressed in matrix form for network flow and piezometric head calculation.
2. **Transient Regime:** Based on **Wave Theory** (Newton/Zhukovsky) and solved through the Method of Characteristics, used as the ground truth for AI validation.
3. **Predictive Model:** A **1D-Convolutional Neural Network (1D-CNN)** architecture designed to detect pressure pulsations and predict structural risks in real-time.

---

## 🛠️ Hardware Platform: CrowPanel ESP32 HMI

The HMI is deployed on the **CrowPanel 4.3-inch Display**, serving as the master node for the sensor network.

### Key Specifications

* **Core:** ESP32-S3 (Dual-core, Wi-Fi/BLE).
* **Display:** 4.3" RGB LCD (480x272).
* **Touch:** XPT2046 (SPI).
* **Sensor Nodes:** Custom-built modules with 16-channel capacity for pressure transducers, flow meters, and accelerometers.

---

## ⚙️ Software Architecture (Strictly ESP-IDF)

This project is developed using **Visual Studio Code** and the **Espressif IoT Development Framework (ESP-IDF) v5.x**.

> **Note:** PlatformIO is not supported.

### Core Modules

* **`main_AIoT`**: Manages FreeRTOS tasks and system orchestration.
* **`Configuracion_AIoT`**: Low-level HAL (Hardware Abstraction Layer) for RGB bus and SPI.
* **`WiFi_AIoT`**: Robust connectivity driver with automatic reconnection and status monitoring.
* **`EEZ_AIoT`**: UI logic generated via **EEZ Studio**, integrating C++ data binding with LVGL.

---

## 🚀 Installation & Build

Ensure you have the ESP-IDF environment correctly configured in VSCode.

```bash
# Clone the repository
git clone https://github.com/G2EJ-IC/Transitorios_AIoT-IDF.git

# Enter the project directory
cd Transitorios_AIoT-IDF

# Set the target to ESP32-S3
idf.py set-target esp32s3

# Build and flash
idf.py build
idf.py -p [PORT] flash monitor
```

---

## 📜 Licensing

### Software and firmware

Unless otherwise noted, the software and firmware in this repository are licensed under the **GNU Affero General Public License v3.0 (AGPL-3.0-or-later)**.

This means you may use, study, modify, and redistribute the code, including for commercial purposes, provided that:

- copyright and license notices are preserved;
- modified versions are also released under AGPL when distributed; and
- if the software is used to provide a network service, users interacting with that service can access the corresponding source code of the running modified version.

### Documentation

Unless otherwise noted, project documentation, figures, explanatory texts, and other non-software written materials are licensed under **Creative Commons Attribution 4.0 International (CC BY 4.0)**.

### Hardware

Unless otherwise noted, hardware design files, schematics, PCB layouts, and related manufacturing documentation are intended to be released under **CERN Open Hardware Licence Version 2 - Strongly Reciprocal (CERN-OHL-S v2)**.

### Attribution and citation

If you use this project in academic work, teaching material, derived repositories, or technical reports, please provide visible attribution to the original authors and cite the repository and associated university research outputs.

### Commercial licensing

For organizations that want to integrate this work into proprietary products, closed services, or commercial solutions without complying with AGPL reciprocal obligations, a separate **commercial license** may be available by written agreement with the copyright holders.

See also: `COMMERCIAL-LICENSE-NOTICE.md`

### Authors

Copyright (c) 2025-2026 Ernesto José Guerrero González and Juan Paulo Delgado Ordoñez.
