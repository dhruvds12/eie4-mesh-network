#include "SX1262Config.h"

// Constructor: pass pins to Module
SX1262Config::SX1262Config(RADIOLIB_PIN_TYPE cs,
                           RADIOLIB_PIN_TYPE irq,
                           RADIOLIB_PIN_TYPE rst,
                           RADIOLIB_PIN_TYPE gpio)
    : radio(new Module(cs, irq, rst, gpio))
{
}

int SX1262Config::begin(float freq,
                        float bw,
                        uint8_t sf,
                        uint8_t cr,
                        uint8_t syncWord,
                        int8_t power,
                        uint16_t preambleLength,
                        float txcoVoltage,
                        bool useRegulatorLDO)
{
    // Currently ignoring tcxoVoltage / useRegulatorLDO if RadioLib doesn't use them
    // Returns 0 (RADIOLIB_ERR_NONE) on success, or an error code
    return radio.begin(freq, bw, sf, cr, syncWord, power, preambleLength);
}

int SX1262Config::startTransmit(const uint8_t *data, size_t len)
{
    return radio.startTransmit(data, len);
}

void SX1262Config::startReceive()
{
    int16_t status = radio.startReceive();
    if (status != RADIOLIB_ERR_NONE)
    {
        Serial.print("[SX1262Config] Error starting receive: ");
        Serial.println(status);
    }
    assert(status == RADIOLIB_ERR_NONE);

    return;
}

int SX1262Config::readData(String &receivedData, int len)
{
    return radio.readData(receivedData, len);
}

void SX1262Config::setDio1Callback(void (*callback)())
{
    radio.setDio1Action(callback);
}

float SX1262Config::getRSSI()
{
    return radio.getRSSI();
}

float SX1262Config::getSNR()
{
    return radio.getSNR();
}

bool SX1262Config::isChannelFree()
{
    // int err = radio.standby();
    // if (err != RADIOLIB_ERR_NONE)
    // {
    //     Serial.println("Error in standdy");
    // }

    // assert(err == RADIOLIB_ERR_NONE);
    // int result = radio.scanChannel();
    // if (result == RADIOLIB_LORA_DETECTED)
    // {
    //     Serial.println("detected traffic");
    //     return false;
    // }

    // if (result != RADIOLIB_CHANNEL_FREE)
    //     Serial.println("Passed RADIOLIB_LORA_DETECTED");

    // assert(result != RADIOLIB_ERR_WRONG_MODEM);
    // Serial.println("no traffic");

    return true;

    // int err = radio.standby();
    // if (err != RADIOLIB_ERR_NONE)
    // {
    //     Serial.print("Error setting standby: ");
    //     Serial.println(err);
    //     return false;
    // }

    // int state = radio.scanChannel();

    // if (state == RADIOLIB_LORA_DETECTED)
    // {
    //     // LoRa preamble was detected
    //     Serial.println(F("detected!"));
    //     return false;
    // }
    // else if (state == RADIOLIB_CHANNEL_FREE)
    // {
    //     // no preamble was detected, channel is free
    //     Serial.println(F("channel is free!"));
    //     return true;
    // }
    // else
    // {
    //     // some other error occurred
    //     Serial.print(F("failed, code "));
    //     Serial.println(state);
    //     return false;
    // }
}
