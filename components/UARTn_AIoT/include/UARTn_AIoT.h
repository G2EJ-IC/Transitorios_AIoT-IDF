#ifndef UARTN_AIOT_H
#define UARTN_AIOT_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// DEFINICIÓN DEL HANDLE OPACO
typedef struct UARTn_Instance* UARTn_handle_t;

// -----------------------------------------------------------------------------
// API PÚBLICA (Compatible con C)
// -----------------------------------------------------------------------------

/**
 * @brief Crea una nueva instancia del objeto UART (Constructor)
 */
UARTn_handle_t UARTn_create(int uart_num, int tx_pin, int rx_pin, int baud_rate);

/**
 * @brief Inicializa el hardware UART
 */
esp_err_t UARTn_init(UARTn_handle_t handle);

/**
 * @brief Escribe datos en el UART
 */
void UARTn_write(UARTn_handle_t handle, const char *msg);

/**
 * @brief Lee datos hasta encontrar un caracter terminador (ej. '\n')
 */
int UARTn_read_until(UARTn_handle_t handle, char terminator, char *buffer, int max_len);

/**
 * @brief Limpia el buffer de recepción (Flush Input)
 * Útil antes de enviar un comando para asegurar que la respuesta sea fresca.
 */
void UARTn_flush(UARTn_handle_t handle);

/**
 * @brief Destruye la instancia y libera memoria (Destructor)
 */
void UARTn_destroy(UARTn_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // UARTN_AIOT_H