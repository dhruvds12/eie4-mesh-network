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

// --- Function Prototypes ---
void WIFISetUp();
void WIFIScan();

// --- Other existing declarations ---
DisplayManager displayManager;
SX1262Config myRadio(8, 14, 12, 13);
RadioManager radioManager(&myRadio);
AODVRouter aodvRouter(&radioManager, []()
                      { 
  uint64_t mac = ESP.getEfuseMac();
  return (uint32_t)(mac & 0xFFFFFFFF); }());

// --- WiFi Setup Function ---
void WIFISetUp()
{
  // Disconnect from any previously connected WiFi
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoConnect(true);

  // Begin WiFi connection
  WiFi.begin(ssid, password);
  Serial.println("Connecting to WiFi...");

  // Wait for connection with a timeout counter
  int count = 0;
  while (WiFi.status() != WL_CONNECTED && count < 20)
  {
    Serial.print(".");
    delay(500);
    count++;
  }

  // Check if connected
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  }
  else
  {
    Serial.println("\nWiFi connection failed!");
  }
}

// --- WiFi Scan Function ---
void WIFIScan()
{
  WiFi.mode(WIFI_STA);
  Serial.println("Starting WiFi scan...");
  int n = WiFi.scanNetworks();
  if (n == 0)
  {
    Serial.println("No networks found");
  }
  else if (n >= 0)
  {
    Serial.print(n);
    Serial.println(" networks found:");
    for (int i = 0; i < n; i++)
    {
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.println(" dBm)");
    }
  }
  else
  {
    Serial.printf("Error code %i\n", n);
  }
}

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

  // Setup WiFi: Connect and optionally scan networks
  WIFISetUp();
  WiFi.disconnect(); //
  WiFi.mode(WIFI_STA);
  delay(100);
  WIFIScan();

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
