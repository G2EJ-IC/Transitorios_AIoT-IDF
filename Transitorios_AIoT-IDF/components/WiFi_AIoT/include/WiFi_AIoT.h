#pragma once
#include <stdbool.h>
#include <stdint.h> 
#include <stddef.h> // Ensure size_t is here

#ifdef __cplusplus
extern "C" {
#endif

// Initialization
void wifi_init_sta(void);
char* wifi_scan_networks_get_list(void);
void wifi_connect(const char *ssid, const char *password);

// Getters
bool get_wifi_is_connected(void);
char* get_wifi_ssid(void);
char* get_wifi_ip(void);
char* get_wifi_dns(void);
char* get_wifi_mac(void);

/**
 * @brief Returns the connection duration in microseconds.
 */
int64_t get_wifi_connection_duration_us(void);

/**
 * @brief Gets the WiFi connection time string (Day HH:MM:SS).
 * Useful for UI labels that need to update every second.
 * @param buffer Output buffer
 * @param len Buffer length
 */
void WiFi_Get_Connection_Time_String(char *buffer, size_t len);

#ifdef __cplusplus
}
#endif