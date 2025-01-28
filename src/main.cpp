#include <RadioLib.h>
#include <Adafruit_GFX.h>
#include "HT_SSD1306Wire.h"
#include "DisplayManager.h"
#include "LoRaManager.h"
// #include <string>

// TODO: uncomment the following only on one
// of the nodes to initiate the pings
// #define INITIATING_NODE

// Initialize DisplayManager with appropriate I2C pins and address
DisplayManager displayManager;

// Create LoRaManager instance with the display reference
LoRaManager lora(displayManager);

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

void setup()
{
  Serial.begin(115200);

  VextON();
  delay(100);
  displayManager.initialise();
  delay(100);

  // initialize SX1262 LoRa module
  lora.begin();

#if defined(INITIATING_NODE)
  // send the first packet on this node

  lora.transmitSYNC1();
#else
  // start listening for LoRa packets on this node
  lora.startReceive();
#endif
}

void loop()
{
  lora.update();
}