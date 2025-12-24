/*
 * File: WiFi_AIoT.c
 * Description: WiFi Driver with Hostname configuration.
 * Author: Ing. Ernesto José Guerrero González
 * Standards: English comments for International Code Compliance.
 */

#include "WiFi_AIoT.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h> 
#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "lwip/ip_addr.h"
#include "esp_netif.h"
#include "esp_timer.h" 
#include "esp_pm.h" // Power Management

static const char *TAG = "Wifi_AIoT";

// -------------------------------------------------------------------------
// State Variables
// -------------------------------------------------------------------------
static char current_ssid[33] = "";
static char current_ip[16]   = "0.0.0.0";
static char current_dns[16]  = "0.0.0.0"; 
static char current_mac[18]  = "00:00:00:00:00:00";
static bool is_connected     = false;
static int64_t connection_start_time = 0;

// Power Management Lock Handle (Safe guard)
#if CONFIG_PM_ENABLE
static esp_pm_lock_handle_t cpu_freq_lock;
#endif

// -------------------------------------------------------------------------
// Event Handler
// -------------------------------------------------------------------------
static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        sprintf(current_ip, IPSTR, IP2STR(&event->ip_info.ip));
        
        esp_netif_t *netif = event->esp_netif;
        esp_netif_dns_info_t dns_info;
        if (esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns_info) == ESP_OK) {
            sprintf(current_dns, IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
        }

        uint8_t mac[6];
        esp_wifi_get_mac(WIFI_IF_STA, mac);
        sprintf(current_mac, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        
        connection_start_time = esp_timer_get_time();
        is_connected = true;
        ESP_LOGI(TAG, "CONNECTED! IP:%s - Hostname: Transitorios-AIoT-v00", current_ip);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        is_connected = false;
        connection_start_time = 0;
        ESP_LOGW(TAG, "Disconnected. Retrying...");
        esp_wifi_connect();
    }
}

// -------------------------------------------------------------------------
// Initialization Function
// -------------------------------------------------------------------------
void wifi_init_sta(void)
{
    static bool initialized = false;
    if (initialized) return;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 1. Create Interface and Set Hostname
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    if (netif) {
        // RFC 952 Compliant Hostname
        esp_netif_set_hostname(netif, "Transitorios_AIoT-v00");
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // ---------------------------------------------------------------------
    // STABILITY SECTION (Safe implementation)
    // ---------------------------------------------------------------------
    // Disable WiFi Power Save to reduce latency
    esp_wifi_set_ps(WIFI_PS_NONE); 

    #if CONFIG_PM_ENABLE
    // Only locks CPU if Power Management is enabled in menuconfig
    if (esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "rgb_fix", &cpu_freq_lock) == ESP_OK) {
        esp_pm_lock_acquire(cpu_freq_lock);
        ESP_LOGI(TAG, "CPU Frequency Locked (Stability Mode Active)");
    }
    #else
    ESP_LOGW(TAG, "CPU Lock Skipped (Enable Power Management in menuconfig to fix screen jitter)");
    #endif
    // ---------------------------------------------------------------------

    initialized = true;
}

// -------------------------------------------------------------------------
// Connection & Scan Functions
// -------------------------------------------------------------------------
void wifi_connect(const char *ssid, const char *password)
{
    strncpy(current_ssid, ssid, 32);
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, 32);
    strncpy((char *)wifi_config.sta.password, password, 64);
    
    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_connect();
}

char* wifi_scan_networks_get_list(void)
{
    wifi_scan_config_t scan_config = { .show_hidden = true };
    esp_wifi_scan_start(&scan_config, true);

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count == 0) return NULL;

    wifi_ap_record_t *ap_list = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_count);
    esp_wifi_scan_get_ap_records(&ap_count, ap_list);

    char *list_buffer = (char *)calloc(ap_count * 35, 1);
    if (list_buffer) {
        for (int i = 0; i < ap_count; i++) {
            if (strlen((char *)ap_list[i].ssid) > 0) {
                strcat(list_buffer, (char *)ap_list[i].ssid);
                if (i < ap_count - 1) strcat(list_buffer, "\n");
            }
        }
    }
    free(ap_list);
    return list_buffer;
}

// -------------------------------------------------------------------------
// Getters & Utils
// -------------------------------------------------------------------------
bool  get_wifi_is_connected(void) { return is_connected; }
char* get_wifi_ssid(void) { return current_ssid; }
char* get_wifi_ip(void)   { return current_ip; }
char* get_wifi_dns(void)  { return current_dns; } 
char* get_wifi_mac(void)  { return current_mac; }

int64_t get_wifi_connection_duration_us(void) {
    if (connection_start_time == 0) return 0;
    return esp_timer_get_time() - connection_start_time;
}

void WiFi_Get_Connection_Time_String(char *buffer, size_t len) {
    int64_t duration_us = get_wifi_connection_duration_us();
    if (duration_us == 0) {
        snprintf(buffer, len, "00:00:00"); 
        return;
    }
    int duration_sec = (int)(duration_us / 1000000); 
    int hours   = (duration_sec % 86400) / 3600;
    int minutes = (duration_sec % 3600) / 60;
    int seconds = (duration_sec % 60);
    snprintf(buffer, len, "%02d:%02d:%02d", hours, minutes, seconds);
}