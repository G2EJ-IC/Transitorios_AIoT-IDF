/*
 * File: components/Configuracion_AIoT/src/xpt2046_lvgl9.c
 * Description: Minimal XPT2046 resistive touch driver for LVGL 9.
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, LVGL 9.x. English comments.
 *
 * Changes versus the previous version:
 *  - SPI clock 2 MHz (datasheet max DCLK is 2.5 MHz; 4 MHz produced noisy samples).
 *  - Pressure (Z1/Z2) check: no more phantom points on press/release edges.
 *  - First conversion after switching channel is discarded (settling time).
 *  - Median-of-5 filter per axis + 2 px hysteresis: a finger held still does not
 *    generate new coordinates, so LVGL does not invalidate/redraw for nothing.
 *  - No artificial debounce period (removed one full read period of latency).
 *  - Calibration and screen size come from System_Defines_AIoT.h.
 */
#include "xpt2046_lvgl9.h"
#include "System_Defines_AIoT.h"

#include <stdlib.h>
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "XPT2046";

/* XPT2046 control bytes: S=1, 12-bit, differential reference, PD1:PD0 = 00
 * (power down between conversions, PENIRQ enabled). */
#define XPT_CMD_X       0xD0
#define XPT_CMD_Y       0x90
#define XPT_CMD_Z1      0xB0
#define XPT_CMD_Z2      0xC0

#define XPT_SAMPLES     5   /* Samples per axis, filtered with a median */
#define XPT_HYST_PX     2   /* Ignore movements smaller than this (pixels) */

static spi_device_handle_t s_touch_spi = NULL;
static int s_irq_pin = -1;
static int32_t s_last_x = 0;
static int32_t s_last_y = 0;
static bool s_was_pressed = false;

/* Sends one control byte and returns the 12-bit conversion result (or -1). */
static int32_t xpt_read12(uint8_t cmd)
{
    spi_transaction_t t = {
        .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
        .length = 24,
        .tx_data = { cmd, 0x00, 0x00, 0x00 },
    };
    if (spi_device_polling_transmit(s_touch_spi, &t) != ESP_OK) {
        return -1;
    }
    return (int32_t)((((uint16_t)t.rx_data[1] << 8) | t.rx_data[2]) >> 3) & 0x0FFF;
}

/* Reads one axis: discards the first conversion, returns the median of N. */
static int32_t xpt_read_axis(uint8_t cmd)
{
    int32_t s[XPT_SAMPLES];
    (void)xpt_read12(cmd); /* settling sample, discarded */
    for (int i = 0; i < XPT_SAMPLES; i++) {
        s[i] = xpt_read12(cmd);
    }
    /* Insertion sort (N is tiny) */
    for (int i = 1; i < XPT_SAMPLES; i++) {
        int32_t k = s[i];
        int j = i - 1;
        while (j >= 0 && s[j] > k) {
            s[j + 1] = s[j];
            j--;
        }
        s[j + 1] = k;
    }
    return s[XPT_SAMPLES / 2];
}

/* Touch pressure estimate: higher value = firmer press. */
static int32_t xpt_read_pressure(void)
{
    int32_t z1 = xpt_read12(XPT_CMD_Z1);
    int32_t z2 = xpt_read12(XPT_CMD_Z2);
    if (z1 < 0 || z2 < 0) {
        return 0;
    }
    return z1 + 4095 - z2;
}

static inline int32_t xpt_map(int32_t x, int32_t in_min, int32_t in_max, int32_t out_min, int32_t out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

static inline int32_t xpt_clamp(int32_t v, int32_t lo, int32_t hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

void xpt2046_init_driver(spi_host_device_t host, int cs_pin, int irq_pin)
{
    s_irq_pin = irq_pin;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << irq_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = AIOT_TOUCH_SPI_CLOCK_HZ,
        .mode = 0,
        .spics_io_num = cs_pin,
        .queue_size = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(host, &devcfg, &s_touch_spi));
    ESP_LOGI(TAG, "XPT2046 ready (SPI %d Hz, IRQ GPIO %d)", AIOT_TOUCH_SPI_CLOCK_HZ, irq_pin);
}

void xpt2046_read_cb_lvgl9(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;

    /* Fast path: PENIRQ high -> nobody is touching the panel (no SPI traffic). */
    bool pressed = (gpio_get_level((gpio_num_t)s_irq_pin) == 0);

    if (pressed && xpt_read_pressure() < AIOT_TOUCH_Z_THRESHOLD) {
        pressed = false; /* Too light: edge of a press/release, ignore */
    }

    if (pressed) {
        int32_t raw_x = xpt_read_axis(XPT_CMD_X);
        int32_t raw_y = xpt_read_axis(XPT_CMD_Y);

        int32_t x = xpt_map(raw_x, AIOT_TOUCH_RAW_X_MIN, AIOT_TOUCH_RAW_X_MAX, 0, AIOT_LCD_H_RES);
        int32_t y = xpt_map(raw_y, AIOT_TOUCH_RAW_Y_MIN, AIOT_TOUCH_RAW_Y_MAX, 0, AIOT_LCD_V_RES);
        x = xpt_clamp(x, 0, AIOT_LCD_H_RES - 1);
        y = xpt_clamp(y, 0, AIOT_LCD_V_RES - 1);

        /* Hysteresis: keep the previous point while the finger is still */
        if (!s_was_pressed || abs(x - s_last_x) > XPT_HYST_PX || abs(y - s_last_y) > XPT_HYST_PX) {
            s_last_x = x;
            s_last_y = y;
        }
        s_was_pressed = true;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        s_was_pressed = false;
        data->state = LV_INDEV_STATE_RELEASED;
    }

    /* On release LVGL needs the last valid point (avoids PRESS_LOST on click) */
    data->point.x = s_last_x;
    data->point.y = s_last_y;
}
