#ifndef SX1262_CONFIG_H
#define SX1262_CONFIG_H

#include <Arduino.h>
#include <RadioLib.h>
#include "ILoRaRadio.h"

#define RADIOLIB_PIN_TYPE uint32_t

/**
 * @brief Configuration module for the SX1262 LoRa chip.
 *
 * Encapsulates all SX1262-specific initialization and configuration.
 */
class SX1262Config : public ILoRaRadio
{
public:
  /**
   * @brief Constructor taking a reference to an SX1262 radio instance.
   * @param radio Reference to the SX1262 object.
   */
  SX1262Config(RADIOLIB_PIN_TYPE cs = 8, RADIOLIB_PIN_TYPE irq = 14, RADIOLIB_PIN_TYPE rst = 12, RADIOLIB_PIN_TYPE gpio = 13);

  /**
   * @brief Initializes the SX1262 with the provided parameters.
   * @return int Returns RADIOLIB_ERR_NONE if successful, or an error code.
   */
  int begin(float freq = 868.0F,
            float bw = 125.0F,
            uint8_t sf = 9u,
            uint8_t cr = 7u,
            uint8_t syncWord = 18u,
            int8_t power = 10,
            uint16_t preambleLength = 8u,
            float tcxoVoltage = 1.6F,
            bool useRegulatorLDO = false) override;

  int startTransmit(const uint8_t *data, size_t len) override;
  void startReceive() override;

  /**
   * @brief Provide a pointer to where to store received data and the length of the data.if len is 0, will automatically get packet length
   *
   *
   * @param receivedData
   * @param len
   * @return int
   */
  int readData(String &receivedData, int len = 0) override;
  void setDio1Callback(void (*callback)()) override;
  float getRSSI() override;
  float getSNR() override;
  bool isChannelFree() override;

private:
  SX1262 radio; // Reference to the SX1262 radio instance.
};

#endif // SX1262_CONFIG_H
