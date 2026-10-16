/*
 * File: components/Configuracion_AIoT/include/Sensor_Defines_AIoT.h
 * Description: Every pin, rate and calibration constant of the SENSOR NODE
 *              (ESP32-S3 DevKit). Change the hardware here, not in the code.
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, C11. English comments.
 *
 * Default wiring (ESP32-S3-WROOM-1 DevKit, e.g. Freenove ESP32-S3):
 *
 *   Signal                     GPIO   Notes
 *   Pressure 1 (4-20 mA)       4      ADC1_CH3, 150 Ohm shunt to GND
 *   Pressure 2 (4-20 mA)       5      ADC1_CH4, 150 Ohm shunt to GND
 *   I2C SDA (MPU-6050/ADS1115) 11
 *   I2C SCL                    12
 *   Flow meter pulses          14     internal pull-up (open collector)
 *   UART1 TX -> CrowPanel RX   17     link test (Enlace_AIoT)
 *   UART1 RX <- CrowPanel TX   18     link test (Enlace_AIoT)
 *
 * Pins to avoid on the ESP32-S3: 0, 3, 45, 46 (strapping), 19-20 (USB),
 * 26-32 (flash), 33-37 (octal PSRAM on N8R8 / N16R8 modules).
 */
#ifndef SENSOR_DEFINES_AIOT_H
#define SENSOR_DEFINES_AIOT_H

/* ==========================================================================
 * Node identity and sampling
 * ========================================================================== */
#define AIOT_NODE_ID                 1        /* sent in every frame */
#define AIOT_SAMPLE_RATE_HZ          500      /* pressure + IMU samples per second (max 1000) */
#define AIOT_STATS_PERIOD_MS         5000     /* console summary period */

/* ==========================================================================
 * ESP-NOW link to the CrowPanel
 * ========================================================================== */
/* Must be the channel of the WiFi network the CrowPanel is connected to
 * (the "Transitorios_AIoT" access point uses channel 6). */
#define AIOT_ESPNOW_CHANNEL          6
/* CrowPanel STA MAC address. FF:FF:FF:FF:FF:FF = broadcast (works without
 * configuration, but has no ACK / retries). For unicast, copy the MAC that
 * the CrowPanel prints at boot ("wifi:mode : sta (xx:xx:xx:xx:xx:xx)"). */
#define AIOT_PEER_MAC                { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }
#define AIOT_TX_QUEUE_LEN            16       /* frames waiting for the radio */

/* ==========================================================================
 * Tasks (WiFi / ESP-NOW live on core 0)
 * ========================================================================== */
#define AIOT_ACQ_TASK_CORE           1
#define AIOT_ACQ_TASK_PRIORITY       20
#define AIOT_ACQ_TASK_STACK          6144
#define AIOT_TX_TASK_CORE            0
#define AIOT_TX_TASK_PRIORITY        5
#define AIOT_TX_TASK_STACK           4096

/* ==========================================================================
 * I2C bus (MPU-6050 and optional ADS1115)
 * ========================================================================== */
#define AIOT_I2C_PORT                0
#define AIOT_I2C_SDA_PIN             11
#define AIOT_I2C_SCL_PIN             12
#define AIOT_I2C_SPEED_HZ            400000
#define AIOT_I2C_INTERNAL_PULLUP     true     /* GY-521 / ADS1115 modules also have pull-ups */

/* ==========================================================================
 * Pressure transducers (4-20 mA through a shunt resistor)
 * ========================================================================== */
#define AIOT_PRESSURE_SOURCE_ADC     0        /* internal ADC1 (ADC_AIoT), 1 or 2 inputs */
#define AIOT_PRESSURE_SOURCE_ADS1115  1       /* external ADS1115 16-bit, 1 input */
#define AIOT_PRESSURE_SOURCE         AIOT_PRESSURE_SOURCE_ADC

#define AIOT_PRESSURE_CHANNELS       2        /* 1 or 2 (ADS1115: 1) */

/* Internal ADC: GPIO 1..10 only (ADC1). Total rate shared by the channels;
 * every sample sent is the average of all conversions since the previous one
 * (20000 Hz / 2 channels / 500 Hz = 20 conversions averaged). */
#define AIOT_P1_GPIO                 4
#define AIOT_P2_GPIO                 5
#define AIOT_ADC_TOTAL_RATE_HZ       20000

/* ADS1115: address 0x48 (ADDR to GND), transducer on AIN0, +/-4.096 V, 860 SPS */
#define AIOT_ADS1115_ADDR            0x48
#define AIOT_ADS1115_INPUT           0

/* Loop current -> pressure.  I [mA] = V [mV] / R [Ohm]
 * 150 Ohm keeps 4-20 mA inside 0.6-3.0 V (safe for both ADCs at 3.3 V). */
#define AIOT_SHUNT_OHMS              150.0f
#define AIOT_LOOP_MIN_MA             4.0f
#define AIOT_LOOP_MAX_MA             20.0f
#define AIOT_LOOP_FAULT_LOW_MA       3.6f     /* below: open loop / no sensor -> NaN */
#define AIOT_LOOP_FAULT_HIGH_MA      21.0f    /* above: over range (value kept, flagged) */

/* Transducer ranges [kPa] (e.g. 0-1.2 MPa G1/4 transducer) and zero offsets */
#define AIOT_P1_MIN_KPA              0.0f
#define AIOT_P1_MAX_KPA              1200.0f
#define AIOT_P1_OFFSET_KPA           0.0f
#define AIOT_P2_MIN_KPA              0.0f
#define AIOT_P2_MAX_KPA              1200.0f
#define AIOT_P2_OFFSET_KPA           0.0f

/* ==========================================================================
 * IMU MPU-6050 (GY-521 module)
 * ========================================================================== */
#define AIOT_IMU_ENABLED             1
#define AIOT_MPU6050_ADDR            0x68     /* AD0 to GND */
#define AIOT_IMU_ACCEL_FS            MPU6050_AIOT_ACCEL_8G
#define AIOT_IMU_GYRO_FS             MPU6050_AIOT_GYRO_500DPS
#define AIOT_IMU_DLPF                MPU6050_AIOT_DLPF_184HZ   /* < Nyquist of 500 Hz */
#define AIOT_IMU_GYRO_CAL_SAMPLES    200      /* 0 = no gyro calibration at boot */

/* ==========================================================================
 * Flow meter (pulse output)
 * ========================================================================== */
#define AIOT_FLOW_ENABLED            1
#define AIOT_FLOW_GPIO               14       /* GPIO 17/18 are the UART link */
#define AIOT_FLOW_GLITCH_NS          1000     /* ignore pulses shorter than 1 us */
#define AIOT_FLOW_K_PULSES_PER_L     450.0f   /* e.g. YF-S201: f = 7.5 * Q[L/min] -> 450 /L */
#define AIOT_FLOW_UPDATE_MS          500      /* flow averaging window */

/* ==========================================================================
 * UART link test with the CrowPanel (Enlace_AIoT, crossed wiring + GND)
 * ========================================================================== */
#define AIOT_LINK_ENABLED            1
#define AIOT_LINK_UART_NUM           1        /* UART0 is the USB console */
#define AIOT_LINK_TX_PIN             17       /* -> CrowPanel IO18 (RX) */
#define AIOT_LINK_RX_PIN             18       /* <- CrowPanel IO17 (TX) */
#define AIOT_LINK_BAUD               115200
#define AIOT_LINK_PERIOD_MS          2000
#define AIOT_LINK_GREETING           "Hola soy un ESP32-S3"

/* ==========================================================================
 * Compile-time checks
 * ========================================================================== */
_Static_assert(AIOT_SAMPLE_RATE_HZ >= 10 && AIOT_SAMPLE_RATE_HZ <= 1000,
               "AIOT_SAMPLE_RATE_HZ must be 10..1000 (MPU-6050 internal rate is 1 kHz)");
_Static_assert(AIOT_PRESSURE_CHANNELS >= 1 && AIOT_PRESSURE_CHANNELS <= 2,
               "AIOT_PRESSURE_CHANNELS must be 1 or 2");
_Static_assert(AIOT_PRESSURE_SOURCE != AIOT_PRESSURE_SOURCE_ADS1115 || AIOT_PRESSURE_CHANNELS == 1,
               "ADS1115 continuous mode reads one transducer: set AIOT_PRESSURE_CHANNELS to 1");
_Static_assert(AIOT_PRESSURE_SOURCE != AIOT_PRESSURE_SOURCE_ADS1115 || AIOT_SAMPLE_RATE_HZ <= 860,
               "ADS1115 maximum rate is 860 samples per second");
_Static_assert(AIOT_FLOW_GPIO != AIOT_LINK_TX_PIN && AIOT_FLOW_GPIO != AIOT_LINK_RX_PIN,
               "The flow meter pin cannot be one of the UART link pins");
_Static_assert(AIOT_ADC_TOTAL_RATE_HZ / AIOT_PRESSURE_CHANNELS >= AIOT_SAMPLE_RATE_HZ,
               "ADC rate per channel must be at least AIOT_SAMPLE_RATE_HZ");

#endif /* SENSOR_DEFINES_AIOT_H */
