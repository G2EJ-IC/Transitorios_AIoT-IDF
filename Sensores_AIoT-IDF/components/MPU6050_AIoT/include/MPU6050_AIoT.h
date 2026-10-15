/*
 * File: components/MPU6050_AIoT/include/MPU6050_AIoT.h
 * Description: MPU-6050 6-axis IMU driver (3-axis accelerometer + 3-axis
 *              gyroscope + die temperature) for ESP-IDF, built on I2C_AIoT.
 *              Same philosophy as UARTn_AIoT: the logic lives in the C++
 *              class MPU6050_AIoT (src/MPU6050_AIoT.cpp); this header exposes
 *              a C API with an opaque handle.
 *              Life cycle: create -> (set_ranges / set_dlpf) -> init -> read
 *                          -> destroy.
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, C11 / C++. English comments.
 *
 * Example (GY-521 module at 0x68 on an I2C bus already initialized):
 *   MPU6050_AIoT_handle_t imu = MPU6050_AIoT_create(bus, 0x68, 400000);
 *   MPU6050_AIoT_set_ranges(imu, MPU6050_AIOT_ACCEL_8G, MPU6050_AIOT_GYRO_500DPS);
 *   MPU6050_AIoT_set_dlpf(imu, MPU6050_AIOT_DLPF_184HZ);
 *   MPU6050_AIoT_init(imu);
 *   MPU6050_AIoT_calibrate_gyro(imu, 200);          // board at rest
 *   MPU6050_AIoT_data_t d;
 *   MPU6050_AIoT_read(imu, &d);                     // d.ax [m/s2], d.gx [rad/s]
 *
 * Notes:
 *  - Output units are SI: acceleration in m/s2, angular rate in rad/s,
 *    temperature in degrees Celsius.
 *  - Internal sample rate is 1 kHz (any DLPF setting), so the sensor can be
 *    read at up to 1000 Hz.
 *  - Register-compatible clones (MPU-6500 / MPU-9250 / unknown WHO_AM_I) are
 *    accepted with a warning.
 *  - Thread safe: every call is protected by a mutex.
 */
#ifndef MPU6050_AIOT_H
#define MPU6050_AIOT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "I2C_AIoT.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle: points to the MPU6050_AIoT C++ object */
typedef struct MPU6050_AIoT_Instance *MPU6050_AIoT_handle_t;

#define MPU6050_AIOT_ADDR_LOW    0x68    /* AD0 pin to GND (default) */
#define MPU6050_AIOT_ADDR_HIGH   0x69    /* AD0 pin to VCC */

/* Accelerometer full scale */
typedef enum {
    MPU6050_AIOT_ACCEL_2G  = 0,
    MPU6050_AIOT_ACCEL_4G  = 1,
    MPU6050_AIOT_ACCEL_8G  = 2,
    MPU6050_AIOT_ACCEL_16G = 3,
} MPU6050_AIoT_accel_fs_t;

/* Gyroscope full scale */
typedef enum {
    MPU6050_AIOT_GYRO_250DPS  = 0,
    MPU6050_AIOT_GYRO_500DPS  = 1,
    MPU6050_AIOT_GYRO_1000DPS = 2,
    MPU6050_AIOT_GYRO_2000DPS = 3,
} MPU6050_AIoT_gyro_fs_t;

/* Digital low-pass filter (accelerometer bandwidth) */
typedef enum {
    MPU6050_AIOT_DLPF_260HZ = 0,
    MPU6050_AIOT_DLPF_184HZ = 1,
    MPU6050_AIOT_DLPF_94HZ  = 2,
    MPU6050_AIOT_DLPF_44HZ  = 3,
    MPU6050_AIOT_DLPF_21HZ  = 4,
    MPU6050_AIOT_DLPF_10HZ  = 5,
    MPU6050_AIOT_DLPF_5HZ   = 6,
} MPU6050_AIoT_dlpf_t;

/* One reading in SI units */
typedef struct {
    float ax, ay, az;        /* acceleration [m/s2] */
    float gx, gy, gz;        /* angular rate [rad/s] (gyro bias removed) */
    float temp_c;            /* die temperature [degC] */
} MPU6050_AIoT_data_t;

/* -------------------------------------------------------------------------- */
/* Constructor / configuration / destructor                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Creates the IMU object (no hardware touched yet).
 * @param bus       I2C bus created and initialized with I2C_AIoT
 * @param addr7     0x68 or 0x69
 * @param speed_hz  SCL frequency (100000 or 400000)
 * @return Handle, or NULL on invalid arguments / no memory.
 */
MPU6050_AIoT_handle_t MPU6050_AIoT_create(I2C_AIoT_bus_t bus, uint8_t addr7, uint32_t speed_hz);

/**
 * @brief Selects the full scales (default 2 g / 250 dps). Before or after init.
 */
esp_err_t MPU6050_AIoT_set_ranges(MPU6050_AIoT_handle_t handle,
                                  MPU6050_AIoT_accel_fs_t accel_fs,
                                  MPU6050_AIoT_gyro_fs_t gyro_fs);

/**
 * @brief Selects the low-pass filter (default 184 Hz). Before or after init.
 */
esp_err_t MPU6050_AIoT_set_dlpf(MPU6050_AIoT_handle_t handle, MPU6050_AIoT_dlpf_t dlpf);

/**
 * @brief Detects, resets and configures the sensor.
 * @return ESP_OK, ESP_ERR_NOT_FOUND (no ACK at the address) or an I2C error.
 */
esp_err_t MPU6050_AIoT_init(MPU6050_AIoT_handle_t handle);

/**
 * @brief Puts the sensor to sleep, detaches it from the bus and frees the object.
 */
void MPU6050_AIoT_destroy(MPU6050_AIoT_handle_t handle);

/* -------------------------------------------------------------------------- */
/* Measurement                                                                 */
/* -------------------------------------------------------------------------- */

/**
 * @brief Reads acceleration, angular rate and temperature (one 14-byte burst).
 */
esp_err_t MPU6050_AIoT_read(MPU6050_AIoT_handle_t handle, MPU6050_AIoT_data_t *data);

/**
 * @brief Measures the gyroscope offset; the board must be at rest.
 * @param samples Number of readings to average (1 ms apart), e.g. 200
 */
esp_err_t MPU6050_AIoT_calibrate_gyro(MPU6050_AIoT_handle_t handle, uint16_t samples);

/** @brief WHO_AM_I value read in init (0x68 for a genuine MPU-6050, 0 before init) */
uint8_t MPU6050_AIoT_get_who_am_i(MPU6050_AIoT_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* MPU6050_AIOT_H */
