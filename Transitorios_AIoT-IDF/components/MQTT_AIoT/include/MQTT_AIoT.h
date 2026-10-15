/*
 * File: components/MQTT_AIoT/include/MQTT_AIoT.h
 * Description: MQTT client library for ESP-IDF (esp-mqtt), to send the
 *              measurements to a broker (PC, server or cloud).
 *              Same philosophy as UARTn_AIoT: the logic lives in the C++
 *              class MQTT_AIoT (src/MQTT_AIoT.cpp); this header exposes a C API
 *              with an opaque handle.
 *              Life cycle: create -> (credentials, callback) -> init -> use ->
 *                          destroy.
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, C11 / C++. English comments.
 *
 * Example:
 *   MQTT_AIoT_handle_t mq = MQTT_AIoT_create("mqtt://192.168.2.10:1883", "crowpanel-01");
 *   MQTT_AIoT_set_callback(mq, on_message, NULL);
 *   MQTT_AIoT_subscribe(mq, "aiot/cmd", 1);             // kept for reconnections
 *   MQTT_AIoT_init(mq);                                 // needs WiFi connected
 *   MQTT_AIoT_publish_str(mq, "aiot/pressure", "{\"p_kPa\":101.3}", 0, false);
 *
 * Notes:
 *  - WiFi must be connected (wifi_init_sta / wifi_connect) for the client to
 *    reach the broker; it reconnects automatically when the link returns.
 *  - The message callback runs in the MQTT task (core 0 on this project).
 *    Keep it short; to touch LVGL use Configuracion_AIoT_Lock()/Unlock().
 *  - Subscriptions are remembered and repeated after every reconnection.
 *  - Received messages longer than MQTT_AIOT_MAX_MESSAGE are truncated.
 */
#ifndef MQTT_AIOT_H
#define MQTT_AIOT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle: points to the MQTT_AIoT C++ object */
typedef struct MQTT_AIoT_Instance *MQTT_AIoT_handle_t;

#define MQTT_AIOT_MAX_TOPICS     8       /* remembered subscriptions */
#define MQTT_AIOT_TOPIC_LEN      96      /* max topic length incl. '\0' */
#define MQTT_AIOT_MAX_MESSAGE    2048    /* max received payload */

/**
 * @brief Received message callback.
 * @param topic    Null-terminated topic
 * @param data     Payload (not null-terminated)
 * @param len      Payload length
 * @param user_ctx Pointer given in MQTT_AIoT_set_callback()
 */
typedef void (*MQTT_AIoT_message_cb_t)(const char *topic, const uint8_t *data, size_t len, void *user_ctx);

/* -------------------------------------------------------------------------- */
/* Constructor / configuration / destructor                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Creates the MQTT object (no network activity yet).
 * @param broker_uri e.g. "mqtt://192.168.2.10:1883" or "mqtts://host:8883"
 * @param client_id  Unique name of this board on the broker (NULL = automatic)
 */
MQTT_AIoT_handle_t MQTT_AIoT_create(const char *broker_uri, const char *client_id);

/** @brief User and password for the broker (call BEFORE init; NULL = none) */
esp_err_t MQTT_AIoT_set_credentials(MQTT_AIoT_handle_t handle, const char *username, const char *password);

/** @brief Function called for every received message (may be NULL) */
esp_err_t MQTT_AIoT_set_callback(MQTT_AIoT_handle_t handle, MQTT_AIoT_message_cb_t cb, void *user_ctx);

/**
 * @brief Starts the client: it connects in the background and keeps
 *        reconnecting automatically.
 */
esp_err_t MQTT_AIoT_init(MQTT_AIoT_handle_t handle);

/** @brief Disconnects, stops the client and frees the object */
void MQTT_AIoT_destroy(MQTT_AIoT_handle_t handle);

/* -------------------------------------------------------------------------- */
/* Use                                                                         */
/* -------------------------------------------------------------------------- */

/** @brief true while connected to the broker */
bool MQTT_AIoT_is_connected(MQTT_AIoT_handle_t handle);

/**
 * @brief Publishes a binary payload.
 * @param qos    0, 1 or 2
 * @param retain true = the broker keeps the last value for new subscribers
 * @return Message id (>= 0), or -1 if not connected / error.
 */
int MQTT_AIoT_publish(MQTT_AIoT_handle_t handle, const char *topic,
                      const void *data, size_t len, int qos, bool retain);

/** @brief Publishes a null-terminated string (e.g. JSON or CSV) */
int MQTT_AIoT_publish_str(MQTT_AIoT_handle_t handle, const char *topic,
                          const char *text, int qos, bool retain);

/**
 * @brief Subscribes to a topic (wildcards + and # allowed). Works before init
 *        too: the subscription is applied as soon as the client connects.
 */
esp_err_t MQTT_AIoT_subscribe(MQTT_AIoT_handle_t handle, const char *topic, int qos);

/** @brief Removes a subscription */
esp_err_t MQTT_AIoT_unsubscribe(MQTT_AIoT_handle_t handle, const char *topic);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_AIOT_H */
