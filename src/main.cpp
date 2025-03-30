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
#include <NimBLEDevice.h>
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
#ifdef BLUETOOTH
// NimBLE server callbacks for connection events
class MyServerCallbacks : public NimBLEServerCallbacks
{
  void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) override
  {
    Serial.println("Device connected");
    Serial.printf("Latency : %x", connInfo.getConnLatency());
    Serial.println();
  }
  void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason) override
  {
    Serial.println("Device disconnected");
    Serial.printf("Reason: %x", reason);
    Serial.println();
    // Restart advertising to allow reconnection
    NimBLEDevice::startAdvertising();
  }
};
#endif

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
  // Initialize NimBLE with a device name and explicitly set the device name
  NimBLEDevice::init("HeltecNode");
  NimBLEDevice::setDeviceName("HeltecNode");

  // Create a BLE server and set connection callbacks
  NimBLEServer *pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create and start the BLE service
  NimBLEService *pService = pServer->createService(SERVICE_UUID);
  pService->start();

  // Get the advertising object, enable scan response data, and start advertising
  NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData scanResponse;
  scanResponse.setName("HeltecNode");
  pAdvertising->setScanResponseData(scanResponse);

  pAdvertising->start();

  Serial.println("NimBLE advertising started...");
#endif

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
  const char initMsg[] = "PING";
  radioManager.enqueueTxPacket((const uint8_t *)initMsg, sizeof(initMsg) - 1);
#else
  Serial.println("Listening");
#endif
}

// --- Main Loop ---
void loop()
{
#ifdef BLUETOOTH
  delay(1000);
#endif
}
