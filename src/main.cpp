#include <RadioLib.h>
#include <Adafruit_GFX.h>
#include "HT_SSD1306Wire.h"
#include "DisplayManager.h"
#include "freertos/queue.h"
#include "SX1262Config.h"
#include "RadioManager.h"
#include "PingPongRouter.h"
// #include <string>

// TODO: uncomment the following only on one
// of the nodes to initiate the pings
#define INITIATING_NODE

// Initialize DisplayManager with appropriate I2C pins and address
DisplayManager displayManager;

// Create LoRaManager instance with the display reference
// LoRaManager lora(displayManager);

// QueueHandle_t radioRxQueue;

// Pin mode Controls

/// @brief Turn Vext ON
/// @param  void
void VextON(void)
{
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW);
  Serial.println("VextON");
}

/// @brief  Turn Vext OFF
/// @param   void
void VextOFF(void) // Vext default OFF
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

void setup()
{
  Serial.begin(115200);

  // setup queues
  // setupQueues();

  VextON();
  delay(100);
  // Initialise the display (need to check the display code)
  displayManager.initialise();
  delay(100);

  // initialize SX1262 LoRa module
  // lora.begin();

  if (!radioManager.begin())
  {
    Serial.println("Radio Manager initialization failed!");
    while (1)
    {
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
  }

  // Start the ping-pong router task.
  if (!pingPongRouter.begin())
  {
    Serial.println("PingPong Router initialization failed!");
    while (1)
    {
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
  }

#if defined(INITIATING_NODE)
  // send the first packet on this node
  const char initMsg[] = "PING";
  radioManager.enqueueTxPacket((const uint8_t *)initMsg, sizeof(initMsg) - 1);
  // lora.transmitSYNC1();
#else
  // start listening for LoRa packets on this node
  // lora.startReceive();
  Serial.println("Listening");
#endif
}

void loop()
{
  // lora.update();
}