/*
 * File: components/ESPNOW_AIoT/include/ESPNOW_AIoT.h
 * Description: ESP-NOW library for ESP-IDF (ESP32 to ESP32, no router).
 *              Same philosophy as UARTn_AIoT: the logic lives in the C++
 *              class ESPNOW_AIoT (src/ESPNOW_AIoT.cpp); this header exposes a
 *              C API with an opaque handle.
 *              Life cycle: create -> init -> use -> destroy.
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, C11 / C++. English comments.
 *
 * Typical use (sensor node -> CrowPanel):
 *   ESPNOW_AIoT_handle_t link = ESPNOW_AIoT_create(1, 16);  // channel, RX queue
 *   ESPNOW_AIoT_init(link);
 *   ESPNOW_AIoT_add_peer(link, panel_mac);
 *   ESPNOW_AIoT_send(link, panel_mac, &frame, sizeof(frame));
 *
 *   // on the CrowPanel (any task):
 *   ESPNOW_AIoT_packet_t pkt;
 *   if (ESPNOW_AIoT_receive(link, &pkt, 100) == ESP_OK) { ... }
 *
 * Notes:
 *  - Only ONE ESP-NOW object can exist (the radio has one ESP-NOW stack).
 *  - If WiFi is already started (e.g. wifi_init_sta() on the CrowPanel),
 *    ESP-NOW uses the channel of the access point; the sensor nodes must use
 *    that same channel (see ESPNOW_AIoT_get_channel()).
 *  - If WiFi is not started (sensor node), init() starts it in station mode
 *    on the requested channel. nvs_flash_init() must have been called before.
 *  - Received packets are copied to a FreeRTOS queue: the WiFi task is never
 *    blocked, and any task can read them.
 */
#ifndef ESPNOW_AIOT_H
#define ESPNOW_AIOT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle: points to the ESPNOW_AIoT C++ object */
typedef struct ESPNOW_AIoT_Instance *ESPNOW_AIoT_handle_t;

#define ESPNOW_AIOT_MAC_LEN          6
#define ESPNOW_AIOT_MAX_PAYLOAD      250    /* ESP-NOW v1.0 limit, compatible with every ESP32 */
#define ESPNOW_AIOT_SEND_TIMEOUT_MS  100    /* wait for the delivery report */

/* Received packet (copied from the WiFi task into the RX queue) */
typedef struct {
    uint8_t  mac[ESPNOW_AIOT_MAC_LEN];      /* sender MAC address */
    int8_t   rssi;                          /* signal strength, dBm */
    uint16_t len;                           /* payload length in bytes */
    uint8_t  data[ESPNOW_AIOT_MAX_PAYLOAD]; /* payload */
} ESPNOW_AIoT_packet_t;

/* -------------------------------------------------------------------------- */
/* Constructor / destructor                                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Creates the ESP-NOW object (no radio activity yet).
 * @param channel      WiFi channel 1..13, used only if WiFi is not started yet
 * @param rx_queue_len Packets that can wait in the RX queue (e.g. 16)
 * @return Handle, or NULL on invalid arguments, no memory or if an ESP-NOW
 *         object already exists.
 */
ESPNOW_AIoT_handle_t ESPNOW_AIoT_create(uint8_t channel, uint16_t rx_queue_len);

/**
 * @brief Starts ESP-NOW (and WiFi in station mode if it is not running).
 */
esp_err_t ESPNOW_AIoT_init(ESPNOW_AIoT_handle_t handle);

/**
 * @brief Stops ESP-NOW and frees the object. WiFi is stopped only if this
 *        object started it.
 */
void ESPNOW_AIoT_destroy(ESPNOW_AIoT_handle_t handle);

/* -------------------------------------------------------------------------- */
/* Peers                                                                       */
/* -------------------------------------------------------------------------- */

/** @brief Registers a destination MAC (needed before sending to it) */
esp_err_t ESPNOW_AIoT_add_peer(ESPNOW_AIoT_handle_t handle, const uint8_t mac[ESPNOW_AIOT_MAC_LEN]);

/** @brief Removes a registered destination */
esp_err_t ESPNOW_AIoT_remove_peer(ESPNOW_AIoT_handle_t handle, const uint8_t mac[ESPNOW_AIOT_MAC_LEN]);

/* -------------------------------------------------------------------------- */
/* Data                                                                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Sends a packet and waits for the delivery report.
 * @param mac  Destination MAC (must be a registered peer), or NULL = broadcast
 * @param data Payload
 * @param len  1..ESPNOW_AIOT_MAX_PAYLOAD bytes
 * @return ESP_OK when the receiver acknowledged it (broadcast: when sent),
 *         ESP_FAIL if not acknowledged, ESP_ERR_TIMEOUT, or driver errors.
 */
esp_err_t ESPNOW_AIoT_send(ESPNOW_AIoT_handle_t handle, const uint8_t *mac, const void *data, size_t len);

/**
 * @brief Takes the oldest received packet from the RX queue.
 * @param timeout_ms 0 = do not wait
 * @return ESP_OK, ESP_ERR_TIMEOUT (queue empty) or ESP_ERR_INVALID_ARG.
 */
esp_err_t ESPNOW_AIoT_receive(ESPNOW_AIoT_handle_t handle, ESPNOW_AIoT_packet_t *packet, uint32_t timeout_ms);

/* -------------------------------------------------------------------------- */
/* Information                                                                 */
/* -------------------------------------------------------------------------- */

/** @brief MAC address of this board (station interface): give it to the peers */
esp_err_t ESPNOW_AIoT_get_own_mac(uint8_t mac[ESPNOW_AIOT_MAC_LEN]);

/** @brief Current WiFi channel (the nodes must use the same one) */
uint8_t ESPNOW_AIoT_get_channel(ESPNOW_AIoT_handle_t handle);

/** @brief Packets lost because the RX queue was full */
uint32_t ESPNOW_AIoT_get_dropped(ESPNOW_AIoT_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* ESPNOW_AIOT_H */
