/*
 * File: components/Configuracion_AIoT/src/Configuracion_AIoT.c
 * Description: Hardware bring-up for the CrowPanel 4.3" (480x272 RGB, XPT2046)
 *              and LVGL 9 port (display, tick, touch input, mutex).
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, LVGL 9.x. English comments.
 *
 * Performance notes (why this version is faster than the previous one):
 *  1. One PSRAM frame buffer (the second one was never displayed in PARTIAL mode).
 *  2. LVGL draw buffer of 48 lines in internal SRAM (was 8 lines -> 34 render
 *     passes per full screen, each one walking the whole widget tree).
 *  3. LVGL tick taken from esp_timer_get_time() (no 500 Hz timer interrupt).
 *  4. Refresh (33 ms) and touch read (20 ms) periods fixed from code.
 *     See System_Defines_AIoT.h for the PSRAM bandwidth limits.
 *  5. All parameters come from System_Defines_AIoT.h (single source of truth).
 *  6. Must be called from the UI task on core 1 so the RGB/GDMA interrupts
 *     are not delayed by the WiFi interrupts on core 0.
 */
#include "Configuracion_AIoT.h"
#include "System_Defines_AIoT.h"
#include "xpt2046_lvgl9.h"

#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"

static const char *TAG = "Config_AIoT";

/* Compile-time checks for the bounce buffer geometry (see System_Defines_AIoT.h) */
#if AIOT_LCD_BOUNCE_LINES > 0
_Static_assert((AIOT_LCD_V_RES % AIOT_LCD_BOUNCE_LINES) == 0,
               "AIOT_LCD_BOUNCE_LINES must divide AIOT_LCD_V_RES");
_Static_assert(((AIOT_LCD_V_RES / AIOT_LCD_BOUNCE_LINES) % 2) == 0,
               "V_RES / AIOT_LCD_BOUNCE_LINES must be EVEN (RESTART_IN_VSYNC shifts odd counts)");
#endif

static esp_lcd_panel_handle_t s_panel_handle = NULL;
static lv_display_t *s_lv_disp = NULL;
static lv_indev_t *s_lv_indev = NULL;
static SemaphoreHandle_t s_lvgl_mutex = NULL;

/* -------------------------------------------------------------------------- */
/* LVGL callbacks                                                             */
/* -------------------------------------------------------------------------- */

/* Millisecond tick source for LVGL (replaces the periodic esp_timer). */
static uint32_t aiot_lvgl_tick_get_cb(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

/* Copies the rendered area into the PSRAM frame buffer.
 * In copy mode the RGB driver performs a synchronous CPU copy, so the draw
 * buffer can be released immediately. */
static void aiot_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

/* -------------------------------------------------------------------------- */
/* Public API: LVGL mutex                                                     */
/* -------------------------------------------------------------------------- */

bool Configuracion_AIoT_Lock(uint32_t timeout_ms)
{
    if (s_lvgl_mutex == NULL) {
        return false;
    }
    const TickType_t ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return (xSemaphoreTakeRecursive(s_lvgl_mutex, ticks) == pdTRUE);
}

void Configuracion_AIoT_Unlock(void)
{
    if (s_lvgl_mutex != NULL) {
        xSemaphoreGiveRecursive(s_lvgl_mutex);
    }
}

/* -------------------------------------------------------------------------- */
/* Private init helpers                                                       */
/* -------------------------------------------------------------------------- */

static esp_err_t aiot_init_rgb_panel(void)
{
    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = AIOT_LCD_PIXEL_CLOCK_HZ,
            .h_res = AIOT_LCD_H_RES,
            .v_res = AIOT_LCD_V_RES,
            .hsync_pulse_width = AIOT_LCD_HSYNC_PULSE_WIDTH,
            .hsync_back_porch = AIOT_LCD_HSYNC_BACK_PORCH,
            .hsync_front_porch = AIOT_LCD_HSYNC_FRONT_PORCH,
            .vsync_pulse_width = AIOT_LCD_VSYNC_PULSE_WIDTH,
            .vsync_back_porch = AIOT_LCD_VSYNC_BACK_PORCH,
            .vsync_front_porch = AIOT_LCD_VSYNC_FRONT_PORCH,
            .flags.pclk_active_neg = 1,
        },
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = AIOT_LCD_NUM_FBS,
        .bounce_buffer_size_px = AIOT_LCD_H_RES * AIOT_LCD_BOUNCE_LINES,
        .dma_burst_size = 64,
        .hsync_gpio_num = AIOT_PIN_NUM_HSYNC,
        .vsync_gpio_num = AIOT_PIN_NUM_VSYNC,
        .de_gpio_num = AIOT_PIN_NUM_DE,
        .pclk_gpio_num = AIOT_PIN_NUM_PCLK,
        .disp_gpio_num = AIOT_PIN_NUM_DISP_EN,
        .data_gpio_nums = {
            AIOT_PIN_NUM_DATA0,  AIOT_PIN_NUM_DATA1,  AIOT_PIN_NUM_DATA2,  AIOT_PIN_NUM_DATA3,
            AIOT_PIN_NUM_DATA4,  AIOT_PIN_NUM_DATA5,  AIOT_PIN_NUM_DATA6,  AIOT_PIN_NUM_DATA7,
            AIOT_PIN_NUM_DATA8,  AIOT_PIN_NUM_DATA9,  AIOT_PIN_NUM_DATA10, AIOT_PIN_NUM_DATA11,
            AIOT_PIN_NUM_DATA12, AIOT_PIN_NUM_DATA13, AIOT_PIN_NUM_DATA14, AIOT_PIN_NUM_DATA15,
        },
        .flags.fb_in_psram = 1,
    };

    esp_err_t err = esp_lcd_new_rgb_panel(&panel_config, &s_panel_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_rgb_panel failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel_handle));
    return ESP_OK;
}

static void aiot_init_backlight_gpio(void)
{
    /* Plain GPIO ON at boot. IO_AIoT_Init() later takes the pin over with LEDC PWM. */
    gpio_config_t bl_conf = {
        .pin_bit_mask = (1ULL << AIOT_PIN_NUM_BK_LIGHT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&bl_conf);
    gpio_set_level((gpio_num_t)AIOT_PIN_NUM_BK_LIGHT, 1);
}

static esp_err_t aiot_init_touch(void)
{
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = AIOT_PIN_NUM_TOUCH_MOSI,
        .miso_io_num = AIOT_PIN_NUM_TOUCH_MISO,
        .sclk_io_num = AIOT_PIN_NUM_TOUCH_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0, /* default (4092 bytes): keeps the bus usable for a microSD later */
    };
    esp_err_t err = spi_bus_initialize(AIOT_TOUCH_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return err;
    }
    xpt2046_init_driver(AIOT_TOUCH_SPI_HOST, AIOT_PIN_NUM_TOUCH_CS, AIOT_PIN_NUM_TOUCH_IRQ);
    return ESP_OK;
}

static esp_err_t aiot_init_lvgl(void)
{
    lv_init();
    lv_tick_set_cb(aiot_lvgl_tick_get_cb);

    /* Display */
    s_lv_disp = lv_display_create(AIOT_LCD_H_RES, AIOT_LCD_V_RES);
    if (s_lv_disp == NULL) {
        ESP_LOGE(TAG, "lv_display_create failed");
        return ESP_FAIL;
    }
    lv_display_set_color_format(s_lv_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_user_data(s_lv_disp, s_panel_handle);
    lv_display_set_flush_cb(s_lv_disp, aiot_lvgl_flush_cb);

    /* Single draw buffer in internal SRAM (fast rendering).
     * A second buffer gives no benefit here because the flush is synchronous. */
    void *buf = heap_caps_malloc(AIOT_LVGL_DRAW_BUF_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (buf == NULL) {
        ESP_LOGW(TAG, "No internal RAM for draw buffer, falling back to PSRAM (slower)");
        buf = heap_caps_malloc(AIOT_LVGL_DRAW_BUF_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (buf == NULL) {
        ESP_LOGE(TAG, "Draw buffer allocation failed");
        return ESP_ERR_NO_MEM;
    }
    lv_display_set_buffers(s_lv_disp, buf, NULL, AIOT_LVGL_DRAW_BUF_BYTES, LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_timer_t *refr_timer = lv_display_get_refr_timer(s_lv_disp);
    if (refr_timer != NULL) {
        lv_timer_set_period(refr_timer, AIOT_LVGL_REFR_PERIOD_MS);
    }

    /* Touch input */
    s_lv_indev = lv_indev_create();
    lv_indev_set_type(s_lv_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_lv_indev, xpt2046_read_cb_lvgl9);
    lv_indev_set_display(s_lv_indev, s_lv_disp);

    lv_timer_t *read_timer = lv_indev_get_read_timer(s_lv_indev);
    if (read_timer != NULL) {
        lv_timer_set_period(read_timer, AIOT_TOUCH_READ_PERIOD_MS);
    }
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/* Public API: init                                                           */
/* -------------------------------------------------------------------------- */

esp_err_t Configuracion_AIoT_Init(void)
{
    ESP_LOGI(TAG, "Init: PCLK %d Hz, bounce %d lines, LVGL buffer %d lines, core %d",
             AIOT_LCD_PIXEL_CLOCK_HZ, AIOT_LCD_BOUNCE_LINES, AIOT_LVGL_DRAW_BUF_LINES,
             (int)xPortGetCoreID());

    s_lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    if (s_lvgl_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = aiot_init_rgb_panel();
    if (err != ESP_OK) {
        return err;
    }

    aiot_init_backlight_gpio();

    err = aiot_init_touch();
    if (err != ESP_OK) {
        return err;
    }

    err = aiot_init_lvgl();
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "Free internal: %u bytes, free PSRAM: %u bytes",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return ESP_OK;
}
