/*
 * File: components/SPI_AIoT/include/SPI_AIoT.h
 * Description: SPI master library for ESP-IDF.
 *              Same philosophy as UARTn_AIoT: the logic lives in the C++
 *              class SPI_AIoT_Device (src/SPI_AIoT.cpp); this header exposes
 *              a C API with an opaque handle.
 *              Life cycle: create -> init -> use -> destroy.
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, C11 / C++. English comments.
 *
 * Two levels, because one SPI bus carries several chips (one CS each):
 *   SPI_AIoT_bus_init()  -> the bus (host + MOSI + MISO + SCLK), once.
 *   SPI_AIoT_handle_t    -> one device on that bus (CS pin + clock + mode).
 *
 * Example (external ADC on SPI2, CS = GPIO 10, 1 MHz, mode 0):
 *   SPI_AIoT_bus_init(SPI_AIOT_HOST_SPI2, 11, 13, 12, 0);
 *   SPI_AIoT_handle_t adc = SPI_AIoT_create(SPI_AIOT_HOST_SPI2, 10, 1000000, 0);
 *   SPI_AIoT_init(adc);
 *   uint8_t cmd[3] = { 0x01, 0x80, 0x00 }, ans[3];
 *   SPI_AIoT_transfer(adc, cmd, ans, 3);
 *
 * CrowPanel 4.3" note:
 *   The XPT2046 touch already uses SPI2 (MOSI 11, MISO 13, SCLK 12, CS 0).
 *   SPI_AIoT_bus_init() on an already initialized host returns ESP_OK, so new
 *   devices can share that bus: just give each one its own free CS pin.
 *
 * Thread safety: the ESP-IDF driver serializes the devices of one bus; this
 * library also serializes the transactions of the same device.
 */
#ifndef SPI_AIOT_H
#define SPI_AIOT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle: points to a SPI_AIoT_Device C++ object */
typedef struct SPI_AIoT_Instance *SPI_AIoT_handle_t;

/* SPI hosts usable by the application on the ESP32-S3 (SPI1 = flash/PSRAM) */
#define SPI_AIOT_HOST_SPI2   1   /* == SPI2_HOST (FSPI) */
#define SPI_AIOT_HOST_SPI3   2   /* == SPI3_HOST        */

/* -------------------------------------------------------------------------- */
/* Bus                                                                         */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initializes an SPI bus with DMA.
 *        If the host is already initialized (e.g. by the touch driver) it
 *        returns ESP_OK and the existing bus is shared; in that case the pins
 *        given here are ignored.
 * @param host               SPI_AIOT_HOST_SPI2 or SPI_AIOT_HOST_SPI3
 * @param mosi_pin           MOSI GPIO (-1 if not used)
 * @param miso_pin           MISO GPIO (-1 if not used)
 * @param sclk_pin           SCLK GPIO
 * @param max_transfer_bytes Largest transaction in bytes (0 = driver default, 4092)
 */
esp_err_t SPI_AIoT_bus_init(int host, int mosi_pin, int miso_pin, int sclk_pin, int max_transfer_bytes);

/**
 * @brief Frees an SPI bus. All its devices must be destroyed first.
 */
esp_err_t SPI_AIoT_bus_free(int host);

/* -------------------------------------------------------------------------- */
/* Device                                                                      */
/* -------------------------------------------------------------------------- */

/**
 * @brief Creates a device object (no hardware is touched yet).
 * @param host      Bus host (SPI_AIOT_HOST_SPI2 / SPI_AIOT_HOST_SPI3)
 * @param cs_pin    Chip-select GPIO (-1 = CS handled by the application)
 * @param clock_hz  SCLK frequency (respect the chip datasheet)
 * @param mode      SPI mode 0..3 (CPOL/CPHA)
 */
SPI_AIoT_handle_t SPI_AIoT_create(int host, int cs_pin, uint32_t clock_hz, uint8_t mode);

/**
 * @brief Attaches the device to its bus. The bus must be initialized first.
 */
esp_err_t SPI_AIoT_init(SPI_AIoT_handle_t handle);

/**
 * @brief Detaches the device from the bus and frees the object.
 */
void SPI_AIoT_destroy(SPI_AIoT_handle_t handle);

/* -------------------------------------------------------------------------- */
/* Transfers (blocking, polling mode: lowest latency for short frames)         */
/* -------------------------------------------------------------------------- */

/**
 * @brief Full-duplex transfer: sends len bytes of tx while receiving len bytes.
 * @param tx  Data to send (NULL = send zeros)
 * @param rx  Received data (NULL = discard)
 */
esp_err_t SPI_AIoT_transfer(SPI_AIoT_handle_t handle, const uint8_t *tx, uint8_t *rx, size_t len);

/** @brief Sends len bytes, ignores what comes back */
esp_err_t SPI_AIoT_write(SPI_AIoT_handle_t handle, const uint8_t *tx, size_t len);

/** @brief Receives len bytes while sending the fill byte (usually 0x00 or 0xFF) */
esp_err_t SPI_AIoT_read(SPI_AIoT_handle_t handle, uint8_t *rx, size_t len, uint8_t fill);

/**
 * @brief Sends tx_len bytes and then reads rx_len bytes in the SAME CS frame
 *        (typical "command/register + data" access).
 */
esp_err_t SPI_AIoT_write_then_read(SPI_AIoT_handle_t handle,
                                   const uint8_t *tx, size_t tx_len,
                                   uint8_t *rx, size_t rx_len);

/* -------------------------------------------------------------------------- */
/* Exclusive access                                                            */
/* -------------------------------------------------------------------------- */

/**
 * @brief Reserves the bus for this device (several transfers in a row without
 *        other devices interleaving). Always pair it with SPI_AIoT_unlock().
 * @param timeout_ms 0 = wait forever
 */
esp_err_t SPI_AIoT_lock(SPI_AIoT_handle_t handle, uint32_t timeout_ms);

/** @brief Releases the bus reserved with SPI_AIoT_lock() */
void SPI_AIoT_unlock(SPI_AIoT_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* SPI_AIOT_H */
