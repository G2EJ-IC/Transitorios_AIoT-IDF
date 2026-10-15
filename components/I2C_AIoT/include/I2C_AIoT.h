/*
 * File: components/I2C_AIoT/include/I2C_AIoT.h
 * Description: I2C master library for ESP-IDF (new driver/i2c_master.h API).
 *              Same philosophy as UARTn_AIoT: the logic lives in the C++
 *              classes I2C_AIoT_Bus and I2C_AIoT_Device (src/I2C_AIoT.cpp);
 *              this header exposes a C API with opaque handles.
 *              Life cycle: create -> init -> use -> destroy.
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, C11 / C++. English comments.
 *
 * Two objects, because one I2C bus carries several sensors:
 *   I2C_AIoT_bus_t     -> the bus (port + SDA + SCL), created once.
 *   I2C_AIoT_handle_t  -> one device on that bus (7-bit address + speed).
 *
 * Example (IMU at 0x68 on I2C0):
 *   I2C_AIoT_bus_t bus = I2C_AIoT_bus_create(0, SDA_PIN, SCL_PIN, true);
 *   I2C_AIoT_bus_init(bus);
 *   I2C_AIoT_scan(bus, NULL, 0);                      // logs found addresses
 *   I2C_AIoT_handle_t imu = I2C_AIoT_create(bus, 0x68, 400000);
 *   I2C_AIoT_init(imu);
 *   I2C_AIoT_write_reg(imu, 0x6B, 0x00);              // wake up
 *   uint8_t raw[14];
 *   I2C_AIoT_read_regs(imu, 0x3B, raw, sizeof(raw));  // burst read
 *
 * Thread safety: the ESP-IDF I2C master driver serializes transactions per
 * bus, so different tasks may use different devices of the same bus.
 */
#ifndef I2C_AIOT_H
#define I2C_AIOT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handles: they point to I2C_AIoT_Bus / I2C_AIoT_Device C++ objects */
typedef struct I2C_AIoT_BusInstance *I2C_AIoT_bus_t;
typedef struct I2C_AIoT_Instance    *I2C_AIoT_handle_t;

/* Default values */
#define I2C_AIOT_DEFAULT_TIMEOUT_MS   100      /* per transaction */
#define I2C_AIOT_SPEED_STANDARD       100000   /* 100 kHz */
#define I2C_AIOT_SPEED_FAST           400000   /* 400 kHz */

/* -------------------------------------------------------------------------- */
/* Bus                                                                         */
/* -------------------------------------------------------------------------- */

/**
 * @brief Creates a bus object (no hardware is touched yet).
 * @param port             I2C port (0 or 1 on the ESP32-S3)
 * @param sda_pin          SDA GPIO
 * @param scl_pin          SCL GPIO
 * @param internal_pullup  true = enable the internal ~45 kOhm pull-ups
 *                         (enough for short wires at 100 kHz; use external
 *                         2,2-4,7 kOhm resistors for 400 kHz or long cables)
 * @return Handle, or NULL on invalid arguments / no memory.
 */
I2C_AIoT_bus_t I2C_AIoT_bus_create(int port, int sda_pin, int scl_pin, bool internal_pullup);

/**
 * @brief Installs the I2C master bus.
 */
esp_err_t I2C_AIoT_bus_init(I2C_AIoT_bus_t bus);

/**
 * @brief Checks whether a device answers (ACK) at a 7-bit address.
 * @return ESP_OK if present, ESP_ERR_NOT_FOUND if not, other codes on error.
 */
esp_err_t I2C_AIoT_probe(I2C_AIoT_bus_t bus, uint8_t addr7);

/**
 * @brief Scans addresses 0x08..0x77 and logs every device found.
 * @param found      Optional array to store the addresses (may be NULL)
 * @param max_found  Size of the array
 * @return Number of devices found, or -1 on error.
 */
int I2C_AIoT_scan(I2C_AIoT_bus_t bus, uint8_t *found, int max_found);

/**
 * @brief Removes the bus (all its devices must be destroyed first) and frees it.
 */
void I2C_AIoT_bus_destroy(I2C_AIoT_bus_t bus);

/* -------------------------------------------------------------------------- */
/* Device                                                                      */
/* -------------------------------------------------------------------------- */

/**
 * @brief Creates a device object on a bus (no hardware is touched yet).
 * @param bus       Bus created with I2C_AIoT_bus_create()
 * @param addr7     7-bit address (e.g. 0x68), without the R/W bit
 * @param speed_hz  SCL frequency for this device (e.g. 100000, 400000)
 */
I2C_AIoT_handle_t I2C_AIoT_create(I2C_AIoT_bus_t bus, uint8_t addr7, uint32_t speed_hz);

/**
 * @brief Attaches the device to the bus. The bus must be initialized first.
 */
esp_err_t I2C_AIoT_init(I2C_AIoT_handle_t handle);

/**
 * @brief Detaches the device from the bus and frees the object.
 */
void I2C_AIoT_destroy(I2C_AIoT_handle_t handle);

/**
 * @brief Sets the timeout of every transaction of this device (milliseconds).
 */
void I2C_AIoT_set_timeout(I2C_AIoT_handle_t handle, uint32_t timeout_ms);

/* -------------------------------------------------------------------------- */
/* Transfers                                                                   */
/* -------------------------------------------------------------------------- */

/** @brief Writes len raw bytes: START, addr+W, data..., STOP */
esp_err_t I2C_AIoT_write(I2C_AIoT_handle_t handle, const uint8_t *data, size_t len);

/** @brief Reads len raw bytes: START, addr+R, data..., STOP */
esp_err_t I2C_AIoT_read(I2C_AIoT_handle_t handle, uint8_t *data, size_t len);

/** @brief Write then read with a repeated START (no STOP in between) */
esp_err_t I2C_AIoT_write_read(I2C_AIoT_handle_t handle,
                              const uint8_t *tx, size_t tx_len,
                              uint8_t *rx, size_t rx_len);

/** @brief Writes one 8-bit register: [reg, value] */
esp_err_t I2C_AIoT_write_reg(I2C_AIoT_handle_t handle, uint8_t reg, uint8_t value);

/** @brief Reads one 8-bit register */
esp_err_t I2C_AIoT_read_reg(I2C_AIoT_handle_t handle, uint8_t reg, uint8_t *value);

/** @brief Burst read of len bytes starting at reg (auto-increment devices) */
esp_err_t I2C_AIoT_read_regs(I2C_AIoT_handle_t handle, uint8_t reg, uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* I2C_AIOT_H */
