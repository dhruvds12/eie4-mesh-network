#ifndef AODVROUTER_H
#define AODVROUTER_H

#include "IRouter.h"
#include "IRadioManager.h"
#include "packet.h"
#include <map>
#include <vector>
#include <FreeRTOS.h>
#include <unordered_set>
#include "mqttmanager.h"
#include <set>
#include <unordered_map>
#include "userSessionManager.h"
#include "IClientNotifier.h"

class GatewayManager;

#ifdef UNIT_TEST
#include <gtest/gtest_prod.h>
#endif
struct RouteEntry
{
    uint32_t nextHop;
    uint8_t hopcount;
};

struct dataBufferEntry
{
    uint8_t *data;
    size_t length;
};

struct userMessageBufferEntry
{
    uint32_t senderID;
    uint8_t *message;
    size_t length;
};

struct PendingUserRouteEntry
{
    uint32_t senderID;   // who sent the message
    uint32_t destUserID; // ultimate recipient user
    uint8_t *message;    // payload pointer
    size_t length;       // payload length
};

struct ackBufferEntry
{
    uint8_t *packet; // store pointer to entire data
    size_t length;
    uint32_t expectedNextHop; // the next hop node you expect to forward the packet
    TickType_t timestamp;     // time when the packet was sent
};

// per‑user cache entry
struct GutEntry
{
    uint32_t nodeID;
    uint8_t seq;
    uint32_t ts; /* lastSeen epoch */
};

// neighbour info for Bloom check
struct NeighInfo
{
    uint32_t nodeID;
    uint8_t listVer;
    uint8_t bloom[8];
};

static const uint32_t BROADCAST_NOTIFY_BIT = (1u << 0);
static const uint32_t CLEANUP_NOTIFY_BIT = (1u << 1);

class AODVRouter : public IRouter
{

public:
    /**
     * @brief Construct a new AODVRouter object
     *
     * @param RadioManager
     * @param myNodeID
     */
    AODVRouter(IRadioManager *RadioManager, MQTTManager *MQTTManager, uint32_t myNodeID, UserSessionManager *usm, IClientNotifier *icm);

    /**
     * @brief Initialise and create the router task
     *
     * @return true -> if task creation successful
     * @return false -> if task creation unsuccessful
     */
    bool begin();

    /**
     * @brief Function to send data to a desired node, provide the final destination and
     * next hop handled by router.
     *
     * @param destNodeID The final destination (next hop handled by router)
     * @param data The raw data
     * @param len The length of the raw data
     */

    void sendData(uint32_t destNodeID, const uint8_t *data, size_t len, uint8_t flags = 0);

    void sendUserMessage(uint32_t fromUserID, uint32_t toUserID, const uint8_t *data, size_t len, uint8_t flags = 0);

    void setMQTTManager(MQTTManager *mqttMgr) { _mqttManager = mqttMgr; }

    std::vector<uint32_t> getKnownNodeIDs() const;
    std::vector<uint32_t> getKnownUserIDs() const;

    void setGatewayManager(GatewayManager *g) { _gwMgr = g; }

private:
    SemaphoreHandle_t _mutex;
    IRadioManager *_radioManager;
    uint32_t _myNodeID;
    UserSessionManager *_usm;

    TaskHandle_t _routerTaskHandler;
    TaskHandle_t _timerWorkerHandle;

    MQTTManager *_mqttManager;

    IClientNotifier *_clientNotifier;

    GatewayManager *_gwMgr = nullptr;

    struct Lock
    {
        SemaphoreHandle_t m;
        explicit Lock(SemaphoreHandle_t m) : m(m) { xSemaphoreTakeRecursive(m, portMAX_DELAY); }
        ~Lock() { xSemaphoreGiveRecursive(m); }
    };

    // Data structures

    // TODO: MUTEX!!!!! - multiple tasks access this and could modify it!!
    // routeTable[dest] = RouteEntry
    std::map<uint32_t, RouteEntry> _routeTable;

    // Map for entries awaiting RREP
    std::map<uint32_t, std::vector<dataBufferEntry>> _dataBuffer;

    // Map for entries await UREP
    std::map<uint32_t, std::vector<userMessageBufferEntry>> _userMsgBuffer;

    std::map<uint32_t, std::vector<PendingUserRouteEntry>> _userRouteBuffer;

    // Set to store seen message ids
    std::unordered_set<uint32_t> receivedPacketIDs;

    // Handle periodic broadcasts timer
    TimerHandle_t _broadcastTimer;

    // handle ackBuffer cleanup
    TimerHandle_t _ackBufferCleanupTimer;

    // nodes on the network
    std::unordered_set<uint32_t> discoveredNodes;

    // store messages that are waiting on ack
    std::map<uint32_t, ackBufferEntry> ackBuffer;

    // Global user Table
    std::unordered_map<uint32_t, GutEntry> _gut; /* userID → info */

    // Neighbour bloom filter info
    std::unordered_map<uint32_t, NeighInfo> _nbrBloom; /* nodeID → bloom */

    // Timers

    static const TickType_t ACK_TIMEOUT_TICKS = pdMS_TO_TICKS(600000);       // 10 minutes
    static const TickType_t ACK_CLEANUP_PERIOD_TICKS = pdMS_TO_TICKS(60000); // 1 minute

    // Functions:

    /**
     * @brief Router task function
     *
     * @param pvParameters
     */
    static void routerTask(void *pvParameters);

    static void timerWorkerTask(void *pvParameters);

    /**
     * @brief Timer callback to send broadcast info
     *
     * @param xTimer
     */
    static void broadcastTimerCallback(TimerHandle_t xTimer);

    void sendBroadcastInfo();

    void cleanupAckBuffer();

    static void ackCleanupCallback(TimerHandle_t xTimer);

    // TOP LEVEL RX PACKET HANDLERS

    /**
     * @brief Top level packet handler
     *
     * @param rxPacket
     */
    void handlePacket(RadioPacket *rxPacket);

    /**
     * @brief
     *
     * @param base
     * @param payload
     * @param payloadLen
     */
    void handleRREQ(const BaseHeader &base, const uint8_t *payload, size_t payloadLen);

    /**
     * @brief
     *
     * @param base
     * @param payload
     * @param payloadLen
     */
    void handleRREP(const BaseHeader &base, const uint8_t *payload, size_t payloadLen);

    /**
     * @brief
     *
     * @param base
     * @param payload
     * @param payloadLen
     */
    void handleRERR(const BaseHeader &base, const uint8_t *payload, size_t payloadLen);

    /**
     * @brief
     *
     * @param base
     * @param payload
     * @param payloadLen
     */
    void handleData(const BaseHeader &base, const uint8_t *payload, size_t payloadLen);

    /**
     * @brief
     *
     * @param base
     * @param payload
     * @param payloadLen
     */
    void handleBroadcast(const BaseHeader &base, const uint8_t *payload, size_t payloadLen);

    /**
     * @brief
     *
     * @param base
     * @param payload
     * @param payloadLen
     */
    void handleBroadcastInfo(const BaseHeader &base, const uint8_t *payload, size_t payloadLen);

    void handleUREQ(const BaseHeader &base, const uint8_t *payload, size_t payloadlen);

    void handleUREP(const BaseHeader &base, const uint8_t *payload, size_t payloadlen);

    void handleUERR(const BaseHeader &base, const uint8_t *payload, size_t payloadlen);

    void handleDataAck(const BaseHeader &base, const uint8_t *payload, size_t payloadlen);

    void handleUserMessage(const BaseHeader &base, const uint8_t *payload, size_t payloadlen);

    // SEND PACKET HELPER FUNCTIONS

    /**
     * @brief
     *
     * @param destNodeID
     */
    void sendRREQ(uint32_t destNodeID);

    /**
     * @brief
     *
     * @param destNodeID
     * @param nextHop
     * @param hopCount
     */
    void sendRREP(uint32_t originNodeID, uint32_t destNodeID, uint32_t nextHop, uint8_t hopCount);

    /**
     * @brief
     *
     * @param brokenNodeID
     * @param senderNodeID
     * @param originalDest
     * @param originalPacketID
     */
    void sendRERR(uint32_t brokenNodeID, uint32_t senderNodeID, uint32_t originalDest, uint32_t originalPacketID);

    void sendUREQ(uint32_t userID);

    void sendUREP(uint32_t originNodeID, uint32_t destNodeID, uint32_t userID, uint32_t nextHop, uint16_t lifetime, uint8_t hopCount);

    void sendUERR(uint32_t userID, uint32_t nodeID, uint32_t originNodeID, uint32_t originalPacketID, uint32_t nextHop);
    /**
     * @brief
     *
     * @param header
     * @param extHeader
     * @param extLen
     * @param payload
     * @param payloadLen
     */
    void transmitPacket(const BaseHeader &header, const uint8_t *extHeader, size_t extLen,
                        const uint8_t *payload = nullptr, size_t payloadLen = 0);

    //  ROUTING TABLE HELPER FUNCTIONS
    void updateRoute(uint32_t destination, uint32_t nextHop, uint8_t hopCount);
    bool hasRoute(uint32_t destination);
    bool getRoute(uint32_t destination, RouteEntry &RouteEntry);
    void invalidateRoute(uint32_t brokenNodeID, uint32_t finalDestNodeID, uint32_t senderNodeID);

    // DATA QUEUE HELPER FUNCTIONS
    void flushDataQueue(uint32_t destNodeID);

    // Seen IDs SET HELPER FUNCTIONS
    bool isDuplicatePacketID(uint32_t packetID);

    void storePacketID(uint32_t packetID);

    // known nodes
    bool isNodeIDKnown(uint32_t packetID);

    void saveNodeID(uint32_t packetID);

    // ACK BUFFER HELPER FUNCTIONS
    void storeAckPacket(uint32_t packetID, const uint8_t *packet, size_t length, uint32_t expectedNextHop);

    bool findAckPacket(uint32_t packetID);

    void insertDataBuffer(uint32_t destNodeID, uint8_t *data, size_t len);

    bool ackBufferHasPacketID(uint32_t packetID);

    void removeFromACKBuffer(uint32_t packetID);

    bool tryImplicitAck(uint32_t packetID);

    void removeItemRoutingTable(uint32_t ID);

    void flushUserRouteBuffer(uint32_t nodeID);

    inline void addUserMessage(uint32_t userID, const userMessageBufferEntry &entry)
    {
        Lock lock(_mutex);
        _userMsgBuffer[userID].push_back(entry);
    }

    inline bool hasBufferedUserMessages(uint32_t userID) const
    {
        Lock lock(_mutex);
        auto it = _userMsgBuffer.find(userID);
        return (it != _userMsgBuffer.end() && !it->second.empty());
    }

    inline std::vector<userMessageBufferEntry> popBufferedUserMessages(uint32_t userID)
    {
        Lock lock(_mutex);
        auto it = _userMsgBuffer.find(userID);
        if (it == _userMsgBuffer.end())
        {
            return {};
        }
        // move out the vector to avoid copy
        std::vector<userMessageBufferEntry> msgs = std::move(it->second);
        _userMsgBuffer.erase(it);
        return msgs;
    }

    inline void updateGutEntry(uint32_t userID, const GutEntry &entry)
    {
        Lock lock(_mutex);
        _gut[userID] = entry;
    }

    inline bool getGutEntry(uint32_t userID, GutEntry &out) const
    {
        Lock lock(_mutex);
        auto it = _gut.find(userID);
        if (it == _gut.end())
        {
            return false;
        }
        out = it->second;
        return true;
    }

    inline void removeGutEntry(uint32_t userID)
    {
        Lock lock(_mutex);
        _gut.erase(userID);
    }

    inline bool hasGutEntry(uint32_t userID) const
    {
        Lock lock(_mutex);
        return _gut.find(userID) != _gut.end();
    }

    inline void addPendingUserRouteMessage(uint32_t nodeID, const PendingUserRouteEntry &entry)
    {
        Lock lock(_mutex);
        _userRouteBuffer[nodeID].push_back(entry);
    }

    inline bool hasPendingUserRouteMessages(uint32_t nodeID) const
    {
        Lock lock(_mutex);
        auto it = _userRouteBuffer.find(nodeID);
        return it != _userRouteBuffer.end() && !it->second.empty();
    }

    inline std::vector<PendingUserRouteEntry> popPendingUserRouteMessages(uint32_t nodeID)
    {
        Lock lock(_mutex);
        auto it = _userRouteBuffer.find(nodeID);
        if (it == _userRouteBuffer.end())
        {
            return {};
        }
        auto msgs = std::move(it->second);
        _userRouteBuffer.erase(it);
        return msgs;
    }



#ifdef UNIT_TEST
    FRIEND_TEST(AODVRouterTest, BasicSendDataTest);
    FRIEND_TEST(AODVRouterTest, BasicReceiveRREP);
    FRIEND_TEST(AODVRouterTest, ForwardRREP);
    FRIEND_TEST(AODVRouterTest, ReceiveRREPFlushDataQueue);
    FRIEND_TEST(AODVRouterTest, IgnoreMessagesNotAddressedToNode);
    FRIEND_TEST(AODVRouterTest, ReadBroadcasts);
    FRIEND_TEST(AODVRouterTest, BasicReceiveRERR);
    FRIEND_TEST(AODVRouterTest, ComplicatedReceiveRERR);
    FRIEND_TEST(AODVRouterTest, BasicReceiveRREQ);
    FRIEND_TEST(AODVRouterTest, ForwardRREQ);
    FRIEND_TEST(AODVRouterTest, RespondToRREQ);
    FRIEND_TEST(AODVRouterTest, NewRouteFound);
    FRIEND_TEST(AODVRouterTest, IgnoreNewRoute);
    FRIEND_TEST(AODVRouterTest, handleData);
    FRIEND_TEST(AODVRouterTest, forwardData);
    FRIEND_TEST(AODVRouterTest, DiscardSeenPacket);
    FRIEND_TEST(AODVRouterTest, SendBroadcastInfo);
    FRIEND_TEST(AODVRouterTest, ReceiveBroadcastInfo);
    FRIEND_TEST(AODVRouterTest, ReceiveBroadcastInfoExceedMaxHops);
    FRIEND_TEST(AODVRouterTest, ImplicitACKBufferTest);
#endif
};

#endif