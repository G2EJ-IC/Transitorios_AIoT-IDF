//Función Internet.
#include "token.h"

//----------- Parámetros Wi-Fi ---------//
// extern WiFiCredentials wifiNetworks[];
WiFiCredentials wifiNetworks[] = {
    {"SSID1 (red WiFi1)", "Contraseña1 (red WiFi1)"},          // SSID1 (red WiFi1), Contraseña1 (red WiFi1).
    {"SSID1 (red WiFi2)", "Contraseña1 (red WiFi2)"},       // SSID2 (red WiFi2), Contraseña2 (red WiFi2).
    {"SSID1 (red WiFi3)", "Contraseña1 (red WiFi3)"},            // SSID3 (red WiFi3 - Celular1), Contraseña3 (red WiFi3 - Celular1).
    {"SSID1 (red WiFi4)", "Contraseña1 (red WiFi4)"},  // SSID4 (red WiFi4 - Celular2), Contraseña4 (red WiFi4 - Celular2).
	{"SSID1 (red WiFi5)", "Contraseña1 (red WiFi5)"},			// SSID4 (red WiFi4 - Celular3), Contraseña5 (red WiFi5 - Celular3).
};

int numNetworks = sizeof(wifiNetworks) / sizeof(WiFiCredentials);

