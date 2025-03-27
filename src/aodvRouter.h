#ifndef AODVROUTER_H
#define AODVROUTER_H

#include <Arduino.h>
#include <FreeRTOS.h>
#include <queue.h>
#include "radioManager.h"
#include "packet.h"
#include <map>
#include <vector>

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

class AODVRouter
{

public:
    /**
     * @brief Construct a new AODVRouter object
     * 
     * @param RadioManager 
     * @param myNodeID 
     */
    AODVRouter(RadioManager *RadioManager, uint32_t myNodeID);

    /**
     * @brief Initialise and create the router task
     *
     * @return true -> if task creation successful
     * @return false -> if task creation unsuccessful
     */
    bool begin();

    void sendData(uint32_t destNodeID, const uint8_t *data, size_t len);

private:
    RadioManager *_radioManager;
    uint32_t _myNodeID;
    TaskHandle_t _routerTaskHandler;

    // routeTable[dest] = RouteEntry
    std::map<uint32_t, RouteEntry> _routeTable;

    // Map for entries awaiting RREP
    std::map<uint32_t, std::vector<dataBufferEntry>> _dataBuffer;

    /**
     * @brief Router task function
     *
     * @param pvParameters
     */
    static void routerTask(void *pvParameters);

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
     * @param originalDest 
     * @param originalPacketID 
     */
    void sendRERR(uint32_t brokenNodeID, uint32_t senderNodeID ,uint32_t originalDest, uint32_t originalPacketID);

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
    RouteEntry getRoute(uint32_t destination);
    void invalidateRoute(uint32_t brokenNodeID, uint32_t finalDestNodeID, uint32_t senderNodeID);

    // DATA QUEUE HELPER FUNCTIONS
    void flushDataQueue(uint32_t destNodeID);
};

#endif