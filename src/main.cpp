#include <WiFi.h>
#include <RadioLib.h>
#include <Adafruit_GFX.h>
#include "HT_SSD1306Wire.h"
#include "DisplayManager.h"
#include "freertos/queue.h"
#include "SX1262Config.h"
#include "RadioManager.h"
#include "PingPongRouter.h"
#include "aodvRouter.h"
#include <esp_system.h>
#include "wifiConfig.h"
#include "wifiManager.h"


// --- Other existing declarations ---
DisplayManager displayManager;
SX1262Config myRadio(8, 14, 12, 13);
RadioManager radioManager(&myRadio);
AODVRouter aodvRouter(&radioManager, []()
                      { 
  uint64_t mac = ESP.getEfuseMac();
  return (uint32_t)(mac & 0xFFFFFFFF); }());

// --- Vext Control Functions ---
void VextON(void)
{
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW);
  Serial.println("VextON");
}

void VextOFF(void)
{
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, HIGH);
}

// --- Main Setup Function ---
void setup()
{
  Serial.begin(115200);
  VextON();
  delay(100);

  // Initialise display if needed
  displayManager.initialise();
  delay(100);

 // First, scan and print available WiFi networks.
 Serial.println("Scanning for available WiFi networks...");
 wifiPrintNetworks();
 delay(500);
 
 // Then attempt to connect to the network using the credentials from wifiConfig.h.
 bool wifiConnected = wifiConnect(ssid, password);
 
 // If WiFi isn't connected, then do not continue with MQTT/HTTP initialization.
 if (!wifiConnected) {
   Serial.println("WiFi connection failed. MQTT services will not be started.");
   // You might choose to retry, or continue with alternative logic.
 } else {
    // add logic for mqtt client setup here.... 
 }

  // Initialize the radio manager
  if (!radioManager.begin())
  {
    Serial.println("Radio Manager initialization failed!");
    while (1)
    {
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
  }

  // Start the AODV router
  if (!aodvRouter.begin())
  {
    Serial.println("AODV Router initialization failed!");
    while (1)
    {
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
  }

#if defined(INITIATING_NODE)
  // Send the first packet on this node
  const char initMsg[] = "PING";
  radioManager.enqueueTxPacket((const uint8_t *)initMsg, sizeof(initMsg) - 1);
#else
  Serial.println("Listening");
#endif
}

// --- Main Loop ---
void loop()
{
  // Insert your LoRa update or router processing code here.
  // You can also perform API requests using WiFi here as needed.
}
