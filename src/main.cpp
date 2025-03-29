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

// TODO: uncomment the following only on one
// of the nodes to initiate the pings
// #define INITIATING_NODE
#define BLUETOOTH

#ifdef BLUETOOTH
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#endif

// Define a custom service UUID (you can use any valid UUID)
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"

// --- Get Node Mac address ---
uint32_t getNodeID()
{
  // Retrieve the 64-bit MAC address from efuse
  uint64_t mac = ESP.getEfuseMac();
  // Use the lower 32 bits as the unique node ID
  return (uint32_t)(mac & 0xFFFFFFFF);
}

// --- Other existing declarations ---
DisplayManager displayManager;
SX1262Config myRadio(8, 14, 12, 13);
RadioManager radioManager(&myRadio);
AODVRouter aodvRouter(&radioManager, getNodeID());

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

// void setupQueues()
// {
//   radioRxQueue = xQueueCreate(10, sizeof(LoRaManager::RadioPacket));
//   if (radioRxQueue == NULL)
//   {
//     Serial.println("Failed to create radioRxQueue");
//   }

// }

// Create a global radio driver instance.
SX1262Config myRadio(8, 14, 12, 13);
// SX1262Config myRadio(10, 2,3, 9);

// Create the radio manager instance.
RadioManager radioManager(&myRadio);

// Create the ping-pong router instance.
PingPongRouter pingPongRouter(&radioManager);

class MyServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer) override
  {
    Serial.println("Device connected");
  }
  void onDisconnect(BLEServer *pServer) override
  {
    Serial.println("Device disconnected");
  }
};

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

 #ifdef BLUETOOTH
 BLEDevice::init("Heltec BLE Test Node");
 
 BLEServer *pServer = BLEDevice::createServer();
 pServer->setCallbacks(new MyServerCallbacks()); // Set our connection callbacks
 
 BLEService *pService = pServer->createService(SERVICE_UUID);
 pService->start();
 
 BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
 pAdvertising->start();
 
 Serial.println("BLE advertising started...");
 
 #endif
 
// Initialize the radio manager
// initialize SX1262 LoRa module
// lora.begin();
#ifndef BLUETOOTH
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
#endif

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
// lora.update();
#ifdef BLUETOOTH
  delay(1000);
#endif
}
