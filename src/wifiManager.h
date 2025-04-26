#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>

// Performs a WiFi scan and returns the number of networks found,
// or a negative value if there is an error.
int wifiScanNetworks();

// Prints the list of networks to Serial.
void wifiPrintNetworks();

// Attempts to connect to the specified network. Returns true if the
// connection was established before the timeout, or false otherwise.
bool wifiConnect(const char* ssid, const char* password);

#endif
