#include "RadioManager.h"
#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

SemaphoreHandle_t RadioManager::_irqSemaphore = nullptr;
SemaphoreHandle_t RadioManager::_txDoneSemaphore = nullptr;

RadioManager::RadioManager(ILoRaRadio *radio)
    : _radio(radio), _radioTaskHandle(nullptr), _txTaskHandle(nullptr), _rxQueue(nullptr), _txQueue(nullptr), _isTransmitting(false)
{
    // If semaphore isn't created yet, create it
    if (_irqSemaphore == nullptr)
    {
        _irqSemaphore = xSemaphoreCreateBinary();
    }

    if (_txDoneSemaphore == nullptr)
    {
        // Initially “taken,” so first send will proceed immediately.
        _txDoneSemaphore = xSemaphoreCreateBinary();
        // Take it now so that no one can “give” before we even send.
        if (_txDoneSemaphore)
        {
            xSemaphoreTake(_txDoneSemaphore, 0);
        }
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
        3, // bumped to higher priority than AODVRouter
        &_txTaskHandle);

    if (txTaskCreated != pdPASS)
    {
        Serial.println("[RadioManager] Could not create tx task!");
        return false;
    }

    if (_txDoneSemaphore)
    {
        xSemaphoreTake(_txDoneSemaphore, 0);
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
    // xHigherPriorityTaskWoken flag tells FreeRTOS if giving the semaphore
    // has unblocked a higher-priority task than the one currently running.
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (_irqSemaphore)
    {

        // FromISR variant is safe here: it atomically gives the binary semaphore
        // to wake radioTask() without doing any heavy work in interrupt context.
        xSemaphoreGiveFromISR(_irqSemaphore, &xHigherPriorityTaskWoken);
    }

    // If we unblocked a task, yield to it
    if (xHigherPriorityTaskWoken)
    {
        // If by giving the semaphore we’ve woken a task with higher priority,
        // yield immediately to that task before returning from the ISR.
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

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(_txDoneSemaphore, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// void RadioManager::txTask(void *pvParameteres)
// {
//     RadioManager *manager = reinterpret_cast<RadioManager *>(pvParameteres);
//     for (;;)
//     {
//         RadioPacket *packet = nullptr;
//         if (xQueueReceive(manager->_txQueue, &packet, portMAX_DELAY) == pdTRUE)
//         {

//             // TODO why does this not work?
//             if (manager->_radio->isChannelFree() == true)
//             {
//                 /*
//                 TODO: we do some crazy stuff in channelFree ie set radio to standby and remove the Dio1Action
//                       to get the scan channel to work ... is everything being set back correctly .... seems
//                       to work for now but could cause issues down the line :( ... hope not
//                 */
//                 manager->_isTransmitting = true;
//                 int status = manager->_radio->startTransmit(packet->data, packet->len);
//                 if (status == 0)
//                 {
//                     manager->_radio->setDio1Callback(manager->dio1Isr);

//                     Serial.printf("[RadioManager] Transmitted Packet: Length %u\n", packet->len);
//                 }
//                 else
//                 {
//                     manager->_radio->setDio1Callback(manager->dio1Isr);
//                     Serial.print("[RadioManager] TX failed, code: ");
//                     Serial.println(status);
//                     manager->_isTransmitting = false;
//                     manager->_radio->startReceive();
//                 }
//                 vPortFree(packet);
//             }
//             else
//             {
//                 Serial.println("Channel is busy");
//                 // Add item back to queue
//                 manager->enqueueTxPacket(packet->data, packet->len);
//             }
//         }
//     }
// }

// void RadioManager::txTask(void *pvParameteres)
// {
//     RadioManager *manager = reinterpret_cast<RadioManager *>(pvParameteres);
//     for (;;)
//     {
//         // Wait until there’s a packet to send
//         RadioPacket *packet = nullptr;
//         if (xQueueReceive(manager->_txQueue, &packet, portMAX_DELAY) == pdTRUE)
//         {
//             // We got “packet.”  Now we need to wait until the LoRa channel is free,
//             // doing a randomized backoff each time it’s busy, to comply with EU LoRa regs.

//             const uint32_t BACKOFF_MIN_MS = 5;
//             const uint32_t BACKOFF_MAX_MS = 50;
//             bool done = false;

//             while (!done)
//             {
//                 if (manager->_radio->isChannelFree())
//                 {
//                     // Channel is free → transmit
//                     manager->_isTransmitting = true;
//                     int status = manager->_radio->startTransmit(packet->data, packet->len);
//                     if (status == 0)
//                     {
//                         manager->_radio->setDio1Callback(manager->dio1Isr);
//                         Serial.printf("[RadioManager] Transmitted Packet: Length %u\n", packet->len);
//                     }
//                     else
//                     {
//                         // If startTransmit failed, fall back to receive mode immediately
//                         manager->_radio->setDio1Callback(manager->dio1Isr);
//                         Serial.print("[RadioManager] TX failed, code: ");
//                         Serial.println(status);
//                         manager->_isTransmitting = false;
//                         manager->_radio->startReceive();
//                     }
//                     done = true;
//                 }
//                 else
//                 {
//                     // Channel is busy → pick a random backoff interval (between MIN and MAX),
//                     // sleep that long, then retry.  This does NOT hold the CPU.
//                     uint32_t rnd = BACKOFF_MIN_MS + (esp_random() % (BACKOFF_MAX_MS - BACKOFF_MIN_MS + 1));
//                     vTaskDelay(pdMS_TO_TICKS(rnd));
//                 }
//             }

//             // Only once we’ve actually handed the packet to startTransmit()
//             // can we free its memory.
//             vPortFree(packet);
//         }
//     }
// }

// void RadioManager::txTask(void *pvParameteres)
// {
//     RadioManager *manager = reinterpret_cast<RadioManager *>(pvParameteres);

//     // Once per system boot, initialize the random‐seed so esp_random() isn't always the same
//     // (optional, but recommended if esp_random() is the only source of “random” here).
//     // ets_random_seed(xthal_get_ccount());  // uncomment if you want more entropy

//     for (;;)
//     {
//         //  Wait until there’s a new packet to send.
//         RadioPacket *packet = nullptr;
//         if (xQueueReceive(manager->_txQueue, &packet, portMAX_DELAY) == pdTRUE)
//         {
//             //  Try sending it, backing off if the channel is busy.
//             //
//             //  We  do a randomized backoff of 5–50 ms whenever isChannelFree()==false,
//             //  but once we do successfully hand the packet to startTransmit(), we immediately
//             //  enforce a fixed 10 ms cooldown before trying to send the next packet.
//             //
//             const uint32_t BACKOFF_MIN_MS = 5;
//             const uint32_t BACKOFF_MAX_MS = 50;
//             const TickType_t COOLDOWN_TICKS = pdMS_TO_TICKS(10);

//             bool done = false;
//             while (!done)
//             {
//                 if (manager->_radio->isChannelFree())
//                 {
//                     // Channel is free → transmit right now
//                     manager->_isTransmitting = true;
//                     int status = manager->_radio->startTransmit(packet->data, packet->len);
//                     if (status == 0)
//                     {
//                         // Re‐arm the DIO1 callback (so that we’ll get an ISR on TX‐done)
//                         manager->_radio->setDio1Callback(manager->dio1Isr);
//                         Serial.printf("[RadioManager] Transmitted Packet: Length %u\n", packet->len);
//                     }
//                     else
//                     {
//                         // If startTransmit() failed, fall back to receive mode immediately
//                         manager->_radio->setDio1Callback(manager->dio1Isr);
//                         Serial.print("[RadioManager] TX failed, code: ");
//                         Serial.println(status);
//                         manager->_isTransmitting = false;
//                         manager->_radio->startReceive();
//                     }
//                     done = true;
//                 }
//                 else
//                 {
//                     // Channel busy → pick a random backoff in [5..50] ms, sleep, retry
//                     uint32_t rnd = BACKOFF_MIN_MS + (esp_random() % (BACKOFF_MAX_MS - BACKOFF_MIN_MS + 1));
//                     vTaskDelay(pdMS_TO_TICKS(rnd));
//                 }
//             }

//             //  Now that the packet was *sent* (or at least handed to startTransmit),
//             //  free its memory and enforce a 10 ms cooldown before looping.
//             vPortFree(packet);

//             //  Cooldown: prevent any immediate next‐send until 10 ms have passed.
//             //  This spaces out back‐to‐back transmissions even if the channel is free.
//             vTaskDelay(COOLDOWN_TICKS);
//         }
//     }
// }

// void RadioManager::txTask(void *pvParameteres)
// {
//     RadioManager *manager = reinterpret_cast<RadioManager *>(pvParameteres);

//     // ets_random_seed(xthal_get_ccount());

//     for (;;)
//     {
//         //  Wait for a new packet to appear
//         RadioPacket *packet = nullptr;
//         if (xQueueReceive(manager->_txQueue, &packet, portMAX_DELAY) != pdTRUE)
//         {
//             continue;
//         }

//         const uint32_t BACKOFF_MIN_MS = 5;
//         const uint32_t BACKOFF_MAX_MS = 50;
//         const TickType_t COOLDOWN_TICKS = pdMS_TO_TICKS(10);

//         //  Wait until any prior TX is fully done.
//         //  Instead of polling with a 2 ms delay, we block on _txDoneSemaphore.
//         //  If _isTransmitting is false right now, semaphore take should succeed immediately.
//         if (manager->_isTransmitting)
//         {
//             // Block indefinitely until ISR gives us “TX finished.”
//             xSemaphoreTake(manager->_txDoneSemaphore, portMAX_DELAY);
//         }

//         //  Now _isTransmitting is guaranteed false. Next, back off until channel is free.
//         while (!manager->_radio->isChannelFree())
//         {
//             uint32_t rnd = BACKOFF_MIN_MS + (esp_random() % (BACKOFF_MAX_MS - BACKOFF_MIN_MS + 1));
//             vTaskDelay(pdMS_TO_TICKS(rnd));
//         }

//         // The radio needs some time to swap to transmit otherwise the packet gets lost
//         vTaskDelay(pdMS_TO_TICKS(2));

//         //  Channel is free AND no TX in flight → kick off our new send.
//         manager->_isTransmitting = true;
//         int status = manager->_radio->startTransmit(packet->data, packet->len);
//         if (status == 0)
//         {
//             // Arrange to get the DIO1 interrupt on TX‐done
//             manager->_radio->setDio1Callback(manager->dio1Isr);
//             Serial.printf("[RadioManager] Transmitted Packet: Length %u\n", packet->len);
//         }
//         else
//         {
//             // Immediate TX failure → drop back to RX and clear the flag
//             manager->_radio->setDio1Callback(manager->dio1Isr);
//             Serial.print("[RadioManager] TX failed, code: ");
//             Serial.println(status);
//             manager->_isTransmitting = false;
//             manager->_radio->startReceive();
//             // Also “give” the TX‐done semaphore so that if anyone else is waiting, they wake up
//             xSemaphoreGive(manager->_txDoneSemaphore);
//         }

//         //  We’ve handed the packet off (or dropped it). Free its memory.
//         vPortFree(packet);

//         //  Enforce a 10 ms cool‐down before trying the next send
//         //  (even if channel is free immediately, we delay 10 ms.)
//         // vTaskDelay(COOLDOWN_TICKS);
//     }
// }

void RadioManager::txTask(void *pvParameteres)
{
    RadioManager *mgr = reinterpret_cast<RadioManager *>(pvParameteres);

    for (;;)
    {

        RadioPacket *pkt = nullptr;
        if (xQueueReceive(mgr->_txQueue, &pkt, portMAX_DELAY) != pdTRUE)
            continue; // nothing to do

        if (mgr->_isTransmitting)
            xSemaphoreTake(mgr->_txDoneSemaphore, portMAX_DELAY);

        TickType_t backoffBin = pdMS_TO_TICKS(mgr->csma.binInitMs);
        uint8_t beExp = 2;
        bool sent = false;

        while (!sent)
        {
            /*  busy? → choose a back-off ---------------------- */
            if (!mgr->_radio->isChannelFree())
            {
                TickType_t waitTicks = 0;

                switch (mgr->csma.scheme)
                {
                case CsmaOptions::BackoffScheme::Binary:
                {
                    uint32_t rnd = esp_random() % (backoffBin / portTICK_PERIOD_MS + 1);
                    waitTicks = pdMS_TO_TICKS(rnd);
                    backoffBin = std::min(backoffBin * 2, pdMS_TO_TICKS(mgr->csma.binMaxMs));
                    break;
                }
                case CsmaOptions::BackoffScheme::BE:
                {
                    if (beExp > mgr->csma.beMaxExp)
                        beExp = mgr->csma.beMaxExp;
                    uint32_t slot = esp_random() % (1u << beExp);
                    waitTicks = pdMS_TO_TICKS(slot * mgr->csma.beUnitMs);
                    ++beExp;
                    break;
                }
                default: /* legacy uniform 5–50 ms */
                    waitTicks = pdMS_TO_TICKS(mgr->csma.legacyMinMs + esp_random() % (mgr->csma.legacyMaxMs - mgr->csma.legacyMinMs + 1));
                }

                vTaskDelay(waitTicks);
                continue; // retry CCA afterwards
            }

            /*  channel looks idle – optional PCSMA coin-flip -- */
            if (mgr->csma.pcsmaEnabled)
            {
                float r = esp_random() / static_cast<float>(UINT32_MAX);
                if (r > mgr->csma.pTransmit)
                {
                    vTaskDelay(pdMS_TO_TICKS(mgr->csma.deferSlotMs));
                    continue; // defer one slot, retry
                }
            }

            /*  guard time RX→TX  ---------- */
            vTaskDelay(pdMS_TO_TICKS(2));

            /*  fire the real transmit ------------------------ */
            mgr->_isTransmitting = true;
            int rc = mgr->_radio->startTransmit(pkt->data, pkt->len);
            if (rc == 0)
            {
                mgr->_radio->setDio1Callback(mgr->dio1Isr);
                Serial.printf("[RadioManager] TX OK len=%u\n", pkt->len);
            }
            else
            {
                Serial.printf("[RadioManager] TX fail rc=%d – drop\n", rc);
                mgr->_isTransmitting = false;
                mgr->_radio->startReceive();
                xSemaphoreGive(mgr->_txDoneSemaphore); // unblock waiters
            }
            sent = true; // leave CSMA loop regardless of rc
        }

        vPortFree(pkt);
    }
}