/*
 * File: components/Enlace_AIoT/src/Enlace_AIoT.c
 * Description: UART "hello" link test between the sensor node and the
 *              CrowPanel (see Enlace_AIoT.h). Uses UARTn_AIoT.
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, C11. English comments.
 */
#include "Enlace_AIoT.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "UARTn_AIoT.h"

static const char *TAG = "Enlace";

#define ENLACE_LINE_MAX        128     /* longest line accepted */
#define ENLACE_READ_TIMEOUT_MS 1000    /* wait for one line */
#define ENLACE_TASK_STACK      4096
#define ENLACE_TASK_PRIORITY   3       /* low: below WiFi, LVGL and acquisition */

static Enlace_AIoT_config_t s_cfg;
static UARTn_handle_t       s_uart = NULL;
static bool                 s_started = false;

/* Removes a trailing '\r' (lines sent as "...\r\n") */
static void strip_cr(char *line)
{
    size_t n = strlen(line);
    if (n > 0 && line[n - 1] == '\r') {
        line[n - 1] = '\0';
    }
}

/* Sends the greeting of this board followed by '\n' */
static void send_greeting(void)
{
    UARTn_write(s_uart, s_cfg.greeting);
    UARTn_write(s_uart, "\n");
    ESP_LOGI(TAG, "TX -> %s", s_cfg.greeting);
}

/* INITIATOR: greeting every period_ms, then wait for the answer */
static void initiator_loop(void)
{
    char line[ENLACE_LINE_MAX];
    uint32_t misses = 0;

    for (;;) {
        send_greeting();
        int n = UARTn_read_until(s_uart, '\n', line, sizeof(line));
        if (n > 0) {
            strip_cr(line);
            ESP_LOGI(TAG, "RX <- %s", line);
            misses = 0;
        } else {
            misses++;
            if (misses == 1 || (misses % 10) == 0) {
                ESP_LOGW(TAG, "No answer (%lu). Check: TX->RX crossed, GND joined, "
                              "same baud rate, other board running",
                         (unsigned long)misses);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(s_cfg.period_ms));
    }
}

/* RESPONDER: answer every line received */
static void responder_loop(void)
{
    char line[ENLACE_LINE_MAX];

    for (;;) {
        int n = UARTn_read_until(s_uart, '\n', line, sizeof(line));
        if (n > 0) {
            strip_cr(line);
            ESP_LOGI(TAG, "RX <- %s", line);
            send_greeting();
        } else if (n < 0) {
            vTaskDelay(pdMS_TO_TICKS(100));  /* driver error: do not spin */
        }
    }
}

static void enlace_task(void *arg)
{
    (void)arg;
    if (s_cfg.role == ENLACE_AIOT_INITIATOR) {
        initiator_loop();
    } else {
        responder_loop();
    }
}

esp_err_t Enlace_AIoT_Start(const Enlace_AIoT_config_t *config)
{
    if (config == NULL || config->greeting == NULL || config->baud_rate <= 0 ||
        config->uart_num < 1 || config->uart_num > 2 ||
        config->task_core < 0 || config->task_core > 1) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    s_cfg = *config;
    if (s_cfg.period_ms < 100) {
        s_cfg.period_ms = 100;
    }

    s_uart = UARTn_create(s_cfg.uart_num, s_cfg.tx_pin, s_cfg.rx_pin, s_cfg.baud_rate);
    if (s_uart == NULL) {
        return ESP_ERR_NO_MEM;
    }
    UARTn_set_timeout(s_uart, ENLACE_READ_TIMEOUT_MS);
    esp_err_t err = UARTn_init(s_uart);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART%d init failed: %s", s_cfg.uart_num, esp_err_to_name(err));
        UARTn_destroy(s_uart);
        s_uart = NULL;
        return err;
    }

    if (xTaskCreatePinnedToCore(enlace_task, "enlace", ENLACE_TASK_STACK, NULL,
                                ENLACE_TASK_PRIORITY, NULL, s_cfg.task_core) != pdPASS) {
        UARTn_destroy(s_uart);
        s_uart = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    ESP_LOGI(TAG, "%s on UART%d (TX %d, RX %d, %d baud)",
             (s_cfg.role == ENLACE_AIOT_INITIATOR) ? "Initiator" : "Responder",
             s_cfg.uart_num, s_cfg.tx_pin, s_cfg.rx_pin, s_cfg.baud_rate);
    return ESP_OK;
}
