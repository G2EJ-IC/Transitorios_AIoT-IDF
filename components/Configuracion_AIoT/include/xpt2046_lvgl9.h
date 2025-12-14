#pragma once

#include "lvgl.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicializa el driver XPT2046
 * @param host  Host SPI (ej. SPI2_HOST)
 * @param cs_pin Pin Chip Select
 * @param irq_pin Pin de Interrupción (IRQ)
 */
void xpt2046_init_driver(spi_host_device_t host, int cs_pin, int irq_pin);

/**
 * @brief Callback de lectura para LVGL 9
 * @param indev Driver de entrada de LVGL
 * @param data  Estructura de datos para rellenar (coordenadas)
 */
void xpt2046_read_cb_lvgl9(lv_indev_t * indev, lv_indev_data_t * data);

#ifdef __cplusplus
}
#endif