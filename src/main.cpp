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
#include "gatewayManager.h"

// TODO can remove thse imports after testing complete:

#include <esp_wifi.h>

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
MQTTManager *mqttManager = nullptr;
BluetoothManager *btManager = nullptr;
AODVRouter *aodvRouter = nullptr;
NetworkMessageHandler *networkMessageHandler = nullptr;
UserSessionManager userSessionManager(mqttManager);
GatewayManager *gwMgr = nullptr;

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
  delay(100);

  Serial.begin(115200);
  Serial.print("ESP-IDF Version: ");
  Serial.println(esp_get_idf_version());
  Serial.begin(115200);
  VextON();
  delay(100);

  Serial.printf("NODE ID: %lu\n", (unsigned long)getNodeID());

  btManager = new BluetoothManager(&userSessionManager, nullptr, getNodeID());
  aodvRouter = new AODVRouter(&radioManager, mqttManager, getNodeID(), &userSessionManager, btManager);
  networkMessageHandler = new NetworkMessageHandler(aodvRouter);

  btManager->setNetworkMessageHandler(networkMessageHandler);
  // Initialize BLE using our abstraction.
  btManager->init("HeltecNode2");
  btManager->startAdvertising();
  Serial.println("NimBLE advertising started...");

  gwMgr = new GatewayManager("http://132.145.67.221:8443/v1",
                             getNodeID(),
                             networkMessageHandler,
                             &userSessionManager,
                             btManager,
                             aodvRouter);
  gwMgr->begin();

  // aodvRouter->getKnownUserIDs();

  // Initialise display if needed
  displayManager.initialise();
  delay(100);
  displayManager.post("Started Correctly, HI");

  // First, scan and print available WiFi networks.
  Serial.println("Scanning for available WiFi networks...");
  wifiPrintNetworks();
  delay(500);

  wifi_ps_type_t cur;
  esp_wifi_get_ps(&cur);
  Serial.printf("Current WiFi PS mode: %d\n", (int)cur);
  // esp_wifi_set_ps(WIFI_PS_NONE);   // drop to full-power STA

  // Then attempt to connect to the network using the credentials from wifiConfig.h.
  bool wifiConnected = wifiConnect(ssid, password);
  if (wifiConnected)
    gwMgr->onWifiUp();

  WiFi.onEvent([](WiFiEvent_t e)
               {
      if (!gwMgr) return;
      if (e==ARDUINO_EVENT_WIFI_STA_CONNECTED ||
          e==ARDUINO_EVENT_WIFI_STA_GOT_IP)
          gwMgr->onWifiUp();
      else if (e==ARDUINO_EVENT_WIFI_STA_DISCONNECTED)
          gwMgr->onWifiDown(); });

  // Create the MQTTManager instance with the dynamically built subscribe topic.
  mqttManager = new MQTTManager("mqtt://132.145.67.221:1883", getNodeID(), &radioManager, networkMessageHandler);

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
    aodvRouter->setMQTTManager(mqttManager);
    userSessionManager.setMQTTManager(mqttManager);
  }

  if (!radioManager.begin())
  {
    Serial.println("Radio Manager initialization failed!");
    while (1)
    {
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
  }

  // Start the AODV router
  if (!aodvRouter->begin())
  {
    Serial.println("AODV Router initialization failed!");
    while (1)
    {
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
  }

  // #if defined(INITIATING_NODE)
  //   const char initMsg[] = "PING";
  //   radioManager.enqueueTxPacket((const uint8_t *)initMsg, sizeof(initMsg) - 1);
  // #else
  //   Serial.println("Listening");
  // #endif
}

// --- Main Loop ---
void loop()
{

// delay(10000);
// // Example: print the number of connected clients.
// uint8_t n = btManager->getServer()->getConnectedCount();
// Serial.printf("Connected count: %u\n", n);

// if (n)
// {
//   std::string message = "bye";
//   bool ok = btManager->sendBroadcast(message);
//   Serial.printf("notify(): %s\n", ok ? "ok" : "failed");
// }

// static unsigned long lastPublishTime = 0;
// if (millis() - lastPublishTime > 5000)
// {
//   lastPublishTime = millis();
//   mqttManager->publishMessage("hardware/to/simulation", "Hardware Status: Idle");
//   Serial.println("Published hardware status");
// }
// delay(100);
// #ifdef INITIATING_NODE   // only the STA node that has Wi-Fi
//   gwMgr->broadcastUtc(); // now fetches + sends in one call
// #endif
//   delay(5000);
}
