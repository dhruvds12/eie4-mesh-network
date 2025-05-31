#include "NetworkMessageHandler.h"
#include <cstdio>
#include <Arduino.h>
#include "gatewayManager.h"

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

bool NetworkMessageHandler::enqueueMessage(MsgKind kind, uint32_t destID, const uint8_t *data, size_t len, uint32_t userID, uint8_t flags)
{
    OutgoingMessage msg{};
    msg.kind = kind;
    msg.flags = flags;
    msg.destID = destID;
    msg.userID = userID;
    msg.length = (len > sizeof(msg.message)) ? sizeof(msg.message) : len;
    memcpy(msg.message, data, msg.length);

    if (xQueueSend(_sendQueue, &msg, pdMS_TO_TICKS(100)) != pdPASS)
    {
        return false;
    }
    return true;
}

void NetworkMessageHandler::announcePubKey(uint32_t userID, const uint8_t pk[32])
{
    _router->addPubKey(userID, *reinterpret_cast<const std::array<uint8_t, 32> *>(pk));
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
            if (msg.kind == MsgKind::NODE)
            {
                _router->sendData(msg.destID,
                                  msg.message,
                                  msg.length,
                                  msg.flags);
            }
            else if (msg.kind == MsgKind::USER || msg.kind == MsgKind::FROM_GATEWAY)
            {
                _router->sendUserMessage(msg.userID,
                                         msg.destID,
                                         msg.message,
                                         msg.length,
                                         msg.flags);
            }
            else if (msg.kind == MsgKind::TO_GATEWAY && _gwMgr)
            {
                if (_gwMgr->isOnline())
                {
                    Serial.print("Sent message to uplink\n");
                    // forward to GatewayManager queue – never touches the radio here
                    _gwMgr->uplink(msg.userID, msg.destID, msg.message, msg.length);
                }
                else
                {
                    if (_router->haveGateway())
                    {
                        _router->sendUserMessage(msg.userID,
                                                 msg.destID,
                                                 msg.message,
                                                 msg.length,
                                                 msg.flags);
                    }
                    else
                    {
                        Serial.println("Message from user dropped no access to a Gateway");
                    }
                }
            }
            else if (msg.kind == MsgKind::REQ_PUB_KEY)
            {

                _router->sendPubKeyReq(msg.destID);
            }
            else if (msg.kind == MsgKind::ENC_USER)
            {
                Serial.println("Sending encoded messages to other user");
                _router->sendUserMessage(msg.userID,
                                         msg.destID,
                                         msg.message,
                                         msg.length,
                                         msg.flags);
            }
            else if (msg.kind == MsgKind::MOVE_USER_REQ)
            {
                /* userID   = moved user
                   destID   = old nodeID                                 */
                _router->sendMoveUserReq(msg.userID, msg.destID);
            }
        }
    }
}
