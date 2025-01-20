#ifndef LORAMANAGER_H
#define LORAMANAGER_H

#include <Arduino.h>
#include <RadioLib.h>
#include "DisplayManager.h"

class LoRaManager
{
public:
    LoRaManager(DisplayManager &dispManager);

    void begin(float freq = (434.0F), float bw = (125.0F), uint8_t sf = (uint8_t)9U, uint8_t cr = (uint8_t)7U, uint8_t syncWord = (uint8_t)18U, int8_t power = (int8_t)10, uint16_t preambleLength = (uint16_t)8U, float tcxoVoltage = (1.6F), bool useRegulatorLDO = false);

    void initialTransmission(String data);

    void startReceive();

    int readReceivedData(String &receivedData);

    void update();

    // Flags
    // bool transmitFlag;
    // static volatile bool operationDone;

    private:
        SX1262 radio;
        DisplayManager &display;

        // Interrupt handler function (moved inside the class)
        static void setFlag();
        // State variables
        int transmissionState = RADIOLIB_ERR_NONE;
        int packetsSent       = 0;
        int packetsReceived   = 0;
        unsigned long lastTransmissionTime = 0;
        const unsigned long transmissionInterval = 1000; 

        bool transmitFlag     = false; 
        static volatile bool operationDone;
};

#endif