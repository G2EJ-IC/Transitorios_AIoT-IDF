#include "xpt2046_lvgl9.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h" // Para esp_rom_delay_us
#include <string.h>

// static const char *TAG = "XPT2046";

static spi_device_handle_t touch_spi_handle;
static int touch_irq_pin;

#define CMD_X_READ  0xD0
#define CMD_Y_READ  0x90

static int16_t spi_transfer_cmd(uint8_t cmd)
{
    uint8_t data_out[3] = {cmd, 0x00, 0x00};
    uint8_t data_in[3] = {0, 0, 0};

    spi_transaction_t t = {
        .length = 24,
        .tx_buffer = data_out,
        .rx_buffer = data_in,
        .flags = 0
    };
    esp_err_t ret = spi_device_polling_transmit(touch_spi_handle, &t);
    if (ret != ESP_OK) return -1;
    int16_t val = ((data_in[1] << 8) | data_in[2]) >> 3;
    return val;
}

void xpt2046_init_driver(spi_host_device_t host, int cs_pin, int irq_pin) 
{
    touch_irq_pin = irq_pin;
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << irq_pin);
    io_conf.pull_up_en = 1; 
    gpio_config(&io_conf);

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 1 * 1000 * 1000, 
        .mode = 0,
        .spics_io_num = cs_pin,
        .queue_size = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(host, &devcfg, &touch_spi_handle));
}

long map(long x, long in_min, long in_max, long out_min, long out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void xpt2046_read_cb_lvgl9(lv_indev_t * indev, lv_indev_data_t * data)
{
    // 1. LECTURA INICIAL
    if (gpio_get_level(touch_irq_pin) == 0) {
        
        // --- ANTI-REBOTE (DEBOUNCE) ---
        // El ruido eléctrico suele durar microsegundos. Un toque humano dura milisegundos.
        // Esperamos 5ms y verificamos si sigue presionado.
        esp_rom_delay_us(5000); 
        
        if (gpio_get_level(touch_irq_pin) != 0) {
            // Falsa alarma (era ruido), salimos indicando que no hay toque
            data->state = LV_INDEV_STATE_RELEASED;
            return;
        }
        // ------------------------------

        int32_t avg_x = 0;
        int32_t avg_y = 0;
        const int samples = 4;

        for (int i = 0; i < samples; i++) {
            avg_x += spi_transfer_cmd(CMD_X_READ);
            avg_y += spi_transfer_cmd(CMD_Y_READ);
        }
        
        int32_t cal_x = map(avg_x / samples, 200, 3900, 0, 480);
        int32_t cal_y = map(avg_y / samples, 240, 3800, 0, 272);

        if (cal_x < 0) cal_x = 0;
        if (cal_x > 479) cal_x = 479;
        if (cal_y < 0) cal_y = 0;
        if (cal_y > 271) cal_y = 271;

        data->point.x = cal_x;
        data->point.y = cal_y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}