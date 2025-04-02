#include "AODVRouter.h"
#include <Arduino.h>

static const uint8_t PKT_BROADCAST = 0x01;
static const uint8_t PKT_RREQ = 0x02;
static const uint8_t PKT_RREP = 0x03;
static const uint8_t PKT_RERR = 0x04;
static const uint8_t PKT_ACK = 0x05;
static const uint8_t PKT_DATA = 0x06;

static const uint32_t BROADCAST_ADDR = 0xFFFFFFFF;

AODVRouter::AODVRouter(IRadioManager *radioManager, uint32_t myNodeID)
    : _radioManager(radioManager), _myNodeID(myNodeID), _routerTaskHandler(nullptr)
{
}

// TODO: can the ifdef be removed?
bool AODVRouter::begin()
{
#ifdef UNIT_TEST
    // For unit tests, skip task creation and assume initialization is successful.
    return true;
#else
    BaseType_t taskCreated = xTaskCreate(
        routerTask,
        "AODVRouterTask",
        4096,
        this,
        2,
        &_routerTaskHandler);
    if (taskCreated != pdPASS)
    {
        Serial.println("[AODVRouter] Could not create router task!");
        return false;
    }
    return true;
#endif
}

// TODO: Can the ifndef be removed?
#ifndef UNIT_TEST
// The routerTask function is used only when running under FreeRTOS.
void AODVRouter::routerTask(void *pvParameters)
{
    AODVRouter *router = reinterpret_cast<AODVRouter *>(pvParameters);
    RadioPacket *packet = nullptr;

    for (;;)
    {
        if (router->_radioManager->dequeueRxPacket(&packet))
        {
            router->handlePacket(packet);
            vPortFree(packet);
        }
    }
}
#endif

void AODVRouter::sendData(uint32_t destNodeID, const uint8_t *data, size_t len)
{

    if (!hasRoute(destNodeID))
    {
        Serial.printf("[AODVRouter] No route for %u, sending RREQ.\n", destNodeID);

        uint8_t *copy = (uint8_t *)pvPortMalloc(len);
        if (copy)
        {
            memcpy(copy, data, len);
            _dataBuffer[destNodeID].push_back({copy, len});
        }
        else
        {
            Serial.println("[AODVRouter] Memory allocation failed for data buffer");
        }

        sendRREQ(destNodeID);
        return;
    }

    RouteEntry re = getRoute(destNodeID);

    BaseHeader bh;
    bh.destNodeID = re.nextHop;
    bh.srcNodeID = _myNodeID;
    bh.packetID = (uint32_t)(esp_random()); // TODO: Might need to improve random number generation
    bh.packetType = PKT_DATA;
    bh.flags = 0; // No flags set ie no ACK etc expected
    bh.hopCount = 0;
    bh.reserved = 0;

    DATAHeader dh;
    dh.finalDestID = destNodeID;

    transmitPacket(bh, (uint8_t *)&dh, sizeof(DATAHeader), data, len);
}

void AODVRouter::handlePacket(RadioPacket *rxPacket)
{
    if (rxPacket->len < sizeof(BaseHeader))
    {
        Serial.println("[AODVRouter] Received packet with size less than baseheader size. Discarded");
        return;
    }

    BaseHeader bh;
    deserialiseBaseHeader(rxPacket->data, bh);

    if (bh.srcNodeID == _myNodeID)
    {
        Serial.println("[AODVRouter] Reveived packet with srcNodeID == myNodeID. Not expected behaviour!");
        return;
    }

    if (bh.destNodeID != BROADCAST_ADDR && bh.destNodeID != _myNodeID)
    {
        Serial.println("[AODVRouter] Not a message for me");
        return;
    }

    // Check if prev. seen message, hopCount etc.

    size_t offset = sizeof(BaseHeader);
    size_t payloadLen = rxPacket->len - offset;
    uint8_t *payload = rxPacket->data + offset;

    switch (bh.packetType)
    {
    case PKT_RREQ:
        handleRREQ(bh, payload, payloadLen);
        break;
    case PKT_RREP:
        handleRREP(bh, payload, payloadLen);
        break;
    case PKT_RERR:
        handleRERR(bh, payload, payloadLen);
        break;
    case PKT_DATA:
        Serial.println("[AODVRouter] Entered Data path");
        handleData(bh, payload, payloadLen);
        break;
    default:
        Serial.printf("[AODVRouter] Unknown packet type :( %u\n", bh.packetType);
        break;
    }
}

void AODVRouter::handleRREQ(const BaseHeader &base, const uint8_t *payload, size_t payloadLen)
{
    if (payloadLen < sizeof(RREQHeader))
    {
        Serial.println("[AODVRouter] RREQ payload too small!");
        return;
    }

    RREQHeader rreq;
    memcpy(&rreq, payload, sizeof(RREQHeader));

    // add route to origin node through the node sending if the hopcount is less than any previous route
    updateRoute(rreq.originNodeID, base.srcNodeID, base.hopCount + 1);

    // technically shoudl also add the neighbour who sent it as you may not have them saved either
    updateRoute(base.srcNodeID, base.srcNodeID, 1);

    if (rreq.RREQDestNodeID == _myNodeID)
    {
        Serial.printf("[AODVRouter] RREQ arrived at final dest: me (%u)\n", _myNodeID);
        // Note:
        // Changed hop count to not use the prev hop count as it may take a different route to get back to the
        // the origin node so we reset to 0. To improve this we would need to store the route taken by this packet.
        // Therefore, numHops needds to incremented everytime it is forwarded.
        sendRREP(rreq.originNodeID, _myNodeID, base.srcNodeID, 0);
        return;
    }

    if (hasRoute(rreq.RREQDestNodeID))
    {
        RouteEntry re = getRoute(rreq.RREQDestNodeID);
        Serial.printf("[AODVRouter] I have a route to %u, so I'll send RREP back to %u.\n", rreq.RREQDestNodeID, rreq.originNodeID);
        // There is a route to the node, therefore use the entry as the base number of hops
        sendRREP(rreq.originNodeID, rreq.RREQDestNodeID, base.srcNodeID, re.hopcount);
        return;
    }

    RREQHeader forwardRreq = rreq;
    // forwardRreq.currentHops++; // TODO: Not sure this is required as we have hop count already in base header

    BaseHeader fwdBase = base;

    // Next hop is broadcast
    fwdBase.srcNodeID = _myNodeID; // now I'm the new "immediate sender"
    fwdBase.hopCount++;            // or do your TTL logic
    // We do a broadcast
    fwdBase.destNodeID = BROADCAST_ADDR;
    fwdBase.packetType = PKT_RREQ;

    transmitPacket(fwdBase, (uint8_t *)&forwardRreq, sizeof(RREQHeader));
}

void AODVRouter::handleRREP(const BaseHeader &base, const uint8_t *payload, size_t payloadLen)
{
    if (payloadLen < sizeof(RREPHeader))
    {
        Serial.printf("[AODVRouter] RREP payload size %u \n", (unsigned)payloadLen);
        Serial.printf("[AODVRouter] RREP expected payload size %u \n", sizeof(RREPHeader));
        Serial.println("[AODVRouter] RREP payload too small!");
        return;
    }

    RREPHeader rrep;
    memcpy(&rrep, payload, sizeof(RREPHeader));

    // update route to the rrep.RREPDESTNODEID if not already found
    updateRoute(rrep.RREPDestNodeID, base.srcNodeID, rrep.numHops + 1);

    // technically should also add the neighbour who sent it as you may not have them saved either
    updateRoute(base.srcNodeID, base.srcNodeID, 1);

    if (hasRoute(rrep.RREPDestNodeID))
    {
        flushDataQueue(rrep.RREPDestNodeID);
    }

    // If I initially sent the RREQ no need to continue forwarding
    if (_myNodeID == rrep.originNodeID)
    {
        Serial.println("[AODVRouter] Got RREP for rreq");
        return;
    }

    if (!hasRoute(rrep.originNodeID))
    {
        Serial.println("[AODVRouter] Got RREP but no route to the origin!");
        return;
    }

    RouteEntry re = getRoute(rrep.originNodeID);
    RREPHeader newRrep = rrep; // need to increment the number of hops in rrep header as per note
    newRrep.numHops++;         // increment number of hops
    /*
    The above is an initial version
    - We do not currently save the path taken by each packet
    - Therefore, we can not guarentee the rrep will follow rreq route therefore, we assume
      that since the packet found a route to the note it will find a route back to originator of rreq
    - This is a simple method but should suffice for this application.
    */

    BaseHeader fwdBase = base;
    fwdBase.destNodeID = re.nextHop;
    fwdBase.srcNodeID = _myNodeID;
    fwdBase.packetType = PKT_RREP;
    fwdBase.hopCount++;

    transmitPacket(fwdBase, (uint8_t *)&newRrep, sizeof(RREPHeader));
}

void AODVRouter::handleRERR(const BaseHeader &base, const uint8_t *payload, size_t payloadLen)
{
    Serial.printf("[AODVRouter] RERR payload size %u \n", (unsigned)payloadLen);
    Serial.printf("[AODVRouter] RERR expected payload size %u \n", sizeof(RERRHeader));
    if (payloadLen < sizeof(RERRHeader))
    {
        Serial.println("[AODVRouter] RERR payload too small");
        return;
    }

    RERRHeader rerr;
    memcpy(&rerr, payload, sizeof(RERRHeader));

    if (rerr.brokenNodeID != rerr.reporterNodeID)
    {
        invalidateRoute(rerr.brokenNodeID, rerr.originalDestNodeID, base.srcNodeID);
    }
    else
    {
        // TODO: crude implementation for now we just remove the route to the original DestinationNode
        _routeTable.erase(rerr.originalDestNodeID);
    }

    if (_myNodeID == rerr.senderNodeID)
    {
        // I am the original creator of the message
        Serial.printf("[AODVRouter] Message %u failed to send received RERR.", rerr.originalPacketID);
        return;
    }

    // is there a route to the original sender
    if (!hasRoute(rerr.senderNodeID))
    {
        Serial.println("[AODVRouter] Failed to deliver RERR to original sender");
        return;
    }

    // forward the rerr to the original sender as there is a route
    RouteEntry re = getRoute(rerr.senderNodeID);

    // no change to the rerr as that remains static

    BaseHeader fwdBase = base;
    fwdBase.destNodeID = re.nextHop;
    fwdBase.srcNodeID = _myNodeID;
    fwdBase.packetType = PKT_RERR;
    fwdBase.hopCount++;

    transmitPacket(fwdBase, (uint8_t *)&rerr, sizeof(RERRHeader));
}

void AODVRouter::handleData(const BaseHeader &base, const uint8_t *payload, size_t payloadLen)
{
    if (payloadLen < sizeof(DATAHeader))
    {
        Serial.println("[AODVRouter] DATA payload too small");
        return;
    }

    DATAHeader dataHeader;
    memcpy(&dataHeader, payload, sizeof(DATAHeader));

    const uint8_t *actualData = payload + sizeof(DATAHeader);
    size_t actualDataLen = payloadLen - sizeof(DATAHeader);

    if (_myNodeID == dataHeader.finalDestID)
    {
        Serial.println("[AODVRouter] Entered I am receiver path");
        // TODO: need to properly extract the data without the header
        Serial.printf("[AODVRouter] Received DATA for me. PayloadLen=%u\n", (unsigned)payloadLen);
        Serial.printf("Data: %.*s\n", (int)actualDataLen, (const char *)actualData);
        return;
    }

    if (!hasRoute(dataHeader.finalDestID))
    {
        Serial.printf("[AODVRouter] No route to forward data to %u, dropping.\n", dataHeader.finalDestID);
        // special case where node self reports broken link therefore brokenNodeID == originNodeID
        sendRERR(_myNodeID, base.srcNodeID, dataHeader.finalDestID, base.packetID);
        // could just fold the message in data buffer and send a RREQ -> this is probably the best solution
        return;
    }

    RouteEntry re = getRoute(dataHeader.finalDestID);

    BaseHeader fwd = base;
    fwd.destNodeID = re.nextHop;
    fwd.hopCount++;
    fwd.packetType = PKT_DATA;
    // we don't change src node in data message because we should not be learning
    // any new routes at this point

    transmitPacket(fwd, (uint8_t *)&dataHeader, sizeof(DATAHeader), actualData, actualDataLen);
}

// HELPER FUNCTIONS: sendRREQ, sendRREP, sendRERR
// TODO: Need to complete

void AODVRouter::sendRREQ(uint32_t destNodeID)
{
    BaseHeader bh;
    bh.destNodeID = BROADCAST_ADDR;
    bh.srcNodeID = _myNodeID;
    bh.packetID = esp_random();
    bh.packetType = PKT_RREQ;
    bh.flags = 0;
    bh.hopCount = 0;
    bh.reserved = 0;

    RREQHeader rreq;
    rreq.originNodeID = _myNodeID;
    rreq.RREQDestNodeID = destNodeID;

    transmitPacket(bh, (uint8_t *)&rreq, sizeof(RREQHeader));
}

void AODVRouter::sendRREP(uint32_t originNodeID, uint32_t destNodeID, uint32_t nextHop, uint8_t hopCount)
{
    BaseHeader bh;
    bh.destNodeID = nextHop;
    bh.srcNodeID = _myNodeID;
    bh.packetID = esp_random();
    bh.packetType = PKT_RREP;
    bh.flags = 0;
    bh.hopCount = 0;
    bh.reserved = 0;

    RREPHeader rrep;
    rrep.originNodeID = originNodeID; // node that originally needed the route
    rrep.RREPDestNodeID = destNodeID; // destination of the route
    rrep.lifetime = 0;
    rrep.numHops = hopCount;

    transmitPacket(bh, (uint8_t *)&rrep, sizeof(RREPHeader));
}

void AODVRouter::sendRERR(uint32_t brokenNodeID, uint32_t senderNodeID, uint32_t originalDest, uint32_t originalPacketID)
{
    BaseHeader bh;
    bh.destNodeID = BROADCAST_ADDR; // or unicast to original sender
    bh.srcNodeID = _myNodeID;
    bh.packetID = esp_random();
    bh.packetType = PKT_RERR;
    bh.flags = 0;
    bh.hopCount = 0;
    bh.reserved = 0;

    RERRHeader rerr;
    rerr.reporterNodeID = _myNodeID; // I am reporting the error in the routing path
    rerr.brokenNodeID = brokenNodeID;
    rerr.originalDestNodeID = originalDest;
    rerr.originalPacketID = originalPacketID;
    rerr.senderNodeID = senderNodeID;

    transmitPacket(bh, (uint8_t *)&rerr, sizeof(RERRHeader));
}

// DATA BUFFER HELPER FUNCTIONS
void AODVRouter::flushDataQueue(uint32_t destNodeID)
{
    auto it = _dataBuffer.find(destNodeID);
    if (it != _dataBuffer.end())
    {
        for (auto &pending : it->second)
        {
            RouteEntry re = getRoute(destNodeID);
            BaseHeader bh;
            bh.destNodeID = re.nextHop;
            bh.srcNodeID = _myNodeID;
            bh.packetID = esp_random();
            bh.packetType = PKT_DATA;
            bh.flags = 0;
            bh.hopCount = 0;
            bh.reserved = 0;

            DATAHeader dh;
            dh.finalDestID = destNodeID;

            transmitPacket(bh, (uint8_t *)&dh, sizeof(DATAHeader), pending.data, pending.length);
            // Free the memory after transmitting.
            vPortFree(pending.data);
        }
        _dataBuffer.erase(it);
    }
}

// TRANSMIT PACKET
void AODVRouter::transmitPacket(const BaseHeader &header, const uint8_t *extHeader, size_t extLen,
                                const uint8_t *payload, size_t payloadLen)
{
    uint8_t buffer[255];
    size_t offset = 0;
    offset += serialiseBaseHeader(header, buffer + offset);

    // Append the extension header, if provided.
    if (extHeader && extLen > 0)
    {
        if (offset + extLen > sizeof(buffer))
        {
            Serial.println("[AODVRouter] transmitPacket buffer overflow with extension header!");
            return;
        }
        memcpy(buffer + offset, extHeader, extLen);
        offset += extLen;
    }

    // Append the payload, if provided.
    if (payload && payloadLen > 0)
    {
        if (offset + payloadLen > sizeof(buffer))
        {
            Serial.println("[AODVRouter] transmitPacket buffer overflow with payload!");
            return;
        }
        memcpy(buffer + offset, payload, payloadLen);
        offset += payloadLen;
    }

    // Now send the complete packet to the radio manager.
    bool queued = _radioManager->enqueueTxPacket(buffer, offset);
    if (!queued)
    {
        Serial.println("[AODVRouter] transmitPacket could not enqueue!");
    }
}

// ROUTING TABLE HELPER FUNCTIONS

void AODVRouter::updateRoute(uint32_t destination, uint32_t nextHop, uint8_t hopCount)
{
    auto it = _routeTable.find(destination);
    if (it == _routeTable.end())
    {
        // new route
        RouteEntry re{nextHop, hopCount};
        _routeTable[destination] = re;
        Serial.printf("[AODVRouter] Added route to %u via %u, hopCount=%u\n", destination, nextHop, hopCount);
    }
    else
    {
        // possibly update if shorter
        if (hopCount < it->second.hopcount)
        {
            it->second.nextHop = nextHop;
            it->second.hopcount = hopCount;
            Serial.printf("[AODVRouter] Updated route to %u via %u, hopCount=%u\n", destination, nextHop, hopCount);
        }
    }
}

bool AODVRouter::hasRoute(uint32_t destination)
{
    return (_routeTable.find(destination) != _routeTable.end());
}

RouteEntry AODVRouter::getRoute(uint32_t destination)
{
    return _routeTable[destination];
}

void AODVRouter::invalidateRoute(uint32_t brokenNodeID, uint32_t finalDestNodeID, uint32_t senderNodeID)
{
    // remove any routes to the broken node
    _routeTable.erase(brokenNodeID);

    // Remove any routes that have the brokenNode as the nextHop
    for (auto it = _routeTable.begin(); it != _routeTable.end();)
    {
        if (it->second.nextHop == brokenNodeID)
        {
            it = _routeTable.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // TODO: IMPORTANT need to actually remove route to finalDestination
    // The issue is because we do not know the route we took with this message
    // we have to make some assumptions about the current routes that everyone has stored
    // Need to assume that everyones routeTable is unchanged.

    // TODO: Insimulation also remove destination if it goes through the sender (omitted in this case)
}
