/*
 * File: components/UARTn_AIoT/include/UARTn_AIoT.h
 * Description: UART library for ESP-IDF. The logic lives in the C++ class
 *              UARTn_AIoT (src/UARTn_AIoT.cpp); this header exposes a C API
 *              with an opaque handle, so it can be used from C and C++.
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, C11 / C++. English comments.
 *
 * Life cycle:
 *   UARTn_handle_t gsm = UARTn_create(1, 17, 18, 115200);   // constructor
 *   UARTn_set_timeout(gsm, 500);                            // optional
 *   UARTn_init(gsm);                                        // hardware
 *   UARTn_write(gsm, "AT\r\n");
 *   UARTn_read_until(gsm, '\n', line, sizeof(line));
 *   UARTn_destroy(gsm);                                     // destructor
 *
 * Notes:
 *  - UART0 is the USB/serial console of the CrowPanel: use UART1 or UART2.
 *  - Pin value -1 keeps the default pin of that UART.
 *  - Frame format 8N1, no hardware flow control, RX buffer 2048 bytes.
 *  - Thread safe: one internal mutex per object.
 *  - RS-485 (e.g. Modbus RTU transmitters): call UARTn_set_rs485() before
 *    UARTn_init(); the driver then drives the transceiver DE/RE pin itself.
 */
#ifndef UARTN_AIOT_H
#define UARTN_AIOT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle: points to a UARTn_AIoT C++ object */
typedef struct UARTn_Instance *UARTn_handle_t;

/* Default values */
#define UARTN_AIOT_RX_BUF_SIZE        2048   /* driver RX ring buffer (SIM800L safe) */
#define UARTN_AIOT_LINE_BUF_SIZE      512    /* internal line accumulator */
#define UARTN_AIOT_DEFAULT_TIMEOUT_MS 1000   /* read timeout */

/* -------------------------------------------------------------------------- */
/* Constructor / destructor                                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Creates a UART object (constructor). No hardware is touched yet.
 * @param uart_num  UART port (0, 1 or 2 on the ESP32-S3)
 * @param tx_pin    TX GPIO (-1 = default pin)
 * @param rx_pin    RX GPIO (-1 = default pin)
 * @param baud_rate Baud rate (e.g. 9600, 115200)
 * @return Handle, or NULL if the arguments are invalid or there is no memory.
 */
UARTn_handle_t UARTn_create(int uart_num, int tx_pin, int rx_pin, int baud_rate);

/**
 * @brief Installs the UART driver and configures pins and baud rate.
 *        Calling it twice is harmless.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE (port already in
 *         use by another driver) or the driver error.
 */
esp_err_t UARTn_init(UARTn_handle_t handle);

/**
 * @brief Uninstalls the driver (only if it was installed) and frees the object
 *        (destructor). The handle must not be used afterwards.
 */
void UARTn_destroy(UARTn_handle_t handle);

/* -------------------------------------------------------------------------- */
/* Configuration                                                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Sets the timeout of the read functions in milliseconds.
 *        0 = non-blocking (return immediately with whatever is available).
 */
void UARTn_set_timeout(UARTn_handle_t handle, uint32_t timeout_ms);

/**
 * @brief Changes the baud rate (works before and after UARTn_init).
 */
esp_err_t UARTn_set_baud_rate(UARTn_handle_t handle, int baud_rate);

/**
 * @brief Enables RS-485 half-duplex mode (call BEFORE UARTn_init).
 *        The UART hardware raises de_pin while transmitting and lowers it to
 *        receive. Wire it to DE and /RE (joined) of a MAX485/SP3485-type
 *        transceiver. The receiver is off while sending, so there is no echo.
 * @param de_pin GPIO connected to DE + /RE of the transceiver
 * @return ESP_OK, ESP_ERR_INVALID_ARG or ESP_ERR_INVALID_STATE (already init)
 */
esp_err_t UARTn_set_rs485(UARTn_handle_t handle, int de_pin);

/* -------------------------------------------------------------------------- */
/* Transmission                                                                */
/* -------------------------------------------------------------------------- */

/**
 * @brief Writes a null-terminated string.
 */
void UARTn_write(UARTn_handle_t handle, const char *msg);

/**
 * @brief Writes raw bytes (binary safe, 0x00 allowed).
 * @return Number of bytes written, or -1 on error.
 */
int UARTn_write_bytes(UARTn_handle_t handle, const uint8_t *data, size_t len);

/* -------------------------------------------------------------------------- */
/* Reception                                                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief Reads one line ending with the terminator.
 *
 * - The terminator is consumed and NOT stored; the buffer is always
 *   null-terminated.
 * - Bytes received after the terminator stay in the UART buffer for the next
 *   call (nothing is lost).
 * - If the timeout expires before the terminator arrives, the partial line is
 *   kept inside the object and the function returns 0; the next call
 *   continues the same line. This allows polling with timeout 0.
 * - If the line is longer than max_len - 1, it is delivered in pieces.
 * - Empty lines (terminator with nothing before it) are skipped.
 *
 * @param handle     UART object
 * @param terminator End character (e.g. '\n')
 * @param buffer     Destination
 * @param max_len    Size of the destination INCLUDING the final '\0'
 * @return Characters stored (> 0 = complete line or piece), 0 = no complete
 *         line yet, -1 = error.
 */
int UARTn_read_until(UARTn_handle_t handle, char terminator, char *buffer, int max_len);

/**
 * @brief Reads up to len raw bytes, waiting at most the configured timeout.
 * @return Number of bytes read (may be less than len), -1 on error.
 */
int UARTn_read_bytes(UARTn_handle_t handle, uint8_t *data, size_t len);

/**
 * @brief Number of bytes waiting in the UART RX buffer.
 */
size_t UARTn_available(UARTn_handle_t handle);

/**
 * @brief Discards everything received (UART buffer and partial line).
 *        Useful before sending a command so the answer read afterwards is fresh.
 */
void UARTn_flush(UARTn_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* UARTN_AIOT_H */
