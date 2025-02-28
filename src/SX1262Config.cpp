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

int SX1262Config::startTransmit(const uint8_t *data, size_t len) {
    return radio.startTransmit(data, len);
}


int SX1262Config::startReceive()
{
    return radio.startReceive();
}

int SX1262Config::readData(String &receivedData)
{
    return radio.readData(receivedData);
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
