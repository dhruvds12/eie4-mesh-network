#include "LoRaManager.h"

// Definition of the static variable
volatile bool LoRaManager::operationDone = false;

LoRaManager::LoRaManager(DisplayManager &dispManager) 
: display(dispManager), transmitFlag(false), radio(new Module(8, 14, 12, 13))
{
}

void LoRaManager::begin(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t syncWord, int8_t power, uint16_t preambleLength, float tcxoVoltage, bool useRegulatorLDO)
{
    Serial.print(F("[SX1262] Initializing ... "));
    display.displayNewString(0, 0, "[SX1262] Initializing ... ");
    int state = radio.begin(868.0F, 125.0F, 9u, 7u, 18u, 10, 8u);
    if (state == RADIOLIB_ERR_NONE)
    {
        Serial.println(F("success!"));
        display.displayNewString(0, 0, "success!");
    }
    else
    {
        Serial.print(F("failed, code "));
        Serial.println(state);
        std::string error = "failed, code " + std::to_string(state);
        display.displayNewString(0, 0, error.c_str());
        while (true)
        {
            delay(10);
        }
    }

    // set the function that will be called
  // when new packet is received
  radio.setDio1Action(setFlag);
}

#if defined(ESP8266) || defined(ESP32)
ICACHE_RAM_ATTR
#endif
void LoRaManager::setFlag(void) {
    LoRaManager::operationDone = true;
}

// Function to start data transmission
void LoRaManager::initialTransmission(String data)
{
    Serial.print(F("[SX1262] Sending first packet ... "));
    transmissionState = radio.startTransmit(data);
    transmitFlag = true;
    display.displayString(0, 0, "Sending first packet ... ");
}

// Function to start receiving
void LoRaManager::startReceive()
{
      // start listening for LoRa packets on this node
  Serial.print(F("[SX1262] Listening ... "));

  int state = radio.startReceive();
  if (state == RADIOLIB_ERR_NONE)
  {
    Serial.println(F("success!"));
    display.displayNewString(0, 0, "Listening ... ");
  }
  else
  {
    Serial.print(F("failed, code "));
    Serial.println(state);
    std::string error = "failed, code " + std::to_string(state);
    display.displayNewString(0, 0, error.c_str());
    while (true)
    {
      delay(10);
    }
  }
}

// Function to read received data
int LoRaManager::readReceivedData(String &receivedData)
{
    return radio.readData(receivedData);
}

void LoRaManager::update(){
      unsigned long oneWayLatency = 0;
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
        // Serial.println(F("transmission finished!"));
        display.displayString(0, 40, "transmission finished!");
      }
      else
      {
        Serial.print(F("failed, code "));
        Serial.println(transmissionState);
        std::string error = "failed, code " + std::to_string(transmissionState);
        display.displayString(0, 0, error.c_str());
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
      String receivedData;
      int state = radio.readData(receivedData);
      display.oled_display.clear();

      if (state == RADIOLIB_ERR_NONE)
      {
        int delimiterIndex = receivedData.indexOf(',');
        if (delimiterIndex != -1)
        {
          String msg = receivedData.substring(0, delimiterIndex);
          String timeStr = receivedData.substring(delimiterIndex + 1);
          unsigned long sentTime = timeStr.toInt();
          unsigned long receiveTime = millis();
          Serial.print("Sent time: ");
          Serial.println(sentTime);
          Serial.print("Receive time: ");
          Serial.println(receiveTime);
          oneWayLatency = receiveTime - sentTime; // Use or display oneWayLatency
        }
        packetsReceived++;
        // packet was successfully received
        // Serial.println(F("[SX1262] Received packet!"));
        std::string received = "Received packet no: " + std::to_string(packetsReceived);
        display.oled_display.drawString(0, 0, received.c_str());

        // print data of the packet
        // Serial.print(F("[SX1262] Data:\t\t"));
        // Serial.println(str);
        display.oled_display.drawString(0, 10, receivedData.c_str());

        // print RSSI (Received Signal Strength Indicator)
        // Serial.print(F("[SX1262] RSSI:\t\t"));
        // Serial.print(radio.getRSSI());
        // Serial.println(F(" dBm"));
        std::string rssi = "RSSI: " + std::to_string(radio.getRSSI()) + " dBm";

        display.oled_display.drawString(0, 20, rssi.c_str());

        // print SNR (Signal-to-Noise Ratio)
        // Serial.print(F("[SX1262] SNR:\t\t"));
        // Serial.print(radio.getSNR());
        // Serial.println(F(" dB"));
        std::string snr = "SNR: " + std::to_string(radio.getSNR()) + " dB";
        display.oled_display.drawString(0, 30, snr.c_str());



        // data
        // After each reception (or after each cycle):
        Serial.print(millis()); // Log local time
        Serial.print(",");
        Serial.print(packetsSent);
        Serial.print(",");
        Serial.print(packetsReceived);
        Serial.print(",");
        Serial.print(radio.getRSSI());
        Serial.print(",");
        Serial.print(radio.getSNR());
        Serial.print(",");
        Serial.println(oneWayLatency);
      }
      display.oled_display.display();

      // wait a second before transmitting again
      delay(1000);

      if (millis() - lastTransmissionTime >= transmissionInterval)
      {
        // Serial.print(F("[SX1262] Sending another packet ... "));
        unsigned long sendTime = millis();
        String payload = String("Hello,") + String(sendTime); // Add timestamp to the payload
        transmissionState = radio.startTransmit(payload);
        transmitFlag = true;

        packetsSent++;

        // Update the last transmission time
        lastTransmissionTime = millis();
      }
    }
  }
}