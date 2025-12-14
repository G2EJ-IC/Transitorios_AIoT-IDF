#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Inicializa todo el hardware (Pantalla, Touch, I2C, etc.) y LVGL
esp_err_t Configuracion_AIoT_Init(void);

#ifdef __cplusplus
}
#endif