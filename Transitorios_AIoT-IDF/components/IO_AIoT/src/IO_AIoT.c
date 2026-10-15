#include "IO_AIoT.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"  
#include "esp_timer.h" 

static const char *TAG = "IO_AIOT";

// --- HARDWARE CONFIG ---
#define LCD_BK_LIGHT_PIN    2  

// PWM Config
#define PWM_FREQ_HZ            5000 
#define PWM_RES_BIT            LEDC_TIMER_10_BIT
#define PWM_CHANNEL            LEDC_CHANNEL_0
#define PWM_TIMER              LEDC_TIMER_0
#define PWM_MODE               LEDC_LOW_SPEED_MODE

// --- VARIABLES ---
static int32_t g_current_brightness_percent = 100;
static uint32_t g_suspension_timeout_ms = 0; // 0 = Disabled
static bool g_is_dimmed = false;

// --- PWM FUNCTION ---
static void apply_pwm_brightness(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    // Convert 0-100% to 0-1023
    uint32_t duty = (percent * 1023) / 100;
    
    ledc_set_duty(PWM_MODE, PWM_CHANNEL, duty);
    ledc_update_duty(PWM_MODE, PWM_CHANNEL);
}

// --- INITIALIZATION ---
void IO_AIoT_Init(void) {
    ESP_LOGI(TAG, "Init Backlight PWM...");

    ledc_timer_config_t ledc_timer = {
        .speed_mode       = PWM_MODE,
        .duty_resolution  = PWM_RES_BIT,
        .timer_num        = PWM_TIMER,
        .freq_hz          = PWM_FREQ_HZ,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = PWM_MODE,
        .channel        = PWM_CHANNEL,
        .timer_sel      = PWM_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LCD_BK_LIGHT_PIN,
        .duty           = 0, 
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    apply_pwm_brightness(100); // Turn ON at boot
}

// --- MANUAL CONTROL (Slider) ---
void IO_Set_Brillo_Manual(int32_t percentage) {
    // If suspended (black screen), DO NOT turn on light, just save value.
    if (!g_is_dimmed) {
        if (g_current_brightness_percent != percentage) {
            g_current_brightness_percent = percentage;
            apply_pwm_brightness(percentage);
        }
    } else {
        g_current_brightness_percent = percentage;
    }
}

// --- SUSPENSION TIMEOUT (Dropdown) ---
// CORRECTION: Remapped to match Visual List (0=15s, 1=30s, etc.)
void IO_Set_Tiempo_Suspension(int32_t index) {
    switch (index) {
        case 0: g_suspension_timeout_ms = 15000; break;    // Visual: 15s
        case 1: g_suspension_timeout_ms = 30000; break;    // Visual: 30s
        case 2: g_suspension_timeout_ms = 60000; break;    // Visual: 1m
        case 3: g_suspension_timeout_ms = 120000; break;   // Visual: 2m
        case 4: g_suspension_timeout_ms = 300000; break;   // Visual: 5m
        case 5: g_suspension_timeout_ms = 0; break;        // Visual: Never (or >5m)
        default: g_suspension_timeout_ms = 0; break;       // Default: Never
    }
}

// --- UPTIME CLOCK ---
void IO_Get_Uptime(char *buffer, size_t len) {
    int64_t uptime_us = esp_timer_get_time();
    int uptime_sec = uptime_us / 1000000;
    int days = uptime_sec / 86400;
    int hours = (uptime_sec % 86400) / 3600;
    int minutes = (uptime_sec % 3600) / 60;
    int seconds = (uptime_sec % 60);

    if (days > 0) snprintf(buffer, len, "%dd %02d:%02d:%02d", days, hours, minutes, seconds);
    else snprintf(buffer, len, "%02d:%02d:%02d", hours, minutes, seconds);
}

// --- STATE MACHINE (TASK MANAGER) ---
void IO_Task_Manager(void) {
    // If timeout is 0 (Never), ensure screen is ON
    if (g_suspension_timeout_ms == 0) {
        if (g_is_dimmed) {
            g_is_dimmed = false;
            apply_pwm_brightness(g_current_brightness_percent);
        }
        return;
    }

    // Read LVGL Inactivity Time
    uint32_t inactive_ms = lv_display_get_inactive_time(NULL); 

    if (inactive_ms > g_suspension_timeout_ms) {
        // --- SUSPEND (OFF) ---
        if (!g_is_dimmed) {
            g_is_dimmed = true;
            apply_pwm_brightness(0); // Absolute Zero
            ESP_LOGI(TAG, "System suspended (Screen OFF). Timeout: %d ms", (int)g_suspension_timeout_ms);
        }
    } else {
        // --- WAKE UP (ON) ---
        if (g_is_dimmed) {
            g_is_dimmed = false;
            apply_pwm_brightness(g_current_brightness_percent);
            ESP_LOGI(TAG, "System Woke Up!");
        }
    }
}