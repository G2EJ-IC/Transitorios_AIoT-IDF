#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Inicializa el PWM para el brillo y los timers
void sys_control_aiot_init(void);

// Controlar el brillo (0-100)
void sys_control_aiot_set_backlight(int32_t percent);

// Obtener el tiempo formateado "00d 00h 00m 00.000s"
void sys_control_aiot_get_uptime(char *buffer, size_t size);

#ifdef __cplusplus
}
#endif