//Función Internet.
#include "token.h"

//----------- Parámetros Wi-Fi ---------//
// extern WiFiCredentials wifiNetworks[];
WiFiCredentials wifiNetworks[] = {
    {"SSID1 (red WiFi1)", "Contraseña1 (red WiFi1)"},     // SSID1 (red WiFi1), Contraseña1 (red WiFi1).
    {"SSID2 (red WiFi2)", "Contraseña2 (red WiFi2)"},     // SSID2 (red WiFi2), Contraseña2 (red WiFi2).
    {"SSID3 (red WiFi3)", "Contraseña3 (red WiFi3)"},     // SSID3 (red WiFi3), Contraseña3 (red WiFi3).
    {"SSID4 (red WiFi4)", "Contraseña4 (red WiFi4)"},     // SSID4 (red WiFi4), Contraseña4 (red WiFi4).
	{"SSID4 (red WiFi4)", "Contraseña5 (red WiFi5)"},	  // SSID4 (red WiFi4), Contraseña5 (red WiFi5).
};

int numNetworks = sizeof(wifiNetworks) / sizeof(WiFiCredentials);