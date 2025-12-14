#ifndef __SYSTEM_DEFINES_AIOT_H__
#define __SYSTEM_DEFINES_AIOT_H__

/* components/Configuracion_AIoT/include/System_Defines_AIoT.h */
#pragma once

// --- Configuración Física del Panel LCD (NV3047) ---
// Frecuencia de Reloj de Píxel: 10 MHz es el punto óptimo para 480x272 en ESP32-S3 con PSRAM Quad
#define AIOT_LCD_PIXEL_CLOCK_HZ     (10 * 1000 * 1000) 
#define AIOT_LCD_H_RES              480
#define AIOT_LCD_V_RES              272

// Definición de Pines (Mapeo estricto para CrowPanel 4.3 Basic)
#define AIOT_PIN_NUM_BK_LIGHT       2
#define AIOT_PIN_NUM_HSYNC          39
#define AIOT_PIN_NUM_VSYNC          41
#define AIOT_PIN_NUM_DE             40
#define AIOT_PIN_NUM_PCLK           42
#define AIOT_PIN_NUM_DISP_EN        -1 // No conectado en este modelo

// Bus de Datos RGB565
#define AIOT_PIN_NUM_DATA0          8  // B0
#define AIOT_PIN_NUM_DATA1          3  // B1
#define AIOT_PIN_NUM_DATA2          46 // B2
#define AIOT_PIN_NUM_DATA3          9  // B3
#define AIOT_PIN_NUM_DATA4          1  // B4 (MSB Azul)
#define AIOT_PIN_NUM_DATA5          5  // G0
#define AIOT_PIN_NUM_DATA6          6  // G1
#define AIOT_PIN_NUM_DATA7          7  // G2
#define AIOT_PIN_NUM_DATA8          15 // G3
#define AIOT_PIN_NUM_DATA9          16 // G4
#define AIOT_PIN_NUM_DATA10         4  // G5 (MSB Verde)
#define AIOT_PIN_NUM_DATA11         45 // R0
#define AIOT_PIN_NUM_DATA12         48 // R1
#define AIOT_PIN_NUM_DATA13         47 // R2
#define AIOT_PIN_NUM_DATA14         21 // R3
#define AIOT_PIN_NUM_DATA15         14 // R4 (MSB Rojo)

// --- Configuración Táctil XPT2046 (SPI) ---
#define AIOT_TOUCH_SPI_HOST         SPI2_HOST
#define AIOT_PIN_NUM_TOUCH_CS       0
#define AIOT_PIN_NUM_TOUCH_MISO     13
#define AIOT_PIN_NUM_TOUCH_MOSI     11
#define AIOT_PIN_NUM_TOUCH_CLK      12
#define AIOT_PIN_NUM_TOUCH_IRQ      36

// --- Configuración LVGL Middleware ---
#define AIOT_LVGL_TICK_PERIOD_MS    5
// Tamaño del buffer de dibujo LVGL (1/10 de pantalla en SRAM interna para máximo rendimiento)
#define AIOT_LVGL_BUF_SIZE          (AIOT_LCD_H_RES * 40 * sizeof(uint16_t))

#endif // __SYSTEM_DEFINES_AIOT_H__