#ifndef I_LORA_RADIO_H
#define I_LORA_RADIO_H

#include <Arduino.h>


class ILoRaRadio {
    public:
        virtual ~ILoRaRadio() {}

        virtual int begin(float freq,
                          float bw,
                          uint8_t sf,
                          uint8_t cr,
                          uint8_t syncWord,
                          int8_t power,
                          uint16_t preambleLength,
                          float tcxoVoltage,
                          bool useRegulatorLDO) = 0;

      virtual int startTransmit(const uint8_t *data, size_t len) = 0;

      virtual int startReceive() = 0;

      virtual int readData(String &receivedData) = 0;

      virtual void setDio1Callback(void (*callback)()) = 0;

      virtual float getRSSI() = 0;

      virtual float getSNR() = 0;
            
};

#endif