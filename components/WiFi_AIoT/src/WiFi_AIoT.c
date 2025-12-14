#include "WiFi_AIoT.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h> 
#include <stddef.h> // [CRITICAL FIX] Added for size_t
#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "lwip/ip_addr.h"
#include "esp_netif.h"
#include "esp_timer.h" 

static const char *TAG = "Wifi_AIoT";

// State Variables
static char current_ssid[33] = "";
static char current_ip[16]   = "0.0.0.0";
static char current_dns[16]  = "0.0.0.0"; 
static char current_mac[18]  = "00:00:00:00:00:00";
static bool is_connected = false;

// Time Tracking Variable
static int64_t connection_start_time = 0;

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        
        // 1. Get IP
        sprintf(current_ip, IPSTR, IP2STR(&event->ip_info.ip));
        
        // 2. Get DNS
        esp_netif_t *netif = event->esp_netif;
        esp_netif_dns_info_t dns_info;
        if (esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns_info) == ESP_OK) {
            sprintf(current_dns, IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
        }

        // 3. Get MAC
        uint8_t mac[6];
        esp_wifi_get_mac(WIFI_IF_STA, mac);
        sprintf(current_mac, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        
        // 4. START TIMER
        connection_start_time = esp_timer_get_time(); // Time in microseconds (int64_t)
        
        is_connected = true;
        ESP_LOGI(TAG, "CONNECTED! IP:%s Timer Started.", current_ip);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        is_connected = false;
        
        // 5. RESET TIMER (CRITICAL: Must be reset to 0 to signal disconnection)
        connection_start_time = 0;
        
        strcpy(current_ip, "0.0.0.0");
        strcpy(current_dns, "0.0.0.0");
        ESP_LOGW(TAG, "Disconnected... Timer Reset.");
        esp_wifi_connect();
    }
}

void wifi_init_sta(void)
{
    static bool initialized = false;
    if (initialized) return;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    initialized = true;

    // [CRITICAL FIX]: Force initial state to 0 for the clock to start at 00:00:00
    connection_start_time = 0; 
    is_connected = false;
}

void wifi_connect(const char *ssid, const char *password)
{
    strncpy(current_ssid, ssid, 32);
    
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, 32);
    strncpy((char *)wifi_config.sta.password, password, 64);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    
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

// Getters
bool  get_wifi_is_connected(void) { return is_connected; }
char* get_wifi_ssid(void) { return current_ssid; }
char* get_wifi_ip(void)   { return current_ip; }
char* get_wifi_dns(void)  { return current_dns; } 
char* get_wifi_mac(void)  { return current_mac; }

int64_t get_wifi_connection_duration_us(void) {
    if (connection_start_time == 0) {
        return 0;
    }
    return esp_timer_get_time() - connection_start_time;
}

// ----------------------------------------------------------------------------
// --- NEW FUNCTION: String Formatter for UI (FINAL) ---
// ----------------------------------------------------------------------------
void WiFi_Get_Connection_Time_String(char *buffer, size_t len) {
    // 1. Get the raw duration (returns 0 if disconnected or starting)
    int64_t duration_us = get_wifi_connection_duration_us();
    
    // If duration is 0, reset to zero clock (Fixes the "does not reset")
    if (duration_us == 0) {
        snprintf(buffer, len, "00:00:00"); 
        return;
    }

    // 2. Convert to seconds (Identical logic to IO_Get_Uptime)
    int duration_sec = (int)(duration_us / 1000000); 

    // Calculate time components
    int days = duration_sec / 86400;
    int hours = (duration_sec % 86400) / 3600;
    int minutes = (duration_sec % 3600) / 60;
    int seconds = (duration_sec % 60);

    // 3. Format output (Identical logic to IO_Get_Uptime)
    if (days > 0) {
        snprintf(buffer, len, "%dd %02d:%02d:%02d", days, hours, minutes, seconds);
    } else {
        snprintf(buffer, len, "%02d:%02d:%02d", hours, minutes, seconds);
    }
}