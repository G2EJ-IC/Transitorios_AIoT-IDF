# 🌊 AIoT Transitorios Hidráulicos: CrowPanel HMI Interface

This project develops a Human-Machine Interface (HMI) for monitoring hydraulic transients, built on the Espressif ESP32-S3 platform. It utilizes the power of FreeRTOS, ESP-IDF, and LVGL (Light and Versatile Graphics Library) to provide real-time data visualization and configuration capabilities.

---

## 👨‍🏫 Project Leadership & Authors

* **Principal Investigator / Professor:** Ernesto José Guerrero González
    * *Affiliation:* Universidad Distrital Francisco José de Caldas, Bogotá, D.C., Colombia [cite: 2025-11-21]
    * *Role:* Professor of Materials Resistance and Concrete Technology [cite: 2025-11-21]
* **Co-Author:** Juan Pablo Delgado Ordoñez (As observed on the device startup screen)

---

## 🛠️ Hardware Platform: CrowPanel ESP32 HMI

The project is specifically designed for the CrowPanel 4.3-inch HMI Display, which integrates an ESP32-S3 microcontroller with a 480x272 RGB LCD screen and an XPT2046 touch controller.

This platform is crucial for our AIoT (Artificial Intelligence of Things) application due to its robust Wi-Fi/BLE connectivity and integrated PSRAM for efficient LVGL handling.

### Key Hardware Specifications
* **Microcontroller:** ESP32-S3
* **Display:** 4.3-inch RGB LCD (480x272 resolution)
* **Touch Controller:** XPT2046 (SPI)
* **Connectivity:** Wi-Fi and Bluetooth LE

### 🔗 Manufacturer and Technical Links

| Description | Link |
| :--- | :--- |
| **CrowPanel Wiki (Datasheet & Setup)** | [CrowPanel ESP32 HMI 4.3-inch Display - Elecrow Wiki](https://www.elecrow.com/wiki/esp32-display-432727-intelligent-touch-screen-wi-fi26ble-480272-hmi-display.html) |
| **Elecrow GitHub Repository** | [Elecrow-RD/CrowPanel-4.3-HMI-ESP32-Display-480x272](https://github.com/Elecrow-RD/CrowPanel-4.3-HMI-ESP32-Display-480x272/tree/master) |
| **XPT2046 Touch Driver (Reference)** | [PaulStoffregen/XPT2046\_Touchscreen](https://github.com/PaulStoffregen/XPT2046_Touchscreen) |

---

## ⚙️ Software Architecture

The software is built upon the Espressif IoT Development Framework (ESP-IDF) using a highly modular approach to separate hardware drivers from application logic. 

[Image of Project Architecture Diagram]


### Key Modules:

* **`main_AIoT`:** The main orchestration file, managing FreeRTOS tasks, initialization (NVS, Hardware), and the LVGL main loop.
* **`Configuracion_AIoT`:** Initializes the low-level hardware components (RGB Display bus, SPI for XPT2046) and the core LVGL setup.
* **`IO_AIoT`:** **Power and System Management.** Handles Backlight PWM control, screen dimming/suspension logic based on user inactivity, and calculates the system uptime.
* **`WiFi_AIoT`:** **Connectivity Driver.** Manages the STA mode connection state, handles IP/MAC/DNS retrieval, and provides the robust connection uptime timer required by the UI.
* **`EEZ_AIoT (UI Component)`:**
    * **`actions.cpp`:** Contains all the custom business logic, event handlers (e.g., connecting to WiFi, setting suspension time), and the periodic logic that updates the clocks and sensor data.
    * **`vars.cpp`:** Manages the global state variables (Data Binding) used to synchronize data between the C++ logic and the LVGL user interface created with EEZ Studio.

---

## 📈 Current Development Status (Example)

### **Module: WiFi Connection Clock Fix (Resolved)**

* **Issue:** The WiFi connection uptime clock remained static and did not reset to zero upon disconnection.
* **Resolution:** The logic was stabilized by forcing a direct LVGL update on the `label_dhms_wi_fi` object inside the periodic task, resolving a conflict with the EEZ Studio Data Binding mechanism. The `WiFi_AIoT.c` driver was confirmed to correctly provide the `00:00:00` string when disconnected.

***