/*
 * File: components/Configuracion_AIoT/include/Configuracion_AIoT.h
 * Description: Hardware bring-up (RGB LCD, XPT2046 touch, LVGL port) for the
 *              CrowPanel 4.3" and a mutex to access LVGL from other tasks.
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, LVGL 9.x. English comments.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the RGB panel, the touch controller and LVGL.
 *
 * Call it from the UI task pinned to core 1 (see main_AIoT.c): the RGB panel
 * and GDMA interrupts (bounce buffer refill) are allocated on the calling
 * core. On core 0 they compete with the WiFi interrupts, arrive late and the
 * image jumps (DMA underrun). On core 1 they are isolated from WiFi.
 */
esp_err_t Configuracion_AIoT_Init(void);

/**
 * @brief Takes the LVGL mutex (recursive).
 *
 * LVGL is NOT thread safe. Any task other than the UI task that calls an
 * lv_* function (or an EEZ set_var_* that touches widgets) must wrap the call
 * between Configuracion_AIoT_Lock() and Configuracion_AIoT_Unlock().
 *
 * @param timeout_ms Maximum wait in milliseconds (0 = wait forever).
 * @return true if the mutex was taken.
 */
bool Configuracion_AIoT_Lock(uint32_t timeout_ms);

/**
 * @brief Releases the LVGL mutex.
 */
void Configuracion_AIoT_Unlock(void);

#ifdef __cplusplus
}
#endif
