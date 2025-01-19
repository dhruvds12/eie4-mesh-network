#include <RadioLib.h>
#include <Adafruit_GFX.h>
#include "HT_SSD1306Wire.h"
// #include <string>

// TODO: uncomment the following only on one
// of the nodes to initiate the pings
#define INITIATING_NODE

// SX1262 has the following connections:
// NSS pin:   8
// DIO1 pin:  14
// NRST pin:  12
// BUSY pin:  13
SX1262 radio = new Module(8, 14, 12, 13);

// #define RADIO_BOARD_AUTO
// #include <RadioBoards.h>
// Radio radio = new RadioModule();

// save transmission states between loops
int transmissionState = RADIOLIB_ERR_NONE;

// flag to indicate transmission or reception state
bool transmitFlag = false;

// flag to indicate that a packet was sent or received
volatile bool operationDone = false;

// this function is called when a complete packet
// is transmitted or received by the module
// IMPORTANT: this function MUST be 'void' type
//            and MUST NOT have any arguments!
#if defined(ESP8266) || defined(ESP32)
ICACHE_RAM_ATTR
#endif
void setFlag(void)
{
  // we sent or received a packet, set the flag
  operationDone = true;
}

SSD1306Wire oled_display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED); // addr , freq , i2c group , resolution , rst
int count = 0;
unsigned long lastTransmissionTime = 0;          // Track the last transmission time
const unsigned long transmissionInterval = 1000; // Set timeout (e.g., 1000 ms = 1 seconds)

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

void displayString(int x, int y, String str)
{
  oled_display.drawString(x, y, str);
  oled_display.display();
}

void displayNewString(int x, int y, String str)
{
  oled_display.clear();
  oled_display.display();
  oled_display.drawString(x, y, str);
  oled_display.display();
}

void setup()
{
  Serial.begin(115200);

  VextON();
  delay(100);
  oled_display.init();
  oled_display.clear();
  oled_display.display();
  delay(100);

  // initialize SX1262 with default settings
  Serial.print(F("[SX1262] Initializing ... "));
  displayNewString(0, 0, "[SX1262] Initializing ... ");
  int state = radio.begin(868.0F, 125.0F, 9u, 7u, 18u, 10, 8u);
  if (state == RADIOLIB_ERR_NONE)
  {
    Serial.println(F("success!"));
    displayNewString(0, 0, "success!");
  }
  else
  {
    Serial.print(F("failed, code "));
    Serial.println(state);
    std::string error = "failed, code " + std::to_string(state);
    displayNewString(0, 0, error.c_str());
    while (true)
    {
      delay(10);
    }
  }

  // set the function that will be called
  // when new packet is received
  radio.setDio1Action(setFlag);

#if defined(INITIATING_NODE)
  // send the first packet on this node
  Serial.print(F("[SX1262] Sending first packet ... "));
  transmissionState = radio.startTransmit("Hello World!");
  transmitFlag = true;
  displayString(0, 0, "Sending first packet ... ");
#else
  // start listening for LoRa packets on this node
  Serial.print(F("[SX1262] Listening ... "));

  state = radio.startReceive();
  if (state == RADIOLIB_ERR_NONE)
  {
    Serial.println(F("success!"));
    displayNewString(0, 0, "Listening ... ");
  }
  else
  {
    Serial.print(F("failed, code "));
    Serial.println(state);
    std::string error = "failed, code " + std::to_string(state);
    displayNewString(0, 0, error.c_str());
    while (true)
    {
      delay(10);
    }
  }
#endif
}

void loop()
{
  // factory_display.drawString(0, 0, "Hello, world!");
  // check if the previous operation finished
  if (operationDone)
  {
    // reset flag
    operationDone = false;

    if (transmitFlag)
    {
      // the previous operation was transmission, listen for response
      // print the result
      if (transmissionState == RADIOLIB_ERR_NONE)
      {
        // packet was successfully sent
        Serial.println(F("transmission finished!"));
        displayString(0, 40, "transmission finished!");
      }
      else
      {
        Serial.print(F("failed, code "));
        Serial.println(transmissionState);
        std::string error = "failed, code " + std::to_string(transmissionState);
        displayString(0, 0, error.c_str());
      }

      // listen for response
      radio.startReceive();
      transmitFlag = false;
    }
    else
    {
      // the previous operation was reception
      // print data and send another packet
      // displayString(0, 0, "Waiting for packet ... ");
      String str;
      int state = radio.readData(str);
      oled_display.clear();

      if (state == RADIOLIB_ERR_NONE)
      {
        count++;
        // packet was successfully received
        Serial.println(F("[SX1262] Received packet!"));
        std::string received = "Received packet no: " + std::to_string(count);
        oled_display.drawString(0, 0, received.c_str());

        // print data of the packet
        Serial.print(F("[SX1262] Data:\t\t"));
        Serial.println(str);
        oled_display.drawString(0, 10, str.c_str());

        // print RSSI (Received Signal Strength Indicator)
        Serial.print(F("[SX1262] RSSI:\t\t"));
        Serial.print(radio.getRSSI());
        Serial.println(F(" dBm"));
        std::string rssi = "RSSI: " + std::to_string(radio.getRSSI()) + " dBm";

        oled_display.drawString(0, 20, rssi.c_str());

        // print SNR (Signal-to-Noise Ratio)
        Serial.print(F("[SX1262] SNR:\t\t"));
        Serial.print(radio.getSNR());
        Serial.println(F(" dB"));
        std::string snr = "SNR: " + std::to_string(radio.getSNR()) + " dB";
        oled_display.drawString(0, 30, snr.c_str());
      }
      oled_display.display();

      // wait a second before transmitting again
      delay(1000);

      if (millis() - lastTransmissionTime >= transmissionInterval)
      {
        Serial.print(F("[SX1262] Sending another packet ... "));
        transmissionState = radio.startTransmit("Hello World!");
        transmitFlag = true;

        // Update the last transmission time
        lastTransmissionTime = millis();
      }
    }
  }
}