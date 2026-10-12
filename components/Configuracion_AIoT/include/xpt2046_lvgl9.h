/*
 * File: components/Configuracion_AIoT/include/xpt2046_lvgl9.h
 * Description: Minimal XPT2046 resistive touch driver for LVGL 9 (polling,
 *              pressure check, median filtering).
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, LVGL 9.x. English comments.
 */
#pragma once

#include "lvgl.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Registers the XPT2046 on an already initialized SPI bus.
 * @param host    SPI host (e.g. SPI2_HOST)
 * @param cs_pin  Chip select GPIO
 * @param irq_pin PENIRQ GPIO (active low while the panel is pressed)
 */
void xpt2046_init_driver(spi_host_device_t host, int cs_pin, int irq_pin);

/**
 * @brief LVGL 9 pointer read callback.
 * @param indev LVGL input device
 * @param data  Output: point and state
 */
void xpt2046_read_cb_lvgl9(lv_indev_t *indev, lv_indev_data_t *data);

#ifdef __cplusplus
}
#endif
