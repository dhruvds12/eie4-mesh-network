#include "PingPongRouter.h"
#include <FreeRTOS.h>
#include <task.h>
#include <string.h>

PingPongRouter::PingPongRouter(IRadioManager *radioManager)
    : _radioManager(radioManager), _routerTaskHandle(nullptr)
{
}

bool PingPongRouter::begin()
{
    // Create the router task.
    BaseType_t taskCreated = xTaskCreate(
        routerTask, "PingPongRouter", 4096, this, 2, &_routerTaskHandle);
    if (taskCreated != pdPASS)
    {
        Serial.println("[PingPongRouter] Could not create router task!");
        return false;
    }
    return true;
}

void PingPongRouter::routerTask(void *pvParameters)
{
    PingPongRouter *router = reinterpret_cast<PingPongRouter *>(pvParameters);
    RadioPacket *packet = nullptr;
    for (;;)
    {
        // Block until a packet is available in the RX queue.
        if (router->_radioManager->dequeueRxPacket(&packet))
        {
            // For this simple test, if the packet data equals "PING", reply with "PONG".
            if (packet->len >= 4 && strncmp((const char *)packet->data, "PING", 4) == 0)
            {
                Serial.println("[PingPongRouter] Received PING, replying with PONG");
                vTaskDelay(pdMS_TO_TICKS(1000));
                const char reply[] = "PONG";
                router->_radioManager->enqueueTxPacket((const uint8_t *)reply, sizeof(reply) - 1);
            }
            else if (packet->len >= 4 && strncmp((const char *)packet->data, "PONG", 4) == 0)
            {
                Serial.println("[PingPongRouter] Received PONG, replying with PING");
                vTaskDelay(pdMS_TO_TICKS(1000));
                const char reply[] = "PING";
                router->_radioManager->enqueueTxPacket((const uint8_t *)reply, sizeof(reply) - 1);
            }
            else
            {
                Serial.println("[PingPongRouter] Received unknown packet");
                Serial.write(packet->data, packet->len);
                Serial.println();
            }
            // Free the received packet after processing.
            vPortFree(packet);
        }
    }
}
