/*
 * File: components/Enlace_AIoT/include/Enlace_AIoT.h
 * Description: UART "hello" link test between the two boards of
 *              Transitorios AIoT, built on UARTn_AIoT.
 *              THIS COMPONENT IS IDENTICAL IN BOTH PROJECTS; each board only
 *              chooses its role:
 *                INITIATOR (sensor node ESP32-S3): sends its greeting every
 *                          period_ms and prints the answer it receives.
 *                RESPONDER (CrowPanel 4.3"): prints every line it receives
 *                          and answers with its own greeting.
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, C11. English comments.
 *
 * Wiring (crossed): TX of one board -> RX of the other, and GND <-> GND.
 *   CrowPanel UART1 connector: TX = IO17, RX = IO18 (Elecrow wiki).
 *   Do NOT join the supply wire (red) when both boards have their own USB.
 *
 * Expected output on the sensor node monitor:
 *   I (...) Enlace: TX -> Hola soy un ESP32-S3
 *   I (...) Enlace: RX <- Hola soy un CrowPanel 4,3 inch
 */
#ifndef ENLACE_AIOT_H
#define ENLACE_AIOT_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Role of this board in the link test */
typedef enum {
    ENLACE_AIOT_INITIATOR = 0,   /* sends first, waits for the answer */
    ENLACE_AIOT_RESPONDER = 1,   /* waits, answers every line received */
} Enlace_AIoT_role_t;

/* Link configuration */
typedef struct {
    Enlace_AIoT_role_t role;
    int         uart_num;        /* 1 or 2 (UART0 is the USB console) */
    int         tx_pin;          /* TX GPIO of this board */
    int         rx_pin;          /* RX GPIO of this board */
    int         baud_rate;       /* same value on both boards, e.g. 115200 */
    const char *greeting;        /* text this board sends, without '\n' */
    uint32_t    period_ms;       /* INITIATOR: time between greetings */
    int         task_core;       /* 0 = core of WiFi / system tasks */
} Enlace_AIoT_config_t;

/**
 * @brief Starts the link task (returns immediately).
 *        The configuration is copied, the strings must stay valid
 *        (string literals are fine).
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE (already started),
 *         ESP_ERR_NO_MEM or the UART driver error.
 */
esp_err_t Enlace_AIoT_Start(const Enlace_AIoT_config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* ENLACE_AIOT_H */
