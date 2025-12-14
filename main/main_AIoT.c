#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_task_wdt.h"

// COMPONENTS
#include "Configuracion_AIoT.h"
#include "WiFi_AIoT.h"
#include "IO_AIoT.h"
// #include "Bluetooth_AIoT.h" // REMOVED: Bluetooth module disabled
#include "ui.h" 
#include "lvgl.h"

// External declaration
extern void ui_update_periodic_task(void);

static const char *TAG = "Main_App";

void app_main(void)
{
    // 1. Initialize NVS (Non-Volatile Storage)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    
    // 2. Initialize General Hardware (LCD, Touch, etc.)
    if (Configuracion_AIoT_Init() != ESP_OK) {
        ESP_LOGE(TAG, "Hardware initialization failed!");
        return;
    }
    
    IO_AIoT_Init();        // Power Management & Backlight
    // Bluetooth_AIoT_Init(); // REMOVED
    wifi_init_sta();       // WiFi Station Mode
    
    // 3. Initialize UI (EEZ Studio / LVGL)
    ui_init(); 
    
    // 4. Add Main Task to Watchdog
    esp_task_wdt_add(NULL);

    ESP_LOGI(TAG, "System Initialized. Entering Main Loop...");

    for (;;) {
        // A. LVGL Tick Handler
        uint32_t time_until_next = lv_timer_handler();
        
        // B. EEZ Flow Tick
        ui_tick();
        
        // C. Custom UI Logic (Clock, WiFi status, Power)
        ui_update_periodic_task();
        
        // D. Watchdog Reset
        esp_task_wdt_reset();

        // E. Intelligent Delay to prevent starvation
        if (time_until_next > 10) time_until_next = 10;
        if (time_until_next < 1) time_until_next = 1;

        // F. Power Management Task (Auto-Sleep)
        IO_Task_Manager();

        vTaskDelay(pdMS_TO_TICKS(time_until_next));
    }
}