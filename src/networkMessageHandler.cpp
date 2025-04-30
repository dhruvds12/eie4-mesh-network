#include "NetworkMessageHandler.h"
#include <cstdio>
#include <Arduino.h>

// Constants for queue and task configuration.
#define QUEUE_LENGTH 10
#define TASK_STACK_SIZE 4096
#define TASK_PRIORITY 2

NetworkMessageHandler::NetworkMessageHandler(IRouter *router)
    : _router(router), _senderTaskHandle(nullptr)
{
    _sendQueue = xQueueCreate(QUEUE_LENGTH, sizeof(OutgoingMessage));
    if (_sendQueue == NULL)
    {
        printf("Failed to create send queue!\n");
    }
    BaseType_t taskCreated = xTaskCreate(
        SenderTask,
        "SenderTask",
        TASK_STACK_SIZE,
        this,
        TASK_PRIORITY,
        &_senderTaskHandle);
    if (taskCreated != pdPASS)
    {
        printf("Failed to create sender task!\n");
    }
}

NetworkMessageHandler::~NetworkMessageHandler()
{
    if (_senderTaskHandle != NULL)
    {
        vTaskDelete(_senderTaskHandle);
    }
    if (_sendQueue != NULL)
    {
        vQueueDelete(_sendQueue);
    }
}

bool NetworkMessageHandler::enqueueMessage(uint32_t destNodeID, bool userMessage, const char *message, uint32_t userID)
{
    OutgoingMessage msg;
    msg.destID = destNodeID;
    msg.userMessage = userMessage;
    msg.userID = userID;
    // Safely copy the message into the fixed-size buffer.
    strncpy(msg.message, message, sizeof(msg.message) - 1);
    msg.message[sizeof(msg.message) - 1] = '\0';

    if (xQueueSend(_sendQueue, &msg, pdMS_TO_TICKS(100)) != pdPASS)
    {
        return false;
    }
    return true;
}

void NetworkMessageHandler::SenderTask(void *pvParameters)
{
    NetworkMessageHandler *handler = static_cast<NetworkMessageHandler *>(pvParameters);
    handler->processQueue();
}

void NetworkMessageHandler::processQueue()
{
    OutgoingMessage msg;
    for (;;)
    {
        // Wait indefinitely for a message from the queue.
        if (xQueueReceive(_sendQueue, &msg, portMAX_DELAY) == pdTRUE)
        {
            if (!msg.userMessage)
            {
                // Send the message using the injected router.
                _router->sendData(msg.destID,
                                  reinterpret_cast<const uint8_t *>(msg.message),
                                  strlen(msg.message));
            }
            else
            {
                Serial.printf("Sending user message from %u, to %u\n", msg.userID, msg.destID);
                _router->sendUserMessage(msg.userID, msg.destID, reinterpret_cast<const uint8_t *>(msg.message), strlen(msg.message));
            }
        }
    }
}
