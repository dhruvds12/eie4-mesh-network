#ifndef RADIOMANAGER_H
#define RADIOMANAGER_H

#include "IRadioManager.h"
#include <Arduino.h>
#include <FreeRTOS.h>
#include <semphr.h>
#include "ILoRaRadio.h"

// struct RadioPacket
// {
//     uint8_t data[256];
//     size_t len;
// };

/**
 * @brief RadioManager class
 *
 * Provides a FreeRTOS-based concurrency layer on top of ILoRaRadio.
 * Handles radio interrupts, sets up receiving, and can do basic transmissions.
 */
class RadioManager : public IRadioManager
{
public:
    /**
     * @brief Construct a new RadioManager
     * @param radio An ILoRaRadio (e.g. SX1262Config*) that is already allocated
     */
    RadioManager(ILoRaRadio *radio);

    /**
     * @brief Initialize the radio and create tasks/resources.
     * @return true if initialization succeeded
     */
    bool begin();

    bool enqueueTxPacket(const uint8_t *data, size_t len);

    bool dequeueRxPacket(RadioPacket **packet);

    QueueHandle_t getRxQueue() const { return _rxQueue; }

    QueueHandle_t getTxQueue() const { return _txQueue; }

    /**
     * @brief Enqueue or directly perform a transmit.
     * For a minimal example, we do a simple synchronous transmit.
     */
    bool sendPacket(const uint8_t *data, size_t len);

private:
    // Reference to the actual LoRa driver
    ILoRaRadio *_radio;

    // Radio IRQ semaphore
    static SemaphoreHandle_t _irqSemaphore;

    // Task handle for the radio task
    TaskHandle_t _radioTaskHandle;

    // Task handle for the transmit task
    TaskHandle_t _txTaskHandle;

    // Queue for received packets
    QueueHandle_t _rxQueue;

    // Queue for transmit packets
    QueueHandle_t _txQueue;

    // Flag to indicate mode
    volatile bool _isTransmitting;

    // Static ISR callback for DIO1
    static void dio1Isr();

    // The main radio task function
    static void radioTask(void *pvParameters);

    // The transmit task function
    static void txTask(void *pvParameters);

    // Helper for transmit task
    void processTxPacket();

    // Helper method to handle a receive interrupt
    void handleReceiveInterrupt();

    // Helper method ot handle the completion of a transmission
    void handleTransmissionComplete();
};

#endif // RADIOMANAGER_H
