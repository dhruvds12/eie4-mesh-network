#ifndef NETWORKMESSAGEHANDLER_H
#define NETWORKMESSAGEHANDLER_H

#include "IRouter.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include <cstdint>
#include <cstring>
#include <vector>

// Structure to hold outgoing messages for transmission.
enum class MsgKind : uint8_t
{
    NODE = 0,         // legacy “sendData”
    USER = 1,         // user-to-user inside mesh
    TO_GATEWAY = 2,   // to be forwarded to Internet
    FROM_GATEWAY = 3, // came from Internet, needs injecting
    REQ_PUB_KEY = 4,
    ENC_USER = 5,
    MOVE_USER_REQ = 6,
};

struct OutgoingMessage
{
    MsgKind kind;
    uint8_t flags;   // still carries FROM_GATEWAY / TO_GATEWAY bits
    uint32_t destID; // node-ID *or* user-ID (see kind)
    uint32_t userID; // source user (USER / TO_GATEWAY / FROM_GATEWAY)
    char message[128];
};

class NetworkMessageHandler
{
public:
    // Constructor accepts a pointer to an IRouter instance.
    NetworkMessageHandler(IRouter *router);
    ~NetworkMessageHandler();

    // Enqueue a message for transmission.
    /**
     * @brief
     *
     * @param destID
     * @param userMessage
     * @param message
     * @param userID -> optional
     * @param flags -> optional
     * @return true
     * @return false
     */
    bool enqueueMessage(MsgKind kind, uint32_t destID, const char *message, uint32_t userID = 0, uint8_t flags = 0);

    void announcePubKey(uint32_t userID, const uint8_t pk[32]);

    std::vector<uint32_t> getKnownNodes() const
    {
        return _router->getKnownNodeIDs();
    }
    std::vector<uint32_t> getKnownUsers() const
    {
        return _router->getKnownUserIDs();
    }

    // Forward declares teh GatewayManager class so that we can create the networkMessageHandler first
    void setGatewayManager(class GatewayManager *gw) { _gwMgr = gw; }

private:
    IRouter *_router;                       // Dependency-injected router instance
    QueueHandle_t _sendQueue;               // FreeRTOS queue for outgoing messages
    TaskHandle_t _senderTaskHandle;         // Task handle for the sender task
    class GatewayManager *_gwMgr = nullptr; // forward declate the GatewayManager

    // The sender task function.
    static void SenderTask(void *pvParameters);
    // Internal function to process messages from the queue.
    void processQueue();
};

#endif
