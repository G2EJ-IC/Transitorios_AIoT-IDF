/*
 * File: components/ADS1115_AIoT/include/ADS1115_AIoT.h
 * Description: ADS1115 16-bit external ADC driver (I2C, up to 860 samples/s)
 *              for ESP-IDF, built on I2C_AIoT. Intended for 4-20 mA pressure
 *              transducers read through a shunt resistor.
 *              Same philosophy as UARTn_AIoT: the logic lives in the C++
 *              class ADS1115_AIoT (src/ADS1115_AIoT.cpp); this header exposes
 *              a C API with an opaque handle.
 *              Life cycle: create -> (set_pga / set_data_rate) -> init ->
 *                          start_continuous -> read_mv -> destroy.
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, C11 / C++. English comments.
 *
 * Example (transducer on AIN0, continuous 860 SPS):
 *   ADS1115_AIoT_handle_t adc = ADS1115_AIoT_create(bus, 0x48, 400000);
 *   ADS1115_AIoT_set_pga(adc, ADS1115_AIOT_PGA_4096MV);
 *   ADS1115_AIoT_set_data_rate(adc, ADS1115_AIOT_DR_860SPS);
 *   ADS1115_AIoT_init(adc);
 *   ADS1115_AIoT_start_continuous(adc, ADS1115_AIOT_AIN0);
 *   float mv;
 *   ADS1115_AIoT_read_mv(adc, &mv);        // latest conversion
 *
 * Notes:
 *  - Address by the ADDR pin: GND 0x48, VDD 0x49, SDA 0x4A, SCL 0x4B.
 *  - Inputs are single-ended (AINx against GND). An input must never go
 *    above VDD + 0.3 V, whatever PGA is selected.
 *  - Continuous mode gives one input at the full data rate. Reading several
 *    inputs needs single-shot conversions (rate divided among them).
 *  - Thread safe: every call is protected by a mutex.
 */
#ifndef ADS1115_AIOT_H
#define ADS1115_AIOT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "I2C_AIoT.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle: points to the ADS1115_AIoT C++ object */
typedef struct ADS1115_AIoT_Instance *ADS1115_AIoT_handle_t;

#define ADS1115_AIOT_ADDR_GND   0x48
#define ADS1115_AIOT_ADDR_VDD   0x49
#define ADS1115_AIOT_ADDR_SDA   0x4A
#define ADS1115_AIOT_ADDR_SCL   0x4B

/* Single-ended inputs */
typedef enum {
    ADS1115_AIOT_AIN0 = 0,
    ADS1115_AIOT_AIN1 = 1,
    ADS1115_AIOT_AIN2 = 2,
    ADS1115_AIOT_AIN3 = 3,
} ADS1115_AIoT_input_t;

/* Programmable gain: full-scale range */
typedef enum {
    ADS1115_AIOT_PGA_6144MV = 0,     /* +/-6.144 V (input still limited to VDD) */
    ADS1115_AIOT_PGA_4096MV = 1,     /* +/-4.096 V -> 125 uV per LSB */
    ADS1115_AIOT_PGA_2048MV = 2,     /* +/-2.048 V (power-on default) */
    ADS1115_AIOT_PGA_1024MV = 3,
    ADS1115_AIOT_PGA_512MV  = 4,
    ADS1115_AIOT_PGA_256MV  = 5,
} ADS1115_AIoT_pga_t;

/* Data rate (samples per second) */
typedef enum {
    ADS1115_AIOT_DR_8SPS   = 0,
    ADS1115_AIOT_DR_16SPS  = 1,
    ADS1115_AIOT_DR_32SPS  = 2,
    ADS1115_AIOT_DR_64SPS  = 3,
    ADS1115_AIOT_DR_128SPS = 4,
    ADS1115_AIOT_DR_250SPS = 5,
    ADS1115_AIOT_DR_475SPS = 6,
    ADS1115_AIOT_DR_860SPS = 7,
} ADS1115_AIoT_dr_t;

/* -------------------------------------------------------------------------- */
/* Constructor / configuration / destructor                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Creates the ADC object (no hardware touched yet).
 * @param bus       I2C bus created and initialized with I2C_AIoT
 * @param addr7     0x48..0x4B
 * @param speed_hz  SCL frequency (100000 or 400000)
 */
ADS1115_AIoT_handle_t ADS1115_AIoT_create(I2C_AIoT_bus_t bus, uint8_t addr7, uint32_t speed_hz);

/** @brief Selects the full-scale range (default +/-4.096 V). Call before start. */
esp_err_t ADS1115_AIoT_set_pga(ADS1115_AIoT_handle_t handle, ADS1115_AIoT_pga_t pga);

/** @brief Selects the data rate (default 860 SPS). Call before start. */
esp_err_t ADS1115_AIoT_set_data_rate(ADS1115_AIoT_handle_t handle, ADS1115_AIoT_dr_t dr);

/**
 * @brief Detects the device and leaves it powered down (single-shot mode).
 * @return ESP_OK, ESP_ERR_NOT_FOUND (no ACK) or an I2C error.
 */
esp_err_t ADS1115_AIoT_init(ADS1115_AIoT_handle_t handle);

/**
 * @brief Powers the device down, detaches it from the bus and frees the object.
 */
void ADS1115_AIoT_destroy(ADS1115_AIoT_handle_t handle);

/* -------------------------------------------------------------------------- */
/* Conversions                                                                 */
/* -------------------------------------------------------------------------- */

/** @brief Starts continuous conversions of one input at the selected rate */
esp_err_t ADS1115_AIoT_start_continuous(ADS1115_AIoT_handle_t handle, ADS1115_AIoT_input_t input);

/** @brief Latest conversion of continuous mode, in millivolts */
esp_err_t ADS1115_AIoT_read_mv(ADS1115_AIoT_handle_t handle, float *mv);

/**
 * @brief One single-shot conversion of any input, in millivolts (waits for it).
 *        Stops continuous mode if it was running.
 */
esp_err_t ADS1115_AIoT_read_single_mv(ADS1115_AIoT_handle_t handle, ADS1115_AIoT_input_t input, float *mv);

#ifdef __cplusplus
}
#endif

#endif /* ADS1115_AIOT_H */
