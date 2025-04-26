#include "RadioManager.h"
#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

SemaphoreHandle_t RadioManager::_irqSemaphore = nullptr;

RadioManager::RadioManager(ILoRaRadio *radio)
    : _radio(radio), _radioTaskHandle(nullptr), _txTaskHandle(nullptr), _rxQueue(nullptr), _txQueue(nullptr), _isTransmitting(false)
{
    // If semaphore isn't created yet, create it
    if (_irqSemaphore == nullptr)
    {
        _irqSemaphore = xSemaphoreCreateBinary();
    }
}

bool RadioManager::begin()
{

    int status = _radio->begin(868.0F, 125.0F, 9u, 7u, 18u, 10, 8u, 1.6F, false);
    if (status != 0)
    {
        Serial.print("[RadioManager] Radio begin failed, code: ");
        Serial.println(status);
        return false;
    }

    _radio->setDio1Callback(dio1Isr);

    // Create the queue for received packets
    _rxQueue = xQueueCreate(10, sizeof(RadioPacket *));
    if (_rxQueue == nullptr)
    {
        Serial.println("[RadioManager] Could not create RX queue!");
        return false;
    }

    // Create the queue for transmit packets
    _txQueue = xQueueCreate(10, sizeof(RadioPacket *));
    if (_txQueue == nullptr)
    {
        Serial.println("[RadioManager] Could not create TX queue!");
        return false;
    }

    BaseType_t radioTaskCreated = xTaskCreate(
        radioTask,
        "RadioTask",
        4096,
        this,
        5,
        &_radioTaskHandle);

    if (radioTaskCreated != pdPASS)
    {
        Serial.println("[RadioManager] Could not create radio task!");
        return false;
    }

    BaseType_t txTaskCreated = xTaskCreate(
        txTask,
        "TxTask",
        4096,
        this,
        2,
        &_txTaskHandle);

    if (txTaskCreated != pdPASS)
    {
        Serial.println("[RadioManager] Could not create tx task!");
        return false;
    }

    _radio->startReceive();

    return true;
}

bool RadioManager::enqueueTxPacket(const uint8_t *data, size_t len)
{
    RadioPacket *packet = (RadioPacket *)pvPortMalloc(sizeof(RadioPacket));
    if (packet == nullptr)
    {
        Serial.println("[RadioManager] Could not allocate memory for packet!");
        return false;
    }
    if (len > sizeof(packet->data))
    {
        Serial.println("[RadioManager] Packet too large!");
        vPortFree(packet);
        return false;
    }

    memcpy(packet->data, data, len);
    packet->len = len;

    if (xQueueSend(_txQueue, &packet, 0) != pdPASS)
    {
        Serial.println("[RadioManager] Could not send packet to TX queue!");
        vPortFree(packet);
        return false;
    }

    return true;
}

bool RadioManager::dequeueRxPacket(RadioPacket **packet)
{
    if (xQueueReceive(_rxQueue, packet, portMAX_DELAY) == pdTRUE)
    {
        return true;
    }
    return false;
}

bool RadioManager::enqueueRxPacket(const uint8_t *data, size_t len)
{
    RadioPacket *packet = (RadioPacket *)pvPortMalloc(sizeof(RadioPacket));
    if (packet == nullptr)
    {
        Serial.println("[RadioManager] Could not allocate memory for packet!");
        return false;
    }
    if (len > sizeof(packet->data))
    {
        Serial.println("[RadioManager] Packet too large!");
        vPortFree(packet);
        return false;
    }

    memcpy(packet->data, data, len);
    packet->len = len;
    if (xQueueSend(_rxQueue, &packet, 0) != pdPASS)
    {
        Serial.println("[RadioManager] Could not send packet to TX queue!");
        vPortFree(packet);
        return false;
    }
    return true;
}

bool RadioManager::sendPacket(const uint8_t *data, size_t len)
{

    bool channelFree = _radio->isChannelFree();

    if (channelFree)
    {

        int status = _radio->startTransmit(data, len);
        if (status != 0)
        {
            Serial.print("[RadioManager] TX failed, code: ");
            Serial.println(status);
            return false;
        }
        return true;
    }

    return false;
}

// ----------------- PRIVATE METHODS -----------------

void RadioManager::dio1Isr()
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (_irqSemaphore)
    {
        xSemaphoreGiveFromISR(_irqSemaphore, &xHigherPriorityTaskWoken);
    }

    // If we unblocked a task, yield to it
    if (xHigherPriorityTaskWoken)
    {
        portYIELD_FROM_ISR();
    }
}

void RadioManager::radioTask(void *pvParameters)
{
    RadioManager *manager = reinterpret_cast<RadioManager *>(pvParameters);
    for (;;)
    {
        // Wait until the ISR signals the semaphore
        if (xSemaphoreTake(_irqSemaphore, portMAX_DELAY) == pdTRUE)
        {
            if (manager->_isTransmitting)
            {
                manager->handleTransmissionComplete();
            }
            else
            {
                Serial.println("Received transmission");
                // We got an interrupt, handle it
                manager->handleReceiveInterrupt();
            }
        }
        // As per docs we do not need to return the semaphore
        // https://www.freertos.org/Documentation/02-Kernel/04-API-references/10-Semaphore-and-Mutexes/17-xSemaphoreGiveFromISR
    }
}

void RadioManager::handleReceiveInterrupt()
{
    // readData from the radio
    RadioPacket *packet = (RadioPacket *)pvPortMalloc(sizeof(RadioPacket));
    if (packet == nullptr)
    {
        Serial.print("[RadioManager] Memory Allocation failed in RX");
        _radio->startReceive();
        return;
    }

    size_t packetLength = _radio->getPacketLength();
    uint8_t buffer[255];

    int result = _radio->readData(buffer, packetLength);
    if (result == 0) // radiolib not imported equivalent to RADIOLIB_ERR_NONE
    {
        size_t len = packetLength;
        if (len == 0)
        {
            Serial.print("Ignore empty");
            vPortFree(packet);
            // Likely a false interrupt; just restart receive mode.
            _radio->startReceive();
            return;
        }
        if (len > sizeof(packet->data))
        {
            len = sizeof(packet->data);
        }

        memcpy(packet->data, buffer, len);
        packet->len = len;

        if (xQueueSend(_rxQueue, &packet, 0) != pdPASS)
        {
            Serial.print("[RadioManager] RX queue full, dropping packet");
            vPortFree(packet);
        }
    }
    else
    {
        Serial.print("[RadioManager] readData error: ");
        Serial.println(result);
        vPortFree(packet);
    }

    // Always restart receive
    _radio->startReceive();
}

void RadioManager::handleTransmissionComplete()
{
    Serial.println("transmission Complete");
    _isTransmitting = false;
    _radio->startReceive();
}

void RadioManager::txTask(void *pvParameteres)
{
    RadioManager *manager = reinterpret_cast<RadioManager *>(pvParameteres);
    for (;;)
    {
        RadioPacket *packet = nullptr;
        if (xQueueReceive(manager->_txQueue, &packet, portMAX_DELAY) == pdTRUE)
        {

            // TODO why does this not work?
            if (manager->_radio->isChannelFree() == true)
            {
                /*
                TODO: we do some crazy stuff in channelFree ie set radio to standby and remove the Dio1Action
                      to get the scan channel to work ... is everything being set back correctly .... seems
                      to work for now but could cause issues down the line :( ... hope not
                */
                manager->_isTransmitting = true;
                int status = manager->_radio->startTransmit(packet->data, packet->len);
                if (status == 0)
                {
                    manager->_radio->setDio1Callback(manager->dio1Isr);

                    Serial.printf("[RadioManager] Transmitted Packet: Length %u\n", packet->len);
                }
                else
                {
                    manager->_radio->setDio1Callback(manager->dio1Isr);
                    Serial.print("[RadioManager] TX failed, code: ");
                    Serial.println(status);
                    manager->_isTransmitting = false;
                    manager->_radio->startReceive();
                }
                vPortFree(packet);
            }
            else
            {
                Serial.println("Channel is busy");
                // Add item back to queue
                manager->enqueueTxPacket(packet->data, packet->len);
            }
        }
    }
}