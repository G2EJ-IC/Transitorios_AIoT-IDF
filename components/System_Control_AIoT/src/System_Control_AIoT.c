#include "System_Control_AIoT.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "System_Control";

// Configuración Hardware (GPIO 2 es Backlight en CrowPanel 4.3)
#define BL_PIN 2

void sys_control_aiot_init(void)
{
    // Configurar PWM (LEDC)
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT, // 0-1023
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = BL_PIN,
        .duty = 1023, // 100% encendido
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
    
    ESP_LOGI(TAG, "System Control Iniciado");
}

void sys_control_aiot_set_backlight(int32_t percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    
    // Mapear 0-100% a 0-1023
    uint32_t duty = (percent * 1023) / 100;
    
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void sys_control_aiot_get_uptime(char *buffer, size_t size)
{
    int64_t time_us = esp_timer_get_time();
    uint64_t total_ms = time_us / 1000;
    
    // --- FILTRO DE SUAVIDAD (Múltiplos de 250ms) ---
    // Esto evita que los números se vean borrosos por cambiar muy rápido
    int raw_ms = total_ms % 1000;
    int sub_seg = (raw_ms / 250) * 250; 

    int seg_t = total_ms / 1000;
    int dia = seg_t / 86400; seg_t %= 86400;
    int hora = seg_t / 3600; seg_t %= 3600;
    int min = seg_t / 60; seg_t %= 60;
    int seg = seg_t;

    snprintf(buffer, size, "%02d d %02d h %02d m %02d.%03d s", 
                dia, hora, min, seg, sub_seg);
}