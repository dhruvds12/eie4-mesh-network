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
#include "mqttManager.h"
#include "NetworkMessageHandler.h"
#include "userSessionManager.h"

// Uncomment the following only on one of the nodes to initiate pings
// #define INITIATING_NODE
// #define BLUETOOTH

#include "BluetoothManager.h"

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

DisplayManager displayManager;
SX1262Config myRadio(8, 14, 12, 13);
RadioManager radioManager(&myRadio);
UserSessionManager userSessionManager;
BluetoothManager btManager(&userSessionManager);
MQTTManager *mqttManager = nullptr;
AODVRouter aodvRouter(&radioManager, mqttManager, getNodeID(), &userSessionManager);
NetworkMessageHandler networkMessageHandler(&aodvRouter);

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
  Serial.print("ESP-IDF Version: ");
  Serial.println(esp_get_idf_version());
  Serial.begin(115200);
  VextON();
  delay(100);

  Serial.printf("NODE ID: %lu\n", (unsigned long)getNodeID());

  // Initialise display if needed
  displayManager.initialise();
  delay(100);

  // First, scan and print available WiFi networks.
  Serial.println("Scanning for available WiFi networks...");
  wifiPrintNetworks();
  delay(500);

  // Then attempt to connect to the network using the credentials from wifiConfig.h.
  bool wifiConnected = wifiConnect(ssid, password);

  // Create the MQTTManager instance with the dynamically built subscribe topic.
  mqttManager = new MQTTManager("mqtt://132.145.67.221:1883", getNodeID(), &radioManager, &networkMessageHandler);

  // If WiFi isn't connected, then do not continue with MQTT/HTTP initialization.
  if (!wifiConnected)
  {
    Serial.println("WiFi connection failed. MQTT services will not be started.");
    // You might choose to retry, or continue with alternative logic.
  }
  else
  {
    // add logic for mqtt client setup here....
    mqttManager->begin();
    aodvRouter.setMQTTManager(mqttManager);
  }

  // Initialize BLE using our abstraction.
  btManager.init("HeltecNode");
  btManager.startAdvertising();
  Serial.println("NimBLE advertising started...");

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
  const char initMsg[] = "PING";
  radioManager.enqueueTxPacket((const uint8_t *)initMsg, sizeof(initMsg) - 1);
#else
  Serial.println("Listening");
#endif
}

// --- Main Loop ---
void loop()
{

  delay(1000);
  // Example: print the number of connected clients.
  uint8_t n = btManager.getServer()->getConnectedCount();
  Serial.printf("Connected count: %u\n", n);

  if (n) {
      bool ok = btManager.sendBroadcast("hello");
      Serial.printf("notify(): %s\n", ok ? "ok" : "failed");
  }

  // static unsigned long lastPublishTime = 0;
  // if (millis() - lastPublishTime > 5000)
  // {
  //   lastPublishTime = millis();
  //   mqttManager->publishMessage("hardware/to/simulation", "Hardware Status: Idle");
  //   Serial.println("Published hardware status");
  // }
  // delay(100);
}
