#ifndef NETWORKMESSAGEHANDLER_H
#define NETWORKMESSAGEHANDLER_H

#include "IRouter.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include <cstdint>
#include <cstring>

// Structure to hold outgoing messages for transmission.
struct OutgoingMessage {
    uint32_t destNodeID;
    char message[128]; // Adjust size as needed
};

class NetworkMessageHandler {
public:
    // Constructor accepts a pointer to an IRouter instance.
    NetworkMessageHandler(IRouter* router);
    ~NetworkMessageHandler();

    // Enqueue a message for transmission.
    bool enqueueMessage(uint32_t destNodeID, const char* message);

private:
    IRouter* _router;                  // Dependency-injected router instance
    QueueHandle_t _sendQueue;            // FreeRTOS queue for outgoing messages
    TaskHandle_t _senderTaskHandle;      // Task handle for the sender task

    // The sender task function.
    static void SenderTask(void* pvParameters);
    // Internal function to process messages from the queue.
    void processQueue();
};

#endif
