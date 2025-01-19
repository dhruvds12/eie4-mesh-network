#ifndef LORAMANAGER_H
#define LORAMANAGER_H

#include <Arduino.h>
#include <RadioLib.h>

class LoRaManager {
    public:
        LoRaManager();

        bool begin(float freq = (434.0F), float bw = (125.0F), uint8_t sf = (uint8_t)9U, uint8_t cr = (uint8_t)7U, uint8_t syncWord = (uint8_t)18U, int8_t power = (int8_t)10, uint16_t preambleLength = (uint16_t)8U, float tcxoVoltage = (1.6F), bool useRegulatorLDO = false);

        int startTransmit(String data);

        int startReceive();

        int readReceivedData(String &receivedData);

        // Flags
        bool transmitFlag;
        static volatile bool operationDone;


        SX1262 radio;

};

#endif