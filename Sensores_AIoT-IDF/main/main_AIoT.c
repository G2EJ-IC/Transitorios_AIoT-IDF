/*
 * File: main/main_AIoT.c
 * Description: Entry point of the SENSOR NODE of Transitorios AIoT.
 *              Initializes NVS, starts the UART link test with the CrowPanel
 *              (Enlace_AIoT), starts the acquisition engine
 *              (Adquisicion_AIoT) and prints a summary every few seconds.
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, C11. English comments.
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_mac.h"
#include "esp_log.h"

#include "Sensor_Defines_AIoT.h"
#include "Frame_AIoT.h"
#include "Adquisicion_AIoT.h"
#include "Enlace_AIoT.h"

static const char *TAG = "main";

/* NVS is required by the WiFi driver (ESP-NOW runs on top of it) */
static void nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

/* Status bits as text, for the console */
static void status_to_text(uint16_t st, char *buf, size_t len)
{
    if (st == 0) {
        snprintf(buf, len, "OK");
        return;
    }
    snprintf(buf, len, "%s%s%s%s%s%s%s",
                (st & AIOT_STATUS_P1_FAULT)    ? "P1 " : "",
                (st & AIOT_STATUS_P2_FAULT)    ? "P2 " : "",
                (st & AIOT_STATUS_IMU_FAULT)   ? "IMU " : "",
                (st & AIOT_STATUS_FLOW_FAULT)  ? "FLOW " : "",
                (st & AIOT_STATUS_ADC_OVERRUN) ? "ADC_OVR " : "",
                (st & AIOT_STATUS_TIMING_LATE) ? "LATE " : "",
                (st & AIOT_STATUS_TX_DROPPED)  ? "TX_DROP " : "");
}

void app_main(void)
{
    nvs_init();

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "Transitorios AIoT - sensor node %d - MAC " MACSTR, AIOT_NODE_ID, MAC2STR(mac));

#if AIOT_LINK_ENABLED
    /* UART link test: "Hola soy un ESP32-S3" -> CrowPanel, answer printed here */
    const Enlace_AIoT_config_t link_cfg = {
        .role      = ENLACE_AIOT_INITIATOR,
        .uart_num  = AIOT_LINK_UART_NUM,
        .tx_pin    = AIOT_LINK_TX_PIN,
        .rx_pin    = AIOT_LINK_RX_PIN,
        .baud_rate = AIOT_LINK_BAUD,
        .greeting  = AIOT_LINK_GREETING,
        .period_ms = AIOT_LINK_PERIOD_MS,
        .task_core = 0,
    };
    esp_err_t link_err = Enlace_AIoT_Start(&link_cfg);
    if (link_err != ESP_OK) {
        ESP_LOGE(TAG, "UART link not started: %s", esp_err_to_name(link_err));
    }
#endif

    esp_err_t err = Adquisicion_AIoT_Init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Acquisition not started: %s", esp_err_to_name(err));
        return;
    }

    /* Console summary (app_main runs on core 0, low priority) */
    Adquisicion_AIoT_stats_t st;
    char status_txt[64];
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(AIOT_STATS_PERIOD_MS));
        Adquisicion_AIoT_get_stats(&st);
        status_to_text(st.last_status, status_txt, sizeof(status_txt));

        ESP_LOGI(TAG, "frames %lu sent %lu fail %lu drop %lu | late %lu imu_err %lu adc_ovr %lu | %s",
                 (unsigned long)st.frames_built, (unsigned long)st.frames_sent,
                 (unsigned long)st.frames_failed, (unsigned long)st.frames_dropped,
                 (unsigned long)st.missed_ticks, (unsigned long)st.imu_errors,
                 (unsigned long)st.adc_overruns, status_txt);
        ESP_LOGI(TAG, "P1 %.2f kPa  P2 %.2f kPa | Q %.4f L/s  V %.2f L | "
                      "a %.2f %.2f %.2f m/s2 | w %.3f %.3f %.3f rad/s | %.1f C",
                 (double)st.p_kpa[0], (double)st.p_kpa[1],
                 (double)st.flow_lps, (double)st.volume_l,
                 (double)st.acc[0], (double)st.acc[1], (double)st.acc[2],
                 (double)st.gyr[0], (double)st.gyr[1], (double)st.gyr[2],
                 (double)st.temp_c);
    }
}
