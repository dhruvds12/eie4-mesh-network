#include "AODVRouter.h"
#include <Arduino.h>
#include "gatewayManager.h"

static const uint8_t MAX_HOPS = 5; // TODO: need to adjusted

AODVRouter::AODVRouter(IRadioManager *radioManager, MQTTManager *MQTTManager, uint32_t myNodeID, UserSessionManager *usm, IClientNotifier *icm)
    : _radioManager(radioManager), _mqttManager(MQTTManager), _myNodeID(myNodeID), _routerTaskHandler(nullptr), _usm(usm), _clientNotifier(icm)
{
}

// TODO: can the ifdef be removed?
bool AODVRouter::begin()
{
#ifdef UNIT_TEST
    // For unit tests, skip task creation and assume initialisation is successful.
    // Serial.println("[AODVRouter] Run Begin");
    // Send broadcast message
    sendBroadcastInfo();
    return true;
#else
    _mutex = xSemaphoreCreateRecursiveMutex();
    configASSERT(_mutex);
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

    sendBroadcastInfo();

    // Create the timer‐worker task
    xTaskCreate(
        timerWorkerTask,
        "TimerWorker",
        4096,
        this,
        3,
        &_timerWorkerHandle);

    /*

    Create a software timer to send the broadcast every hour.

    FreeRTOS is written in C, therefore the callback is declared static as it cannot be a
    member function. As it is not a member function it does not have a this pointer. Therefore,
    to ensure the pvTimerID is set to the pointer to this
    */
    _broadcastTimer = xTimerCreate(
        "BroadcastTimer",
        pdMS_TO_TICKS(60000),    // 900000ms = 15 minute
        pdTRUE,                  // Auto-reload for periodic execution
        (void *)this,            // Pass the current router instance as timer ID
        broadcastTimerCallback); // Callback to send broadcast info

    if (_broadcastTimer == nullptr)
    {
        Serial.println("[AODVRouter] Failed to create broadcast timer");
    }
    else
    {
        if (xTimerStart(_broadcastTimer, 0) != pdPASS)
        {
            Serial.println("[AODVRouter] Failed to start broadcast timer");
        }
    }

    _ackBufferCleanupTimer = xTimerCreate(
        "AckCleanupTimer",
        ACK_CLEANUP_PERIOD_TICKS, // runs every minute
        pdTRUE,                   // Auto-reload for periodic execution
        (void *)this,             // Pass the current router instance as timer ID
        ackCleanupCallback        // Callback to perform ack cleanup
    );

    if (_ackBufferCleanupTimer == nullptr)
    {
        Serial.println("[AODVRouter] Failed to create ack cleanup timer");
    }
    else
    {
        if (xTimerStart(_ackBufferCleanupTimer, 0) != pdPASS)
        {
            Serial.println("[AODVRouter] Failed to start ack cleanup timer");
        }
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

// worker task simply waits for notifications and dispatches
void AODVRouter::timerWorkerTask(void *pv)
{
    AODVRouter *self = static_cast<AODVRouter *>(pv);
    uint32_t bits;
    for (;;)
    {
        // Block until either bit is set
        xTaskNotifyWait(
            0,           // clear on entry
            0xFFFFFFFFu, // clear on exit
            &bits,
            portMAX_DELAY);

        if (bits & BROADCAST_NOTIFY_BIT)
            self->sendBroadcastInfo();

        if (bits & CLEANUP_NOTIFY_BIT)
            self->cleanupAckBuffer();
    }
}
#endif

void AODVRouter::sendBroadcastInfo()
{
    BaseHeader bh;
    bh.destNodeID = BROADCAST_ADDR; // Broadcast to all nodes
    bh.srcNodeID = _myNodeID;
    bh.packetID = esp_random();
    bh.packetType = PKT_BROADCAST_INFO; // Use broadcast packet type
    bh.flags = 0;
    bh.hopCount = 0;
    bh.reserved = 0;

    std::vector<uint32_t> added, removed;
    _usm->getAndClearDiff(added, removed);

    constexpr size_t MAX_BUF = 255;
    constexpr size_t BASE_HDR = sizeof(BaseHeader);
    constexpr size_t DIFF_HDR = sizeof(DiffBroadcastInfoHeader);
    constexpr size_t SPACE = MAX_BUF - BASE_HDR - DIFF_HDR;
    constexpr size_t IDS_PER_PKT = SPACE / sizeof(uint32_t);

    size_t idxA = 0, idxR = 0;
    if (added.empty() && removed.empty())
    {
        DiffBroadcastInfoHeader dh;
        dh.numAdded = 0;
        dh.numRemoved = 0;
        dh.originNodeID = _myNodeID;

        transmitPacket(bh, reinterpret_cast<const uint8_t *>(&dh), DIFF_HDR);
    }
    else
    {

        while (idxA < added.size() || idxR < removed.size())
        {

            size_t numA = std::min(IDS_PER_PKT, added.size() - idxA);
            size_t remaining = IDS_PER_PKT - numA;
            size_t numR = remaining == 0 ? 0 : std::min(remaining, removed.size() - idxR);

            DiffBroadcastInfoHeader dh;
            dh.numAdded = uint16_t(numA);
            dh.numRemoved = uint16_t(numR);
            dh.originNodeID = _myNodeID;

            std::vector<uint8_t> payload;
            payload.reserve((numA + numR) * sizeof(uint32_t));

            // first the added IDs
            for (size_t i = 0; i < numA; ++i)
            {
                uint32_t uid = added[idxA + i];
                auto p = reinterpret_cast<const uint8_t *>(&uid);
                payload.insert(payload.end(), p, p + sizeof(uid));
            }
            // then the removed IDs
            for (size_t i = 0; i < numR; ++i)
            {
                uint32_t uid = removed[idxR + i];
                auto p = reinterpret_cast<const uint8_t *>(&uid);
                payload.insert(payload.end(), p, p + sizeof(uid));
            }

            transmitPacket(
                bh,
                reinterpret_cast<const uint8_t *>(&dh), DIFF_HDR,
                payload.data(), payload.size());

            idxA += numA;
            idxR += numR;
        }
    }
    Serial.println("Send broadcastInfo");
}

void AODVRouter::cleanupAckBuffer()
{
    struct Expired
    {
        ackBufferEntry entry;
        uint32_t packetID;
    };
    // Create a temporary list of packetIDs to remove.
    std::vector<Expired> expired;
    {
        Lock l(_mutex);
        TickType_t now = xTaskGetTickCount();
        for (auto it = ackBuffer.begin(); it != ackBuffer.end();)
        {
            if (now - it->second.timestamp >= ACK_TIMEOUT_TICKS)
            {
                expired.push_back({it->second, it->first});
                it = ackBuffer.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    // Process all expired entries.
    for (auto &e : expired)
    {
        // Retrieve the stored ack entry.
        ackBufferEntry &abe = e.entry;

        // Deserialize the BaseHeader from the stored packet.
        BaseHeader baseHdr;
        size_t offset = deserialiseBaseHeader(abe.packet, baseHdr);

        // Handle DATA packets (you can add similar logic for other packet types)
        if (baseHdr.packetType == PKT_DATA)
        {
            DATAHeader dataHdr;
            offset = deserialiseDATAHeader(abe.packet, dataHdr, offset);

            // Trigger a Route Error message for the unacknowledged packet.
            // Here, we assume the broken node is the expected next hop.
            sendRERR(abe.expectedNextHop, baseHdr.srcNodeID, dataHdr.finalDestID, baseHdr.packetID);
        }
        else if (baseHdr.packetType == PKT_RREP)
        {
            // For RREP packets, extract the relevant header information.
            RREPHeader rrep;
            memcpy(&rrep, abe.packet + offset, sizeof(RREPHeader));
            sendRERR(abe.expectedNextHop, baseHdr.srcNodeID, rrep.RREPDestNodeID, baseHdr.packetID);
        }
        vPortFree(abe.packet);
    }
}

#ifndef UNIT_TEST
void AODVRouter::broadcastTimerCallback(TimerHandle_t xTimer)
{
    AODVRouter *self = (AODVRouter *)pvTimerGetTimerID(xTimer);
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xTaskNotifyFromISR(
        self->_timerWorkerHandle,
        BROADCAST_NOTIFY_BIT,
        eSetBits,
        &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void AODVRouter::ackCleanupCallback(TimerHandle_t xTimer)
{
    AODVRouter *self = (AODVRouter *)pvTimerGetTimerID(xTimer);
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xTaskNotifyFromISR(
        self->_timerWorkerHandle,
        CLEANUP_NOTIFY_BIT,
        eSetBits,
        &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
#endif

void AODVRouter::sendData(uint32_t destNodeID, const uint8_t *data, size_t len, uint8_t flags)
{
    BaseHeader bh;
    RouteEntry re;
    if (destNodeID != BROADCAST_ADDR)
    {
        if (!getRoute(destNodeID, re))
        {
            Serial.printf("[AODVRouter] No route for %u, sending RREQ.\n", destNodeID);

            uint8_t *copy = (uint8_t *)pvPortMalloc(len);
            if (copy)
            {
                memcpy(copy, data, len);
                insertDataBuffer(destNodeID, copy, len);
            }
            else
            {
                Serial.println("[AODVRouter] Memory allocation failed for data buffer");
            }

            sendRREQ(destNodeID);
            return;
        }
        bh.destNodeID = re.nextHop;
    }
    else
    {
        bh.destNodeID = BROADCAST_ADDR;
    }

    bh.srcNodeID = _myNodeID;
    bh.packetID = (uint32_t)(esp_random()); // TODO: Might need to improve random number generation
    bh.packetType = PKT_DATA;
    bh.flags = flags; // No flags set ie no ACK etc expected
    bh.hopCount = 0;
    bh.reserved = 0;

    DATAHeader dh;
    dh.finalDestID = destNodeID;

    transmitPacket(bh, (uint8_t *)&dh, sizeof(DATAHeader), data, len);
}

void AODVRouter::sendUserMessage(uint32_t fromUserID, uint32_t toUserID, const uint8_t *message, size_t len, uint8_t flags)
{
    Serial.printf("Creating user message to %u from %u\n", toUserID, fromUserID);
    // check GUT
    GutEntry ge;
    if (!getGutEntry(toUserID, ge))
    {
        uint8_t *copy = (uint8_t *)pvPortMalloc(len);
        if (!copy)
            return; // OOM
        memcpy(copy, message, len);
        userMessageBufferEntry buffer;
        buffer.senderID = fromUserID;
        buffer.message = copy;
        buffer.length = len;
        addUserMessage(toUserID, buffer);

        sendUREQ(toUserID);
        return;
    }

    RouteEntry re;

    if (!getRoute(ge.nodeID, re))
    {
        uint8_t *copy = (uint8_t *)pvPortMalloc(len);
        if (!copy)
            return; // OOM
        memcpy(copy, message, len);
        Serial.printf("[AODVRouter] SendUserMessage No route for %u, sending RREQ.\n", ge.nodeID);
        PendingUserRouteEntry entry;
        entry.destUserID = toUserID;
        entry.senderID = fromUserID;
        entry.message = copy;
        entry.length = len;

        addPendingUserRouteMessage(ge.nodeID, entry);

        sendRREQ(ge.nodeID);
        return;
    }

    BaseHeader bh;
    bh.destNodeID = re.nextHop;
    bh.srcNodeID = _myNodeID;
    bh.packetID = (uint32_t)(esp_random()); // TODO: Might need to improve random number generation
    bh.packetType = PKT_USER_MSG;
    bh.flags = 0; // No flags set ie no ACK etc expected
    bh.hopCount = 0;
    bh.reserved = 0;

    UserMsgHeader umh{};
    umh.fromUserID = fromUserID;
    umh.toUserID = toUserID;
    umh.toNodeID = ge.nodeID;

    Serial.printf(
        "Creating user to user message: from user %u to user %u (node %u)\n",
        umh.fromUserID,
        umh.toUserID,
        umh.toNodeID);

    transmitPacket(bh, (uint8_t *)&umh, sizeof(UserMsgHeader), message, len);
}

void AODVRouter::handlePacket(RadioPacket *rxPacket)
{
    if (rxPacket->len < sizeof(BaseHeader))
    {
        Serial.printf("length of packet %u\n", rxPacket->len);
        Serial.println("[AODVRouter] Received packet with size less than baseheader size. Discarded");
        return;
    }

    BaseHeader bh;
    deserialiseBaseHeader(rxPacket->data, bh);

    if (tryImplicitAck(bh.packetID))
        return;

    if (isDuplicatePacketID(bh.packetID))
    {
        Serial.println("[AODVRouter] Received packet which has already been processed");
        return;
    }

    storePacketID(bh.packetID);

    if (bh.srcNodeID == _myNodeID)
    {
        Serial.println("[AODVRouter] Reveived packet with srcNodeID == myNodeID. Not expected behaviour! Unless I sent a broadcast");
        return;
    }

    if (bh.destNodeID != BROADCAST_ADDR && bh.destNodeID != _myNodeID)
    {
        Serial.printf("[AODVRouter] Not a message for me bh.destnodeid: %u\n", bh.destNodeID);
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
        handleData(bh, payload, payloadLen);
        break;
    case PKT_BROADCAST:
        Serial.println("Received broadcast");
        break;
    case PKT_BROADCAST_INFO:
        handleBroadcastInfo(bh, payload, payloadLen);
        break;
    case PKT_ACK:
        Serial.println("Received ACK");
        break;
    case PKT_UERR:
        handleUERR(bh, payload, payloadLen);
        break;
    case PKT_UREP:
        handleUREP(bh, payload, payloadLen);
        break;
    case PKT_UREQ:
        handleUREQ(bh, payload, payloadLen);
        break;
    case PKT_USER_MSG:
        handleUserMessage(bh, payload, payloadLen);
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

    RouteEntry re;
    if (getRoute(rreq.RREQDestNodeID, re))
    {
        Serial.printf("[AODVRouter] I have a route to %u, so I'll send RREP back to %u.\n", rreq.RREQDestNodeID, rreq.originNodeID);
        // There is a route to the node, therefore use the entry as the base number of hops
        sendRREP(rreq.originNodeID, rreq.RREQDestNodeID, base.srcNodeID, re.hopcount);
        return;
    }
    Serial.println("[AODVRouter] Forwading RREQ");

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
        Serial.printf("Flushing data queue for ID: %d", rrep.RREPDestNodeID);
        flushDataQueue(rrep.RREPDestNodeID);
    }

    // If I initially sent the RREQ no need to continue forwarding
    if (_myNodeID == rrep.originNodeID)
    {
        Serial.println("[AODVRouter] Got RREP for rreq");
        return;
    }

    RouteEntry re;
    if (!getRoute(rrep.originNodeID, re))
    {
        Serial.println("[AODVRouter] Got RREP but no route to the origin!");
        return;
    }

    Serial.println("[AODVRouter] Forwading RREP");
    RREPHeader newRrep = rrep; // need to increment the number of hops in rrep header as per note
    newRrep.numHops++;         // increment number of hops
    // TODO: is this required already have a hopCount?????
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
        removeItemRoutingTable(rerr.originalDestNodeID);
        if (_mqttManager->connected)
            _mqttManager->publishInvalidateRoute(rerr.originalDestNodeID);
    }

    if (_myNodeID == rerr.senderNodeID)
    {
        // I am the original creator of the message
        Serial.printf("[AODVRouter] Message %u failed to send received RERR.\n", rerr.originalPacketID);
        return;
    }

    RouteEntry re;
    // is there a route to the original sender
    if (!getRoute(rerr.senderNodeID, re))
    {
        Serial.println("[AODVRouter] Failed to deliver RERR to original sender ");
        return;
    }
    Serial.println("[AODVRouter] Forwading RERR");
    // forward the rerr to the original sender as there is a route
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
        Serial.printf("[AODVRouter] Data: %.*s\n", (int)actualDataLen, (const char *)actualData);
        _clientNotifier->notify(Outgoing{BleType::BLE_Node, dataHeader.finalDestID, base.srcNodeID, actualData, actualDataLen});
        return;
    }

    BaseHeader fwd = base;
    if (dataHeader.finalDestID == BROADCAST_ADDR)
    {
        // TODO: need to stop the broadcast at some point
        Serial.println("[AODVRouter] Entered I am received BROADCAST DATA path");
        // TODO: need to properly extract the data without the header
        Serial.printf("[AODVRouter] Received DATA for me. PayloadLen=%u\n", (unsigned)payloadLen);
        Serial.printf("[AODVRouter] Data: %.*s\n", (int)actualDataLen, (const char *)actualData);
        _clientNotifier->notify(Outgoing{BleType::BLE_Broadcast, 0, 0, actualData, actualDataLen});
        fwd.destNodeID = BROADCAST_ADDR;
    }
    else
    {
        RouteEntry re;

        if (!getRoute(dataHeader.finalDestID, re))
        {
            Serial.printf("[AODVRouter] No route to forward data to %u, dropping.\n", dataHeader.finalDestID);
            // special case where node self reports broken link therefore brokenNodeID == originNodeID
            sendRERR(_myNodeID, base.srcNodeID, dataHeader.finalDestID, base.packetID);
            // could just fold the message in data buffer and send a RREQ -> this is probably the best solution
            return;
        }

        fwd.destNodeID = re.nextHop;
    }

    Serial.println("[AODVRouter] Forwading Data");

    fwd.hopCount++;
    fwd.packetType = PKT_DATA;
    // we don't change src node in data message because we should not be learning
    // any new routes at this point

    transmitPacket(fwd, (uint8_t *)&dataHeader, sizeof(DATAHeader), actualData, actualDataLen);
}

void AODVRouter::handleBroadcastInfo(const BaseHeader &base, const uint8_t *payload, size_t payloadLen)
{
    if (payloadLen < sizeof(DiffBroadcastInfoHeader))
    {
        Serial.println("[AODVRouter] BroadcastInfo too small");
        return;
    }

    DiffBroadcastInfoHeader dh;
    memcpy(&dh, payload, sizeof(BROADCASTINFOHeader));

    if (!isNodeIDKnown(base.srcNodeID))
    {
        saveNodeID(base.srcNodeID);
    }

    if (!isNodeIDKnown(dh.originNodeID))
    {
        saveNodeID(dh.originNodeID);
    }

    Serial.printf("[AODVRouter] Received BroadcastInfo. PayloadLen=%u\n", (unsigned)payloadLen);
    Serial.printf("[AODVRouter] Info: %.*s\n", (int)payloadLen, (const char *)payload);

    // update the routing table
    updateRoute(base.srcNodeID, base.srcNodeID, 1);

    // update route to the originNode
    if (dh.originNodeID != _myNodeID)
    {

        updateRoute(dh.originNodeID, base.srcNodeID, base.hopCount + 1);
    }

    size_t offset = sizeof(DiffBroadcastInfoHeader);
    for (size_t i = 0; i < dh.numAdded; ++i)
    {
        if (offset + sizeof(uint32_t) > payloadLen)
            break;
        uint32_t uid;
        memcpy(&uid, payload + offset, sizeof(uid));
        offset += sizeof(uid);

        GutEntry ge;
        ge.nodeID = dh.originNodeID;
        ge.seq = 0; // or carry over if you have per‑user seqnums
        ge.ts = 0;  // timestamp now
        updateGutEntry(uid, ge);
    }

    for (size_t i = 0; i < dh.numRemoved; ++i)
    {
        if (offset + sizeof(uint32_t) > payloadLen)
            break;
        uint32_t uid;
        memcpy(&uid, payload + offset, sizeof(uid));
        offset += sizeof(uid);

        removeGutEntry(uid);
    }

    Serial.println("[AODVRouter] Forwading BroadcastINFO");
    // increment number of hops
    BaseHeader fwd = base;
    fwd.srcNodeID = _myNodeID; // replace your node id and store the originNodeID in the bih header
    fwd.hopCount++;

    // check number of hops
    if (fwd.hopCount >= MAX_HOPS)
    {
        Serial.println("[AODVRouter] Exceeded max hops broadcast info stopped");
        return;
    }

    // forward the message, keep src id as the same
    transmitPacket(fwd, reinterpret_cast<const uint8_t *>(&dh), sizeof(dh), payload + sizeof(dh), payloadLen - sizeof(dh));
}

void AODVRouter::handleUserMessage(const BaseHeader &base, const uint8_t *payload, size_t payloadLen)
{
    UserMsgHeader umh;
    size_t offset = deserialiseUserMsgHeader(payload, umh, 0);

    const uint8_t *message = payload + offset;
    size_t messageLen = payloadLen - offset;

    if ((_myNodeID == umh.toNodeID) && (base.flags & TO_GATEWAY))
    {
        if (_gwMgr) // forward to GatewayManager
            _gwMgr->uplink(umh.fromUserID, umh.toUserID,
                           (const char *)message);
        return; // stop normal routing
    }

    if (_myNodeID == umh.toNodeID)
    {
        if (base.flags & FROM_GATEWAY)
        {
            Serial.println("[AODVRouter] Received gateway user message");
            Serial.printf("[AODVRouter] Received USER Message for %u. PayloadLen=%u\n", umh.toUserID, (unsigned)payloadLen);
            Serial.printf("[AODVRouter] Data: %.*s\n", (int)messageLen, (const char *)message);
            _clientNotifier->notify(Outgoing{BleType::BLE_GATEWAY, umh.toUserID, umh.fromUserID, message, messageLen});
            return;
        }
        Serial.println("[AODVRouter] Entered I am receiver path User Message");
        // TODO: need to properly extract the data without the header
        Serial.printf("[AODVRouter] Received USER Message for %u. PayloadLen=%u\n", umh.toUserID, (unsigned)payloadLen);
        Serial.printf("[AODVRouter] Data: %.*s\n", (int)messageLen, (const char *)message);
        _clientNotifier->notify(Outgoing{BleType::BLE_UnicastUser, umh.toUserID, umh.fromUserID, message, messageLen});
        return;
    }

    RouteEntry re;

    if (!getRoute(umh.toNodeID, re))
    {
        Serial.printf("[AODVRouter] No route to forward data to %u, dropping.\n", umh.toNodeID);
        // special case where node self reports broken link therefore brokenNodeID == originNodeID
        sendRERR(_myNodeID, base.srcNodeID, umh.toNodeID, base.packetID);
        // could just fold the message in data buffer and send a RREQ -> this is probably the best solution
        return;
    }
    Serial.println("[AODVRouter] Forwading Data");

    BaseHeader fwd = base;
    fwd.destNodeID = re.nextHop;
    fwd.hopCount++;
    fwd.packetType = PKT_USER_MSG;

    transmitPacket(fwd, (uint8_t *)&umh, sizeof(UserMsgHeader), message, messageLen);
}

void AODVRouter::handleUREQ(const BaseHeader &base, const uint8_t *payload, size_t payloadlen)
{
    UREQHeader ureq;
    deserialiseUREQHeader(payload, ureq, 0);

    updateRoute(base.srcNodeID, base.srcNodeID, 1);
    updateRoute(ureq.originNodeID, base.srcNodeID, base.hopCount + 1);

    if (_usm->knowsUser(ureq.userID))
    {
        sendUREP(ureq.originNodeID, _myNodeID, ureq.userID, base.srcNodeID, 0, base.hopCount + 1);
        return;
    }

    GutEntry ge;
    if (getGutEntry(ureq.userID, ge))
    {

        RouteEntry re;
        if (getRoute(ge.nodeID, re))
        {

            sendUREP(ureq.originNodeID, _myNodeID, ureq.userID, base.srcNodeID, 0, base.hopCount + 1 + re.hopcount);
            return;
        }
    }

    // Forward UREQ
    BaseHeader fwd = base;
    fwd.srcNodeID = _myNodeID;
    fwd.hopCount++;

    transmitPacket(fwd, (uint8_t *)&ureq, sizeof(UREPHeader));
}

void AODVRouter::handleUREP(const BaseHeader &base, const uint8_t *payload, size_t payloadlen)
{
    UREPHeader urep;
    deserialiseUREPHeader(payload, urep, 0);

    updateRoute(base.srcNodeID, base.srcNodeID, 1);
    updateRoute(urep.destNodeID, base.srcNodeID, base.hopCount + 1);
    GutEntry ge;
    ge.nodeID = urep.destNodeID;
    ge.seq = 0; // TODO: will need to be changed to actually handle seq number
    ge.ts = 0;
    updateGutEntry(urep.userID, ge);

    if (_myNodeID == urep.originNodeID)
    {
        if (hasBufferedUserMessages(urep.userID))
        {
            flushUserRouteBuffer(urep.userID);
        }

        return;
    }

    RouteEntry re;
    if (!getRoute(urep.originNodeID, re))
    {
        Serial.println("[AODVRouter] Got RREP but no route to the origin!");
        return;
    }

    Serial.println("[AODVRouter] Forwading UREP");
    BaseHeader fwd = base;
    fwd.destNodeID = re.nextHop;
    fwd.srcNodeID = _myNodeID;
    fwd.hopCount++;

    transmitPacket(fwd, (uint8_t *)&urep, sizeof(UREPHeader));
}

void AODVRouter::handleUERR(const BaseHeader &base, const uint8_t *payload, size_t payloadlen)
{
    UERRHeader uerr;
    deserialiseUERRHeader(payload, uerr, 0);

    updateRoute(base.srcNodeID, base.srcNodeID, 1);

    // everyone should note the change in user location, need to check that the gut entry is for that node
    GutEntry ge;
    if (getGutEntry(uerr.userID, ge))
    {
        if (ge.nodeID == uerr.nodeID)
        {
            // or invalidate??
            removeGutEntry(uerr.userID);
        }
    }

    if (_myNodeID == uerr.originNodeID)
    {
        /* TODO: REMOVE FROM REQUIRED ACK LIST
        If this packet required an ack remove as the route to the node was ok just the user was
        not there however, user to user messages usually are NOT acked
        */
        return;
    }

    RouteEntry re;
    // is there a route to the original sender
    if (!getRoute(uerr.originNodeID, re))
    {
        Serial.println("[AODVRouter] Failed to deliver RERR to original sender ");
        return;
    }
    Serial.println("[AODVRouter] Forwading RERR");
    // forward the rerr to the original sender as there is a route
    // no change to the rerr as that remains static

    BaseHeader fwdBase = base;
    fwdBase.destNodeID = re.nextHop;
    fwdBase.srcNodeID = _myNodeID;
    fwdBase.hopCount++;

    transmitPacket(fwdBase, (uint8_t *)&uerr, sizeof(UERRHeader));
}
// HELPER FUNCTIONS: sendRREQ, sendRREP, sendRERR

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
    rreq.originNodeID = _myNodeID;    // Person who request the route request
    rreq.RREQDestNodeID = destNodeID; // ID of node route required for

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
    bh.destNodeID = senderNodeID; // or unicast to original sender
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

void AODVRouter::sendUREQ(uint32_t userID)
{
    BaseHeader bh;
    bh.destNodeID = BROADCAST_ADDR;
    bh.srcNodeID = _myNodeID;
    bh.packetID = esp_random();
    bh.packetType = PKT_UREQ;
    bh.flags = 0;
    bh.hopCount = 0;
    bh.reserved = 0;

    UREQHeader ureq;
    ureq.originNodeID = _myNodeID;
    ureq.userID = userID;

    transmitPacket(bh, (uint8_t *)&ureq, sizeof(UREQHeader));
}

void AODVRouter::sendUREP(uint32_t originNodeID, uint32_t destNodeID, uint32_t userID, uint32_t nextHop, uint16_t lifetime, uint8_t hopCount)
{
    BaseHeader bh;
    bh.destNodeID = nextHop;
    bh.srcNodeID = _myNodeID;
    bh.packetID = esp_random();
    bh.packetType = PKT_UREP;
    bh.flags = 0;
    bh.hopCount = 0;
    bh.reserved = 0;

    UREPHeader urep;
    urep.destNodeID = destNodeID;
    urep.originNodeID = originNodeID;
    urep.numHops = hopCount;
    urep.lifetime = lifetime;
    urep.userID = userID;

    transmitPacket(bh, (uint8_t *)&urep, sizeof(UREPHeader));
}

void AODVRouter::sendUERR(uint32_t userID, uint32_t nodeID, uint32_t originNodeID, uint32_t originalPacketID, uint32_t nextHop)
{
    BaseHeader bh;
    bh.destNodeID = nextHop; // or unicast to original sender
    bh.srcNodeID = _myNodeID;
    bh.packetID = esp_random();
    bh.packetType = PKT_UERR;
    bh.flags = 0;
    bh.hopCount = 0;
    bh.reserved = 0;

    UERRHeader uerr;
    uerr.nodeID = nodeID;
    uerr.originalPacketID = originalPacketID;
    uerr.originNodeID = originNodeID;
    uerr.userID = userID;

    transmitPacket(bh, (uint8_t *)&uerr, sizeof(UERRHeader));
}

// DATA BUFFER HELPER FUNCTIONS
void AODVRouter::flushDataQueue(uint32_t destNodeID)
{

    std::vector<dataBufferEntry> pendingList;
    {
        Lock l(_mutex);
        auto it = _dataBuffer.find(destNodeID);
        if (it != _dataBuffer.end())
        {
            pendingList = std::move(it->second);
            _dataBuffer.erase(it);
        }
    }

    for (auto &pending : pendingList)
    {
        RouteEntry re;
        if (getRoute(destNodeID, re))
        {
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
        else
        {
            // Need to deallocate data in pending AND add back to the data buffer
            insertDataBuffer(destNodeID, pending.data, pending.length);
        }
    }
}

void AODVRouter::flushUserRouteBuffer(uint32_t nodeID)
{
    std::vector<PendingUserRouteEntry> pendingList = popPendingUserRouteMessages(nodeID);

    // Now for each pending user‑message, resend via your existing logic:
    for (auto &entry : pendingList)
    {
        // sendUserMessage will check GUT/route again,
        // retransmit if possible, or re-buffer appropriately.
        sendUserMessage(entry.senderID,
                        entry.destUserID,
                        entry.message,
                        entry.length);
        vPortFree(entry.message);
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
    Serial.printf("[AODVRouer] Added packet with len %u\n", offset);
    if (_mqttManager != nullptr && _mqttManager->connected)
    {
        uint8_t localCopy[255];
        memcpy(localCopy, buffer, offset);
        _mqttManager->publishPacket(header.packetID, localCopy, offset);
    }
    // Now send the complete packet to the radio manager.
    bool queued = _radioManager->enqueueTxPacket(buffer, offset);
    if (!queued)
    {
        Serial.println("[AODVRouter] transmitPacket could not enqueue!");
    }
    else
    {
        // For unicast packets that require an implicit ACK, store a copy in the ackBuffer.
        if (header.destNodeID != BROADCAST_ADDR &&
            (header.packetType == PKT_DATA || header.packetType == PKT_RREP))
        {
            RouteEntry re;
            // Get the expected next hop from the routing table.
            if (getRoute(header.destNodeID, re))
            {
                // Store a copy of the packet along with its metadata.
                storeAckPacket(header.packetID, buffer, offset, re.nextHop);
            }
            // if the get route fails don't store packet details, if for some reason at this stage the route is not found likely transmission will fail due to a broken
            // node BUT too late to stop transmission -> potentially should warn user about a possible failure (NOT REQUIRED FEATURE)
        }
    }
}

// ROUTING TABLE HELPER FUNCTIONS

void AODVRouter::updateRoute(uint32_t destination, uint32_t nextHop, uint8_t hopCount)
{
    Lock l(_mutex);
    auto it = _routeTable.find(destination);
    if (it == _routeTable.end())
    {
        // new route
        RouteEntry re{nextHop, hopCount};
        _routeTable[destination] = re;
        Serial.printf("[AODVRouter] Added route to %u via %u, hopCount=%u\n", destination, nextHop, hopCount);
        if (_mqttManager != nullptr && _mqttManager->connected)
        {
            // send the new routeEntry over mqtt
            _mqttManager->publishUpdateRoute(destination, nextHop, hopCount);
        }
    }
    else
    {
        // possibly update if shorter
        if (hopCount < it->second.hopcount)
        {
            it->second.nextHop = nextHop;
            it->second.hopcount = hopCount;
            Serial.printf("[AODVRouter] Updated route to %u via %u, hopCount=%u\n", destination, nextHop, hopCount);
            if (_mqttManager != nullptr && _mqttManager->connected)
            {
                // send the new routeEntry over mqtt
                _mqttManager->publishUpdateRoute(destination, nextHop, hopCount);
            }
        }
    }
}

bool AODVRouter::hasRoute(uint32_t destination)
{
    Lock l(_mutex);
    return (_routeTable.find(destination) != _routeTable.end());
}

bool AODVRouter::getRoute(uint32_t destination, RouteEntry &routeEntry)
{
    Lock l(_mutex);
    auto it = _routeTable.find(destination);
    if (it == _routeTable.end())
        return false;
    routeEntry = it->second;
    return true;
}

void AODVRouter::invalidateRoute(uint32_t brokenNodeID, uint32_t finalDestNodeID, uint32_t senderNodeID)
{
    std::set<uint32_t> invalidRoute;
    invalidRoute.insert(brokenNodeID);
    invalidRoute.insert(finalDestNodeID);

    {
        Lock l(_mutex);
        // remove any routes to the broken node
        _routeTable.erase(brokenNodeID);
        // Decided to remove route to destination node
        _routeTable.erase(finalDestNodeID);
        // Remove any routes that have the brokenNode as the nextHop
        for (auto it = _routeTable.begin(); it != _routeTable.end();)
        {
            if (it->second.nextHop == brokenNodeID)
            {
                invalidRoute.insert(it->first);
                it = _routeTable.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    // TODO: IMPORTANT need to actually remove route to finalDestination
    // The issue is because we do not know the route we took with this message
    // we have to make some assumptions about the current routes that everyone has stored
    // Need to assume that everyones routeTable is unchanged.

    if (_mqttManager != nullptr && _mqttManager->connected)
    {
        for (uint32_t dest : invalidRoute)
        {
            _mqttManager->publishInvalidateRoute(dest);
        }
    }

    // TODO: Insimulation also remove destination if it goes through the sender (omitted in this case)
}

bool AODVRouter::isDuplicatePacketID(uint32_t packetID)
{
    Lock l(_mutex);
    if (receivedPacketIDs.find(packetID) != receivedPacketIDs.end())
    {
        return true;
    }
    return false;
}

void AODVRouter::storePacketID(uint32_t packetID)
{
    Lock l(_mutex);
    receivedPacketIDs.insert(packetID);
}

bool AODVRouter::isNodeIDKnown(uint32_t packetID)
{
    Lock l(_mutex);
    if (discoveredNodes.find(packetID) != discoveredNodes.end())
    {
        return true;
    }
    return false;
}

void AODVRouter::saveNodeID(uint32_t packetID)
{
    Lock l(_mutex);
    discoveredNodes.insert(packetID);
}

void AODVRouter::storeAckPacket(uint32_t packetID, const uint8_t *packet, size_t length, uint32_t expectedNextHop)
{
    Lock l(_mutex);
    // Allocate memory for a copy of the packet.
    uint8_t *packetCopy = (uint8_t *)pvPortMalloc(length);
    if (packetCopy == nullptr)
    {
        Serial.println("[AODVRouter] Memory allocation failed for ACK packet copy");
        return;
    }
    memcpy(packetCopy, packet, length);

    // Store the packet copy in the ackBuffer along with its metadata.
    ackBuffer[packetID] = {
        packetCopy,
        length,
        expectedNextHop,
        xTaskGetTickCount()};
}

bool AODVRouter::findAckPacket(uint32_t packetID)
{
    Lock l(_mutex);
    if (ackBuffer.find(packetID) != ackBuffer.end())
    {
        return true;
    }
    return false;
}

void AODVRouter::insertDataBuffer(uint32_t destNodeID, uint8_t *data, size_t len)
{
    Lock l(_mutex);
    _dataBuffer[destNodeID].push_back({data, len});
}

bool AODVRouter::ackBufferHasPacketID(uint32_t packetID)
{
    Lock l(_mutex);
    if (ackBuffer.find(packetID) != ackBuffer.end())
    {
        return true;
    }
    return false;
}

void AODVRouter::removeFromACKBuffer(uint32_t packetID)
{
    Lock l(_mutex);
    auto it = ackBuffer.find(packetID);
    if (it != ackBuffer.end())
    {
        // TODO:  verify that the packet came from the expected next hop:
        // if (bh.srcNodeID == it->second.expectedNextHop)
        // {
        //     Serial.printf("[AODVRouter] Implicit ACK received for packet %u\n", bh.packetID);
        //     ackBuffer.erase(it);
        //     return;
        // }

        Serial.printf("[AODVRouter] Implicit ACK received for packet %u\n", packetID);
        vPortFree(it->second.packet);
        ackBuffer.erase(it);
        return;
    }
}

bool AODVRouter::tryImplicitAck(uint32_t packetID)
{
    Lock l(_mutex);
    auto it = ackBuffer.find(packetID);
    if (it == ackBuffer.end())
        return false;
    vPortFree(it->second.packet);
    ackBuffer.erase(it);
    Serial.printf("[AODVRouter] Implicit ACK for %u\n", packetID);
    return true;
}

void AODVRouter::removeItemRoutingTable(uint32_t id)
{
    Lock l(_mutex);
    _routeTable.erase(id);
}

// AODVRouter.cpp (inside class AODVRouter)

std::vector<uint32_t> AODVRouter::getKnownNodeIDs() const
{
    Lock l(_mutex);
    return std::vector<uint32_t>(discoveredNodes.begin(),
                                 discoveredNodes.end());
}

std::vector<uint32_t> AODVRouter::getKnownUserIDs() const
{
    Lock l(_mutex);
    std::vector<uint32_t> users;
    users.reserve(_gut.size());
    for (auto &kv : _gut)
    {
        users.push_back(kv.first);
    }
    return users;
}

/*

Notes:

USER TO USER MESSAGING:
- Each node needs to store the user ids of users that have connected to them
- The then share this list with other nodes at a given period of time

- When nodes join the network they should request information from neighbouring nodes about
    the nodes and users on the network. New nodes are only active when they have this information
    ie they should not send any other messages on the network

- When a user moves from one node to another then the original node needs to be notified this will trigger
    - a reroute of data sent to the original home node of the user
    - a broadcast that will inform other nodes of this change


STORING MESSAGES:

- IF the home node of the user is know message is routed to the home node and stored there

- IF home node of the user is unknown then message is stored locally on sender node until the
    home node of the dest user is found

*/