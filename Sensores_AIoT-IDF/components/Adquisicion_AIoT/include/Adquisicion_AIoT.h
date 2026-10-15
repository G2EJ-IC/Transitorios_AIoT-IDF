/*
 * File: components/Adquisicion_AIoT/include/Adquisicion_AIoT.h
 * Description: Acquisition engine of the SENSOR NODE.
 *              Samples the pressure transducers and the IMU at a fixed rate,
 *              reads the flow meter, packs everything into AIoT_frame_t
 *              frames (Frame_AIoT.h) and sends them to the CrowPanel over
 *              ESP-NOW.
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, C11. English comments.
 *
 * Tasks:
 *   core 1  "aiot_acq"  high priority, woken by an esp_timer every sample
 *   core 0  "aiot_tx"   sends the finished frames (WiFi / ESP-NOW side)
 *
 * Every pin and rate is defined in Sensor_Defines_AIoT.h.
 * NVS must be initialized before Adquisicion_AIoT_Init() (WiFi needs it).
 */
#ifndef ADQUISICION_AIOT_H
#define ADQUISICION_AIOT_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Counters and latest values, for the console / diagnostics */
typedef struct {
    uint32_t samples;            /* samples taken */
    uint32_t frames_built;       /* frames packed */
    uint32_t frames_sent;        /* frames handed to the radio successfully */
    uint32_t frames_failed;      /* send errors (unicast: no ACK) */
    uint32_t frames_dropped;     /* TX queue full: frame discarded */
    uint32_t missed_ticks;       /* sampling ticks lost (late task) */
    uint32_t imu_errors;         /* IMU read errors */
    uint32_t adc_overruns;       /* internal ADC DMA overruns */
    bool     pressure_ok;        /* pressure source initialized */
    bool     imu_ok;             /* IMU initialized */
    bool     flow_ok;            /* flow meter initialized */
    uint16_t last_status;        /* status bits of the last frame */
    float    p_kpa[2];           /* latest pressures [kPa] */
    float    acc[3];             /* latest acceleration [m/s2] */
    float    gyr[3];             /* latest angular rate [rad/s] */
    float    temp_c;             /* IMU temperature [degC] */
    float    flow_lps;           /* latest flow [L/s] */
    float    volume_l;           /* accumulated volume [L] */
} Adquisicion_AIoT_stats_t;

/**
 * @brief Starts the sensors, ESP-NOW and the acquisition / transmission tasks.
 *        A missing sensor is not fatal: its values are sent as NaN and its
 *        status bit is set in every frame.
 * @return ESP_OK, or an error if ESP-NOW or the tasks could not start.
 */
esp_err_t Adquisicion_AIoT_Init(void);

/**
 * @brief Copies the counters and latest values (thread safe).
 */
void Adquisicion_AIoT_get_stats(Adquisicion_AIoT_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* ADQUISICION_AIOT_H */
