#include "wifiManager.h"

int wifiScanNetworks() {
  // Set to STA mode
  WiFi.mode(WIFI_STA);
  // Start scan (synchronous version)
  int n = WiFi.scanNetworks();
  return n;  // n >= 0 means number of networks; negative indicates an error
}

void wifiPrintNetworks() {
  int n = wifiScanNetworks();
  if(n < 0){
    Serial.printf("Error scanning networks, code: %d\n", n);
    return;
  }

  Serial.printf("%d networks found:\n", n);
  for (int i = 0; i < n; i++) {
    Serial.print(i + 1);
    Serial.print(": ");
    Serial.print(WiFi.SSID(i));
    Serial.print(" (");
    Serial.print(WiFi.RSSI(i));
    Serial.println(" dBm)");
  }
}

bool wifiConnect(const char* ssid, const char* password) {
  // Disconnect any previous session and setup station mode.
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  // Begin connection
  Serial.println("Attempting to connect to WiFi...");
  WiFi.begin(ssid, password);

  // Wait for connection with a timeout
  int count = 0;
  while (WiFi.status() != WL_CONNECTED && count < 20) {
    Serial.print(".");
    delay(500);
    count++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println("\nWiFi connection failed!");
    return false;
  }
}
