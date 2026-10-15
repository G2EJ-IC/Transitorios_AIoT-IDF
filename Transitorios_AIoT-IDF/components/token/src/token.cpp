//Función Internet.
#include "token.h"

//----------- Parámetros Wi-Fi ---------//
// extern WiFiCredentials wifiNetworks[];
WiFiCredentials wifiNetworks[] = {
    {"GUERRERO_BOLANNO_INV", "11291104BG397"},          // SSID1 (red WiFi1), Contraseña1 (red WiFi1).
    {"GUERRERO_BOLANNO", "(11291104BG397)**397"},       // SSID2 (red WiFi2), Contraseña2 (red WiFi2).
    {"HONOR X7a - G2EJ-IC", "135797531!$&"},            // SSID3 (red WiFi3 - Celular1), Contraseña3 (red WiFi3 - Celular1).
    {"Redmi Note 8 (2001) - G2EJ.IC", "135797531!$&"},  // SSID4 (red WiFi4 - Celular2), Contraseña4 (red WiFi4 - Celular2).
	{"Vivo Y21s - OsBolGu", "135797531!$&"},			// SSID4 (red WiFi4 - Celular3), Contraseña5 (red WiFi5 - Celular3).
};

int numNetworks = sizeof(wifiNetworks) / sizeof(WiFiCredentials);

