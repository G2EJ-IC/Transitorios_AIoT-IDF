/*
 * File: main/main_AIoT.c
 * Description: Application entry point.
 *              Core 1: RGB panel interrupts + LVGL + EEZ Flow (ui_task).
 *              Core 0: WiFi, lwIP, esp_timer and the rest of the system.
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, LVGL 9.x. English comments.
 *
 * Revision 3 (WiFi jitter fix):
 *  Symptom: once WiFi is connected the picture jumps/shifts even on a static
 *           screen. Before connecting (first video) it did not happen.
 *  Cause:   the RGB bounce-buffer refill runs in an interrupt that is allocated
 *           on the core that creates the panel. It was core 0, the same core as
 *           the WiFi driver interrupts and tasks; their latency made the refill
 *           arrive late -> DMA underrun -> image shifted until the next VSYNC.
 *  Fix:     the panel (and therefore its interrupts) is created INSIDE ui_task,
 *           which is pinned to core 1. WiFi keeps core 0 for itself.
 *
 * Revision 7 (instant tab change):
 *  The Main3 tabview content was horizontally scrollable: any small drag of
 *  the stylus on the resistive panel slid the content between the
 *  "Connec To WiFi" and "AIoT Transitorios" tabs with an animation.
 *  ui_apply_runtime_tweaks() disables that scrolling after ui_init(), so tabs
 *  change only with the tab bar buttons, instantly (LVGL uses LV_ANIM_OFF).
 *  Done here and not in the EEZ files, because EEZ regenerates screens.c.
 *
 * Start-up order (same as the original program):
 *   1. app_main  : NVS
 *   2. ui_task   : Configuracion_AIoT_Init()  (LCD + touch + LVGL on core 1)
 *   3. app_main  : IO_AIoT_Init(), wifi_init_sta()
 *   4. ui_task   : ui_init() and UI loop
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_task_wdt.h"

/* Components */
#include "Configuracion_AIoT.h"
#include "System_Defines_AIoT.h"
#include "WiFi_AIoT.h"
#include "IO_AIoT.h"
#include "ui.h"
#include "screens.h"
#include "lvgl.h"

/* Custom UI logic (clock labels, WiFi status...) implemented in EEZ_AIoT */
extern void ui_update_periodic_task(void);

static const char *TAG = "Main_App";

/* Period for ui_update_periodic_task(): the uptime label has 250 ms
 * resolution, so 50 ms is more than enough. */
#define UI_PERIODIC_UPDATE_MS   50

/* Upper bound for the UI loop sleep (ms) */
#define UI_LOOP_MAX_SLEEP_MS    10

/* Start-up synchronization between app_main (core 0) and ui_task (core 1) */
static SemaphoreHandle_t s_hw_ready_sem = NULL;    /* ui_task  -> app_main */
static SemaphoreHandle_t s_sys_ready_sem = NULL;   /* app_main -> ui_task  */
static volatile esp_err_t s_hw_init_result = ESP_FAIL;

/* -------------------------------------------------------------------------- */
/* Adjustments applied once after ui_init() (survive EEZ code regeneration)    */
/* -------------------------------------------------------------------------- */
static void ui_apply_runtime_tweaks(void)
{
    /* Main3 tabview: no swipe between tabs, only instant tab-bar clicks */
    if (objects.tab_view_main2 != NULL) {
        lv_obj_t *tab_content = lv_tabview_get_content(objects.tab_view_main2);
        if (tab_content != NULL) {
            /* LVGL 9.6 dedicated setters (lv_obj_remove_flag is deprecated) */
            lv_obj_set_scrollable(tab_content, false);
            lv_obj_set_scroll_elastic(tab_content, false);
            lv_obj_set_scroll_momentum(tab_content, false);
        }
        ESP_LOGI(TAG, "Tabview: swipe disabled, instant tab change");
    }
}

/* -------------------------------------------------------------------------- */
/* UI task: the ONLY task that calls lv_* / EEZ functions without the mutex.   */
/* -------------------------------------------------------------------------- */
static void ui_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "UI task running on core %d", (int)xPortGetCoreID());

    /* 1. LCD + touch + LVGL. Created here so the RGB/GDMA interrupts that
     *    refill the bounce buffers are allocated on core 1, away from WiFi. */
    s_hw_init_result = Configuracion_AIoT_Init();
    xSemaphoreGive(s_hw_ready_sem);
    if (s_hw_init_result != ESP_OK) {
        ESP_LOGE(TAG, "Hardware initialization failed!");
        vTaskDelete(NULL);
        return;
    }

    /* 2. Wait until app_main has started the backlight PWM and WiFi */
    xSemaphoreTake(s_sys_ready_sem, portMAX_DELAY);

    /* 3. Build the EEZ UI (still single-threaded with respect to LVGL) */
    if (Configuracion_AIoT_Lock(0)) {
        ui_init();
        ui_apply_runtime_tweaks();
        Configuracion_AIoT_Unlock();
    }

    esp_task_wdt_add(NULL);
    int64_t last_periodic_us = 0;

    for (;;) {
        uint32_t time_until_next = UI_LOOP_MAX_SLEEP_MS;

        if (Configuracion_AIoT_Lock(0)) {
            /* A. LVGL timers: refresh, input read, animations */
            time_until_next = lv_timer_handler();

            /* B. EEZ Flow tick */
            ui_tick();

            /* C. Custom UI logic, throttled */
            int64_t now_us = esp_timer_get_time();
            if ((now_us - last_periodic_us) >= (UI_PERIODIC_UPDATE_MS * 1000LL)) {
                last_periodic_us = now_us;
                ui_update_periodic_task();
                IO_Task_Manager(); /* Backlight / auto-suspend state machine */
            }

            Configuracion_AIoT_Unlock();
        }

        /* D. Watchdog */
        esp_task_wdt_reset();

        /* E. Sleep until the next LVGL timer is due (1..10 ms) */
        if (time_until_next == LV_NO_TIMER_READY || time_until_next > UI_LOOP_MAX_SLEEP_MS) {
            time_until_next = UI_LOOP_MAX_SLEEP_MS;
        }
        if (time_until_next < 1) {
            time_until_next = 1;
        }
        vTaskDelay(pdMS_TO_TICKS(time_until_next));
    }
}

void app_main(void)
{
    /* 1. NVS (required by WiFi) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_hw_ready_sem = xSemaphoreCreateBinary();
    s_sys_ready_sem = xSemaphoreCreateBinary();
    if (s_hw_ready_sem == NULL || s_sys_ready_sem == NULL) {
        ESP_LOGE(TAG, "Could not create start-up semaphores");
        return;
    }

    /* 2. Start the UI task on core 1; it initializes the display hardware */
    BaseType_t ok = xTaskCreatePinnedToCore(ui_task, "ui_task",
                                            AIOT_UI_TASK_STACK_BYTES, NULL,
                                            AIOT_UI_TASK_PRIORITY, NULL,
                                            AIOT_UI_TASK_CORE);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Could not create ui_task");
        return;
    }

    /* Wait for LCD + touch + LVGL to be ready */
    xSemaphoreTake(s_hw_ready_sem, portMAX_DELAY);
    if (s_hw_init_result != ESP_OK) {
        return;
    }

    /* 3. Backlight PWM and power management */
    IO_AIoT_Init();

    /* 4. WiFi station (WiFi task is pinned to core 0 by default) */
    wifi_init_sta();

    /* 5. Let the UI task build the screens and enter its loop */
    xSemaphoreGive(s_sys_ready_sem);

    ESP_LOGI(TAG, "System initialized on core %d. UI runs on core %d",
             (int)xPortGetCoreID(), AIOT_UI_TASK_CORE);
    /* app_main may return: its task is deleted, the scheduler keeps running */
}
