/*
 * File: components/SD_AIoT/include/SD_AIoT.h
 * Description: microSD library for ESP-IDF (SPI mode + FAT file system), for
 *              logging hydraulic transient events.
 *              Same philosophy as UARTn_AIoT: the logic lives in the C++
 *              class SD_AIoT (src/SD_AIoT.cpp); this header exposes a C API
 *              with an opaque handle.
 *              Life cycle: create -> init (mount) -> use -> destroy (unmount).
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, C11 / C++. English comments.
 *
 * Example (CrowPanel 4.3": SD shares SPI2 with the touch; check the CS pin of
 * your board in the Elecrow schematic):
 *   SD_AIoT_handle_t sd = SD_AIoT_create(1, 11, 13, 12, SD_CS_PIN, "/sdcard");
 *   if (SD_AIoT_init(sd) == ESP_OK) {
 *       SD_AIoT_append_line(sd, "events.csv", "t_ms;p_kPa;q_Lps");
 *   }
 *   // Standard C files also work: fopen("/sdcard/events.csv", "a") ...
 *
 * Notes:
 *  - host: 1 = SPI2, 2 = SPI3. If that SPI bus is already initialized (touch),
 *    it is shared and the pins given here are ignored.
 *  - Call SD_AIoT_init() AFTER Configuracion_AIoT_Init() when sharing SPI2.
 *  - File names: relative names are placed under the mount point.
 *    Long names need CONFIG_FATFS_LFN_HEAP=y (otherwise 8.3 names only).
 *  - The card is NOT formatted automatically (data safety).
 */
#ifndef SD_AIOT_H
#define SD_AIOT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle: points to the SD_AIoT C++ object */
typedef struct SD_AIoT_Instance *SD_AIoT_handle_t;

#define SD_AIOT_DEFAULT_FREQ_KHZ   20000   /* SPI clock of the card */
#define SD_AIOT_MAX_OPEN_FILES     5
#define SD_AIOT_PATH_MAX           128

/* -------------------------------------------------------------------------- */
/* Constructor / destructor                                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Creates the microSD object (no hardware touched yet).
 * @param host        SPI host: 1 = SPI2, 2 = SPI3
 * @param mosi_pin    MOSI GPIO
 * @param miso_pin    MISO GPIO
 * @param sclk_pin    SCLK GPIO
 * @param cs_pin      Chip-select GPIO of the card
 * @param mount_point Path where the card appears, e.g. "/sdcard"
 */
SD_AIoT_handle_t SD_AIoT_create(int host, int mosi_pin, int miso_pin, int sclk_pin,
                                int cs_pin, const char *mount_point);

/**
 * @brief Sets the SPI clock of the card in kHz (call BEFORE init).
 *        Lower it (e.g. 10000) if the wires are long or the card fails.
 */
esp_err_t SD_AIoT_set_max_freq_khz(SD_AIoT_handle_t handle, int freq_khz);

/**
 * @brief Initializes (or shares) the SPI bus and mounts the FAT file system.
 */
esp_err_t SD_AIoT_init(SD_AIoT_handle_t handle);

/**
 * @brief Unmounts the card and frees the object (the SPI bus is left as is,
 *        because other devices may use it).
 */
void SD_AIoT_destroy(SD_AIoT_handle_t handle);

/* -------------------------------------------------------------------------- */
/* State                                                                       */
/* -------------------------------------------------------------------------- */

/** @brief true when the card is mounted */
bool SD_AIoT_is_mounted(SD_AIoT_handle_t handle);

/** @brief Mount point given in SD_AIoT_create(), e.g. "/sdcard" */
const char *SD_AIoT_get_mount_point(SD_AIoT_handle_t handle);

/** @brief Card capacity and free space in bytes */
esp_err_t SD_AIoT_get_info(SD_AIoT_handle_t handle, uint64_t *total_bytes, uint64_t *free_bytes);

/* -------------------------------------------------------------------------- */
/* Files (thread safe)                                                         */
/* -------------------------------------------------------------------------- */

/**
 * @brief Writes a buffer to a file.
 * @param append true = add at the end, false = replace the file
 */
esp_err_t SD_AIoT_write_file(SD_AIoT_handle_t handle, const char *name,
                             const void *data, size_t len, bool append);

/** @brief Appends one text line and the '\n' (handy for CSV logs) */
esp_err_t SD_AIoT_append_line(SD_AIoT_handle_t handle, const char *name, const char *line);

/**
 * @brief Reads up to max_len bytes from the start of a file.
 * @return Bytes read, or -1 if the file cannot be opened.
 */
int SD_AIoT_read_file(SD_AIoT_handle_t handle, const char *name, void *buffer, size_t max_len);

/** @brief true if the file exists */
bool SD_AIoT_exists(SD_AIoT_handle_t handle, const char *name);

/** @brief Deletes a file */
esp_err_t SD_AIoT_remove(SD_AIoT_handle_t handle, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* SD_AIOT_H */
