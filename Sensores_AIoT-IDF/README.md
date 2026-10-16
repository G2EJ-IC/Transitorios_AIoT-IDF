# Transitorios AIoT – Sensor Node (ESP32-S3, ESP-IDF)

Sensor node of the **AIoT Hydraulic Transients** system. A plain
**ESP32-S3 DevKit** samples the pipe sensors at a fixed rate and sends the
data to the CrowPanel 4.3" (communication node) over **ESP-NOW**. Written
with the official Espressif framework (**ESP-IDF v5.5.1**), no Arduino layer.

```
 4-20 mA pressure transducers ──► ADC (internal ADC1 or ADS1115)
 MPU-6050 IMU (accel + gyro)  ──► I2C                       ┐
 Pulse flow meter             ──► PCNT (hardware counter)    ├─► frame ─► ESP-NOW ─► CrowPanel
                                                             ┘
```

## What it does

- Samples **pressure and IMU together** at `AIOT_SAMPLE_RATE_HZ` (500 Hz by
  default) using an `esp_timer` clock and a high-priority task on core 1.
- Each pressure value is the **average of every ADC conversion** since the
  previous sample (20 conversions at the default rates), which filters noise
  and aliasing.
- Reads the **flow meter** every 500 ms (flow in L/s and accumulated volume).
- Packs **5 samples per frame** (198 bytes, CRC-16) and sends **100 frames/s**
  from a separate task on core 0, next to WiFi.
- A missing sensor is not fatal: its values are sent as `NaN` and a status bit
  is set, so the receiver always knows what is valid.

## Hardware

| Item | Default |
|---|---|
| Board | ESP32-S3-WROOM-1 DevKit (e.g. Freenove ESP32-S3), any flash / PSRAM size |
| Pressure | 1 or 2 industrial 4-20 mA transducers (e.g. 0-1.2 MPa, G1/4) |
| Pressure ADC | Internal ADC1 (default) or external **ADS1115** 16-bit (1 transducer) |
| IMU | **MPU-6050** (GY-521 module), I2C address 0x68 |
| Flow | Pulse-output flow meter (hall effect / turbine), e.g. YF-S201 |

### Wiring (change pins in `Sensor_Defines_AIoT.h`)

| Signal | GPIO | Notes |
|---|---|---|
| Pressure 1 | 4 | ADC1_CH3, 150 Ohm shunt to GND |
| Pressure 2 | 5 | ADC1_CH4, 150 Ohm shunt to GND |
| I2C SDA | 11 | MPU-6050 (and ADS1115 if used) |
| I2C SCL | 12 | |
| Flow pulses | 14 | Internal pull-up, open-collector output |
| UART1 TX (link) | 17 | To CrowPanel UART1 IO18 (RX) |
| UART1 RX (link) | 18 | From CrowPanel UART1 IO17 (TX), join GND too |

4-20 mA loop (two-wire transducer):

```
 +24 V ──► transducer (+)
           transducer (−) ──┬──► 1 kOhm ──► GPIO 4  (100 nF to GND at the pin)
                            │
                         150 Ohm shunt
                            │
 0 V (supply) ──────────────┴──► GND of the ESP32-S3 (common ground)
```

`I = V / R`: 4 mA → 0.6 V, 20 mA → 3.0 V, inside the ADC range. Below 3.6 mA
the loop is considered open (`NaN` + fault bit); above 21 mA it is flagged as
over range.

## Data frame (`components/Frame_AIoT`)

`Frame_AIoT.h` is the contract between the two boards: **copy the
`Frame_AIoT` folder unchanged into the CrowPanel project**.

| Field | Type | Meaning |
|---|---|---|
| `magic`, `version` | u8, u8 | 0xA5, 1 |
| `node_id` | u8 | sensor node number |
| `n_samples` | u8 | valid samples (5) |
| `seq` | u32 | frame counter (gaps = lost frames) |
| `t0_us` | u64 | node time of the first sample [us] |
| `period_us` | u32 | time between samples [us] |
| `status` | u16 | fault bits (`AIOT_STATUS_*`) |
| `flow_lps`, `volume_l`, `temp_c` | float | flow [L/s], volume [L], IMU temperature [degC] |
| `s[5]` | 5 × 32 bytes | `p_kpa[2]` [kPa], `acc[3]` [m/s2], `gyr[3]` [rad/s] |
| `crc` | u16 | CRC-16/CCITT-FALSE of all previous bytes |

Receiver side (C): `if (AIoT_frame_check(pkt.data, pkt.len)) { const AIoT_frame_t *f = ...; }`

Receiver side (Python):

```python
import struct, binascii
v = struct.unpack("<BBBBIQIHHfff" + "8f" * 5 + "H", payload)
ok = binascii.crc_hqx(payload[:196], 0xFFFF) == v[-1]
```

Piezometric head for the analysis: `H [m] = p [kPa] / 9.81 + z`.

## Project structure

```
components/
  Configuracion_AIoT/   Sensor_Defines_AIoT.h: pins, rates, calibration
  Adquisicion_AIoT/     Sampling task, frame building, ESP-NOW TX task
  Frame_AIoT/           ESP-NOW frame shared with the CrowPanel (header only)
  ADC_AIoT/             Continuous ADC with DMA and calibration
  ADS1115_AIoT/         ADS1115 16-bit external ADC driver
  MPU6050_AIoT/         MPU-6050 IMU driver
  I2C_AIoT/             I2C master bus and devices
  PCNT_AIoT/            Hardware pulse counter, flow and volume
  ESPNOW_AIoT/          ESP-NOW link
  Enlace_AIoT/          UART "hello" link test with the CrowPanel (shared)
  UARTn_AIoT/           UART ports, line reader, optional RS-485 (spare)
  SPI_AIoT/             SPI master bus and devices (spare)
  SD_AIoT/              microSD (SPI + FAT) logging (spare)
  MQTT_AIoT/            MQTT client (spare, needs a WiFi access point)
main/                   app_main: NVS, start, console summary
partitions/             Partition tables (CSV); huge_app_v2.csv is the one in use
sdkconfig.defaults      Board settings (target, partitions, no PSRAM, WiFi / ESP-NOW radio only)
```

Every hardware library follows the same rule: **C++ class** in `src/*.cpp`,
**C API** (`extern "C"`) with an **opaque handle**, life cycle
`create()` → `init()` → use → `destroy()`. Each one has its own `README.md`.

## Configuration

Everything is in `components/Configuracion_AIoT/include/Sensor_Defines_AIoT.h`:

| Define | Default | Meaning |
|---|---|---|
| `AIOT_SAMPLE_RATE_HZ` | 500 | Samples per second (max 1000; ADS1115 max 860) |
| `AIOT_ESPNOW_CHANNEL` | 6 | Must match the WiFi channel the CrowPanel uses |
| `AIOT_PEER_MAC` | broadcast | CrowPanel MAC for unicast (ACK + retries) |
| `AIOT_PRESSURE_SOURCE` | ADC | `AIOT_PRESSURE_SOURCE_ADC` or `AIOT_PRESSURE_SOURCE_ADS1115` |
| `AIOT_PRESSURE_CHANNELS` | 2 | 1 or 2 transducers (ADS1115: 1) |
| `AIOT_P1_MAX_KPA` / `AIOT_P2_MAX_KPA` | 1200 | Transducer full scale |
| `AIOT_P1_OFFSET_KPA` / `AIOT_P2_OFFSET_KPA` | 0 | Zero correction |
| `AIOT_FLOW_K_PULSES_PER_L` | 450 | Flow meter K factor |
| `AIOT_IMU_ENABLED` / `AIOT_FLOW_ENABLED` | 1 | Set to 0 if the sensor is not installed |

Invalid combinations are stopped at compile time (`_Static_assert`).

## Partition table

`partitions/huge_app_v2.csv` (4 MB flash), selected in `sdkconfig.defaults`:

| Name | Type | Offset | Size | Use |
|---|---|---|---|---|
| nvs | data | 0x9000 | 20 KB | WiFi / ESP-NOW data |
| otadata | data | 0xE000 | 8 KB | Boot selection |
| app0 | app | 0x10000 | 3 MB | Firmware (about 0.8 MB used) |
| spiffs | data | 0x310000 | 896 KB | Free for future files |
| coredump | data | 0x3F0000 | 64 KB | Crash dump: `idf.py -p COMx coredump-info` |

To use another table of the folder, change only
`CONFIG_PARTITION_TABLE_CUSTOM_FILENAME`, then delete `sdkconfig` and run
`idf.py fullclean`.

## Build

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

`sdkconfig.defaults` is only applied when `sdkconfig` does not exist yet.
After changing it, delete `sdkconfig` and run `idf.py fullclean`.

Expected console output (every 5 s):

```
main: Transitorios AIoT - sensor node 1 - MAC xx:xx:xx:xx:xx:xx
Adquisicion: Running -> 500 Hz, 5 samples per frame (198 bytes), 100 frames/s, node 1
main: frames 500 sent 500 fail 0 drop 0 | late 0 imu_err 0 adc_ovr 0 | OK
main: P1 101.25 kPa  P2 99.80 kPa | Q 0.0000 L/s  V 0.00 L | a 0.02 -0.05 9.79 m/s2 | ...
```

## Notes

- Keep the node **still for 0.2 s at power-up**: the gyroscope offset is
  measured then (`AIOT_IMU_GYRO_CAL_SAMPLES`).
- `t0_us` is the node's own clock; use `seq` to detect lost frames and
  `period_us` to rebuild the time axis.
- At 100 frames/s the radio is busy about 20 % of the time on the shared
  channel. Lower `AIOT_SAMPLE_RATE_HZ` if the CrowPanel WiFi link suffers.
- The CrowPanel must receive with WiFi power save disabled
  (`esp_wifi_set_ps(WIFI_PS_NONE)`), otherwise it misses ESP-NOW frames.

## License

Copyright (C) 2026 Ernesto José Guerrero González

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU Affero General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your option) any
later version. See [`LICENSE`](LICENSE).

For use in proprietary or closed-source products, see
[`COMMERCIAL-LICENSE-NOTICE.md`](COMMERCIAL-LICENSE-NOTICE.md).

## Author

**Ing. Ernesto José Guerrero González**
Universidad Distrital Francisco José de Caldas – Bogotá, Colombia
GitHub: [G2EJ-IC](https://github.com/G2EJ-IC)
