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
void LoRaManager::setFlag(void)
{
    LoRaManager::operationDone = true;
}

void LoRaManager::transmitSYNC1()
{
    // Serial.print(F("[SX1262] Sending another packet ... "));
    unsigned long sendTime = millis();
    String payload = String("SYNC1,") + String(sendTime); // Add timestamp to the payload
    transmissionState = radio.startTransmit(payload);
    transmitFlag = true;

    // packetsSent++;

    // Update the last transmission time
    // lastTransmissionTime = millis();
    timeSYNC1Sent = millis();
}

void LoRaManager::transmitSYNC2()
{
    // Serial.print(F("[SX1262] Sending another packet ... "));
    unsigned long sendTime = millis();
    String payload = String("SYNC2,") + String(timeSYNC1Received) + "," + String(sendTime); // Add timestamp to the payload
    transmissionState = radio.startTransmit(payload);
    timeSYNC2Sent = sendTime;
    transmitFlag = true;

    // packetsSent++;

    // Update the last transmission time
    // lastTransmissionTime = millis();
}

void LoRaManager::receiveSYNC1(String timeStr)
{
    transmitSYNC2();
    timeSYNC1Sent = timeStr.toInt();
}

void LoRaManager::receiveSYNC2(String timeStr)
{
    int firstDelimiter = timeStr.indexOf(','); // Find first comma position

    if (firstDelimiter == -1)
    {
        Serial.println("Invalid packet received");
        return;
    }

    String sync1recieve = timeStr.substring(0, firstDelimiter);
    String sync2sentTime = timeStr.substring(firstDelimiter + 1);

    timeSYNC1Received = sync1recieve.toInt();
    timeSYNC2Sent = sync2sentTime.toInt();
    Serial.print("Time SYNC1 sent: ");
    Serial.println(timeSYNC1Sent);
    Serial.print("Time SYNC2 sent: ");
    Serial.println(timeSYNC2Sent);
    Serial.print("Time SYNC1 received: ");
    Serial.println(timeSYNC1Received);
    Serial.print("Time SYNC2 received: ");
    Serial.println(timeSYNC2Received);

    unsigned long processingTime = timeSYNC2Sent - timeSYNC1Received;
    Serial.print("Processing time: ");
    Serial.println(processingTime);

    unsigned long timeDifference1 = abs((long)(timeSYNC1Received - timeSYNC1Sent));
    unsigned long timeDifference2 = abs((long)(timeSYNC2Received - timeSYNC2Sent));
    rtt = timeSYNC2Received - timeSYNC1Sent - processingTime;
    Serial.print("Round trip time: ");
    Serial.println(rtt);
    // timeOffset = (timeDifference1 + timeDifference2 - rtt) / 2;
    timeOffset = timeDifference1 - (rtt / 2);
    Serial.print("Time difference between SYNC2 and SYNC1: ");
    Serial.println(timeOffset);

    sendHello();
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

// Function to send Hello
void LoRaManager::sendHello()
{

    // Serial.print(F("[SX1262] Sending another packet ... "));
    unsigned long sendTime = millis() + timeOffset;
    String payload = String("Hello,") + String(sendTime); // Add timestamp to the payload
    transmissionState = radio.startTransmit(payload);
    transmitFlag = true;

    packetsSent++;

    // Update the last transmission time
    lastTransmissionTime = millis();
}

// Function to read received data
int LoRaManager::readReceivedData(String &receivedData)
{
    return radio.readData(receivedData);
}

void LoRaManager::update()
{
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
            String msg = "unknown";
            String timeStr = "0";
            int state = radio.readData(receivedData);
            display.oled_display.clear();
            unsigned long recieveTime = millis();

            if (state == RADIOLIB_ERR_NONE)
            {
                int delimiterIndex = receivedData.indexOf(',');
                if (delimiterIndex != -1)
                {
                    msg = receivedData.substring(0, delimiterIndex);
                    timeStr = receivedData.substring(delimiterIndex + 1);

                    if (msg == "SYNC1")
                    {
                        timeSYNC1Received = millis();
                        receiveSYNC1(timeStr);
                        display.oled_display.drawString(0, 10, "SYNC received");
                    }
                    else if (msg == "SYNC2")
                    {
                        timeSYNC2Received = millis();
                        receiveSYNC2(timeStr);
                        display.oled_display.drawString(0, 10, "SYNC received");
                    }
                    else if (msg == "Hello")
                    {
                        unsigned long sentTime = timeStr.toInt();
                        unsigned long receiveTime = millis();
                        // Serial.print("Sent time: ");
                        // Serial.println(sentTime);
                        // Serial.print("Receive time: ");
                        // Serial.println(receiveTime);
                        // Serial.print("Time offset: ");
                        // Serial.println(timeOffset);
                        oneWayLatency = receiveTime + timeOffset - sentTime; // Use or display oneWayLatency
                        if (lastTransmissionTime != 0)
                        {
                            twoWayLatency = receiveTime - lastTransmissionTime - 1000;
                        }

                        if (msg == "Hello")
                        {
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
                            Serial.print(oneWayLatency);
                            Serial.print(",");
                            if (lastTransmissionTime != 0)
                            {
                                Serial.println(twoWayLatency);
                            }
                            else
                            {
                                Serial.println("0");
                            }
                        }
                        // wait a second before transmitting again
                        delay(1000);

                        if ((millis() - lastTransmissionTime >= transmissionInterval))
                        {
                            sendHello();
                        }
                    }
                    else
                    {
                        display.oled_display.drawString(0, 0, "Invalid packet received");
                    }
                }
                display.oled_display.display();
            }
        }
    }
}