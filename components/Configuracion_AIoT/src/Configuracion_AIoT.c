#include "Configuracion_AIoT.h"
#include "System_Defines_AIoT.h"
#include "xpt2046_lvgl9.h" 

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "Config_AIoT";

// --- PINES CROWPANEL 4.3 BASIC ---
#define MY_DISP_PCLK    42
#define MY_DISP_VSYNC   41
#define MY_DISP_HSYNC   39
#define MY_DISP_DE      40
#define MY_DISP_BL      2

// TOUCH SPI
#define MY_TOUCH_HOST   SPI2_HOST
#define MY_TOUCH_CLK    12
#define MY_TOUCH_MOSI   11
#define MY_TOUCH_MISO   13
#define MY_TOUCH_CS     0   
#define MY_TOUCH_IRQ    36

static esp_lcd_panel_handle_t panel_handle = NULL;
static lv_display_t *lv_disp = NULL;
static lv_indev_t *lv_indev = NULL;

static void lvgl_tick_task(void *arg) {
    lv_tick_inc(2); 
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
    int x1 = area->x1;
    int y1 = area->y1;
    int x2 = area->x2;
    int y2 = area->y2;
    esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x2 + 1, y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

// ESTA ES LA FUNCIÓN QUE TE FALTA
esp_err_t Configuracion_AIoT_Init(void) {
    ESP_LOGI(TAG, "Iniciando Hardware (7MHz / Buffer x8)...");

    // 1. Configurar Panel RGB
    esp_lcd_rgb_panel_config_t panel_config = {
        .data_width = 16,
        .psram_trans_align = 64,
        .num_fbs = 1,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .disp_gpio_num = GPIO_NUM_NC,
        .pclk_gpio_num = MY_DISP_PCLK,
        .vsync_gpio_num = MY_DISP_VSYNC,
        .hsync_gpio_num = MY_DISP_HSYNC,
        .de_gpio_num = MY_DISP_DE,
        .data_gpio_nums = {
            GPIO_NUM_8,  GPIO_NUM_3,  GPIO_NUM_46, GPIO_NUM_9,  GPIO_NUM_1,  GPIO_NUM_5, 
            GPIO_NUM_6,  GPIO_NUM_7,  GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_4,  GPIO_NUM_45, 
            GPIO_NUM_48, GPIO_NUM_47, GPIO_NUM_21, GPIO_NUM_14
        },
        .timings = {
            .pclk_hz = 7 * 1000 * 1000, // 7 MHz
            .h_res = 480,
            .v_res = 272,
            .hsync_back_porch = 43, .hsync_front_porch = 8, .hsync_pulse_width = 4,
            .vsync_back_porch = 12, .vsync_front_porch = 8, .vsync_pulse_width = 4,
            .flags.pclk_active_neg = 1,
        },
        .flags.fb_in_psram = 1, 
        .bounce_buffer_size_px = 480 * 8, // Buffer x8 (Estable)
    };

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    // 2. Backlight ON
    gpio_config_t bl_conf = {
        .pin_bit_mask = (1ULL << MY_DISP_BL),
        .mode = GPIO_MODE_OUTPUT, .pull_up_en = 0, .pull_down_en = 0, .intr_type = 0
    };
    gpio_config(&bl_conf);
    gpio_set_level((gpio_num_t)MY_DISP_BL, 1);

    // 3. Touch SPI
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = MY_TOUCH_MOSI,
        .miso_io_num = MY_TOUCH_MISO,
        .sclk_io_num = MY_TOUCH_CLK,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = 0
    };
    spi_bus_initialize(MY_TOUCH_HOST, &bus_cfg, SPI_DMA_CH_AUTO);

    // 4. Iniciar Driver XPT2046
    xpt2046_init_driver(MY_TOUCH_HOST, MY_TOUCH_CS, MY_TOUCH_IRQ);

    // 5. LVGL
    lv_init();
    
    const esp_timer_create_args_t lvgl_tick_timer_args = { .callback = &lvgl_tick_task, .name = "lvgl_tick" };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, 2000));

    lv_disp = lv_display_create(480, 272);
    lv_display_set_user_data(lv_disp, panel_handle);
    lv_display_set_flush_cb(lv_disp, lvgl_flush_cb);

    // Buffer x8 Líneas
    #define BUFFER_LINES 8 
    void *buf1 = heap_caps_malloc(480 * BUFFER_LINES * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    void *buf2 = heap_caps_malloc(480 * BUFFER_LINES * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    
    lv_display_set_buffers(lv_disp, buf1, buf2, 480 * BUFFER_LINES * sizeof(uint16_t), LV_DISPLAY_RENDER_MODE_PARTIAL);

    // Conectar Touch
    lv_indev = lv_indev_create();
    lv_indev_set_type(lv_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lv_indev, xpt2046_read_cb_lvgl9);

    return ESP_OK;
}