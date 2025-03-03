#ifndef LORAMANAGER_H
#define LORAMANAGER_H

#include <Arduino.h>
#include <RadioLib.h>
#include "DisplayManager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ILoRaRadio.h"

class LoRaManager {
public:
    LoRaManager(DisplayManager &dispManager, ILoRaRadio *radioInterface);

    void begin(float freq = 434.0F, float bw = 125.0F, uint8_t sf = 9U, uint8_t cr = 7U,
               uint8_t syncWord = 18U, int8_t power = 10, uint16_t preambleLength = 8U,
               float tcxoVoltage = 1.6F, bool useRegulatorLDO = false);

    // Other methods...
    void initialTransmission(String data);
    void startReceive();
    int readReceivedData(String &receivedData);
    void update();
    void transmitSYNC1();
    void transmitSYNC2();
    void receiveSYNC1(String timeStr);
    void receiveSYNC2(String timeStr);
    void sendHello();

    struct RadioPacket {
        String payload;
        int rssi;
        int snr;
    };

    static SemaphoreHandle_t radioSemaphore;

private:
    // Remove direct SX1262 radio instantiation
    // Instead, hold a pointer to the configuration module
    ILoRaRadio *radio;
    DisplayManager &display;

    // ISR handler
    static void recievedPacketISR();

    // State variables...
    int transmissionState = RADIOLIB_ERR_NONE;
    int packetsSent       = 0;
    int packetsReceived   = 0;
    unsigned long lastTransmissionTime = 0;
    const unsigned long transmissionInterval = 1000;
    bool transmitFlag     = false;
    static volatile bool operationDone;
    unsigned long timeSYNC1Sent = 0;
    unsigned long timeSYNC2Sent = 0;
    unsigned long timeSYNC1Received = 0;
    unsigned long timeSYNC2Received = 0;
    unsigned long timeOffset = 0;
    unsigned long rtt = 0;
    unsigned long twoWayLatency = 0;
};

#endif
