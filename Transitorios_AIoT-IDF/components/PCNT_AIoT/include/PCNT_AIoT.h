/*
 * File: components/PCNT_AIoT/include/PCNT_AIoT.h
 * Description: Hardware pulse counter library for ESP-IDF (PCNT), intended for
 *              pulse-output flow meters (turbine / hall effect).
 *              Same philosophy as UARTn_AIoT: the logic lives in the C++
 *              class PCNT_AIoT (src/PCNT_AIoT.cpp); this header exposes a C API
 *              with an opaque handle.
 *              Life cycle: create -> init -> read -> destroy.
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, C11 / C++. English comments.
 *
 * Example (flow meter on GPIO 18, K = 450 pulses per liter):
 *   PCNT_AIoT_handle_t fm = PCNT_AIoT_create(18, 1000);   // 1 us glitch filter
 *   PCNT_AIoT_set_k_factor(fm, 450.0f);
 *   PCNT_AIoT_init(fm);
 *   ...
 *   float q_lps, vol_l;
 *   PCNT_AIoT_get_flow(fm, &q_lps);        // L/s (Q in m3/s = q_lps / 1000)
 *   PCNT_AIoT_get_volume(fm, &vol_l);      // liters since init/clear
 *
 * Notes:
 *  - Counting is done by hardware on rising edges: no CPU load, no lost pulses.
 *  - The ESP32-S3 has 4 PCNT units: up to 4 objects at the same time.
 *  - The input has the internal pull-up enabled (open-collector sensors).
 *  - Flow is computed from the pulses between two consecutive calls of
 *    PCNT_AIoT_get_frequency()/PCNT_AIoT_get_flow(): call them periodically
 *    (e.g. every 100..1000 ms) for a stable reading.
 *  - K factor: pulses per liter from the sensor datasheet (some datasheets
 *    give f = k * Q[L/min]; then K[pulses/L] = 60 * k).
 */
#ifndef PCNT_AIOT_H
#define PCNT_AIOT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle: points to the PCNT_AIoT C++ object */
typedef struct PCNT_AIoT_Instance *PCNT_AIoT_handle_t;

#define PCNT_AIOT_GLITCH_MAX_NS   12000   /* hardware filter limit (~1023 APB cycles) */

/* -------------------------------------------------------------------------- */
/* Constructor / destructor                                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Creates a pulse counter object (no hardware touched yet).
 * @param gpio      Input pin of the pulses
 * @param glitch_ns Pulses shorter than this are ignored (0 = no filter,
 *                  max PCNT_AIOT_GLITCH_MAX_NS)
 */
PCNT_AIoT_handle_t PCNT_AIoT_create(int gpio, uint32_t glitch_ns);

/**
 * @brief Creates the PCNT unit and starts counting from zero.
 */
esp_err_t PCNT_AIoT_init(PCNT_AIoT_handle_t handle);

/**
 * @brief Stops the counter, releases the unit and frees the object.
 */
void PCNT_AIoT_destroy(PCNT_AIoT_handle_t handle);

/* -------------------------------------------------------------------------- */
/* Counting                                                                    */
/* -------------------------------------------------------------------------- */

/** @brief Total pulses since init or the last clear */
esp_err_t PCNT_AIoT_get_count(PCNT_AIoT_handle_t handle, int64_t *count);

/** @brief Sets the total to zero (the frequency reference restarts too) */
esp_err_t PCNT_AIoT_clear(PCNT_AIoT_handle_t handle);

/** @brief Pulse frequency (Hz) measured since the previous call */
esp_err_t PCNT_AIoT_get_frequency(PCNT_AIoT_handle_t handle, float *hz);

/* -------------------------------------------------------------------------- */
/* Flow meter helpers                                                          */
/* -------------------------------------------------------------------------- */

/** @brief Sets the K factor of the meter in pulses per liter (> 0) */
esp_err_t PCNT_AIoT_set_k_factor(PCNT_AIoT_handle_t handle, float pulses_per_liter);

/** @brief Flow in liters per second since the previous call (needs K) */
esp_err_t PCNT_AIoT_get_flow(PCNT_AIoT_handle_t handle, float *liters_per_s);

/** @brief Accumulated volume in liters since init or the last clear (needs K) */
esp_err_t PCNT_AIoT_get_volume(PCNT_AIoT_handle_t handle, float *liters);

#ifdef __cplusplus
}
#endif

#endif /* PCNT_AIOT_H */
