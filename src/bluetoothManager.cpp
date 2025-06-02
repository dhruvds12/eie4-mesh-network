#include "BluetoothManager.h"
#include <NimBLEDevice.h>
#include "packet.h"

BluetoothManager::BluetoothManager(UserSessionManager *sessionMgr, NetworkMessageHandler *networkHandler, uint32_t nodeID)
    : pServer(nullptr), pService(nullptr), pAdvertising(nullptr), _userMgr(sessionMgr), _netHandler(networkHandler), _serverCallbacks(nullptr), _txCallbacks(nullptr), _rxCallbacks(nullptr), pTxCharacteristic(nullptr), pRxCharacteristic(nullptr), _nodeID(nodeID)
{
    _connectedDevicesMutex = xSemaphoreCreateMutex();
}

BluetoothManager::~BluetoothManager()
{
    delete _serverCallbacks;
    delete _txCallbacks;
    delete _rxCallbacks;
}

void BluetoothManager::init(const std::string &deviceName)
{
    _bleTxQueue = xQueueCreate(20, sizeof(BleOut *));
    xTaskCreate(bleTxWorker, "BLE-TX", 4096, this, tskIDLE_PRIORITY + 1, nullptr);

    _bleRxQueue = xQueueCreate(20, sizeof(BleIn *));
    configASSERT(_bleRxQueue);
    // start the RX worker at a suitable priority
    xTaskCreate(bleRxWorker, "BLE-RX", 4096, this, tskIDLE_PRIORITY + 2, nullptr);
    // Initialize NimBLE with a device name and set it for advertisement.
    NimBLEDevice::init(deviceName);
    NimBLEDevice::setDeviceName(deviceName);
    NimBLEDevice::setMTU(247);

    // Create the BLE server and assign callbacks.
    pServer = NimBLEDevice::createServer();
    _serverCallbacks = new ServerCallbacks(this);
    pServer->setCallbacks(_serverCallbacks);

    // Create the BLE service.
    pService = pServer->createService(SERVICE_UUID);

    // TX characteristic: phone writes to node
    pTxCharacteristic = pService->createCharacteristic(
        TX_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    _txCallbacks = new CharacteristicCallbacks(this);
    pTxCharacteristic->setCallbacks(_txCallbacks);

    // RX characteristic: node notifies phone
    pRxCharacteristic = pService->createCharacteristic(
        RX_CHAR_UUID,
        NIMBLE_PROPERTY::NOTIFY);

    pRxCharacteristic->addDescriptor(new NimBLE2904());
    pTxCharacteristic->addDescriptor(new NimBLE2904());

    // Start the service.
    pService->start();

    // Get the advertising object and configure scan response data.
    pAdvertising = NimBLEDevice::getAdvertising();
    NimBLEAdvertisementData scanResponse;
    scanResponse.setName(deviceName);
    pAdvertising->setScanResponseData(scanResponse);
}

void BluetoothManager::bleRxWorker(void *pv)
{
    auto *mgr = static_cast<BluetoothManager *>(pv);
    BleIn *pkt;
    for (;;)
    {
        if (xQueueReceive(mgr->_bleRxQueue, &pkt, portMAX_DELAY) == pdTRUE)
        {
            // handle it (in FreRTOS context, not NimBLE ISR)
            mgr->processIncomingMessage(pkt->connHandle, pkt->data);
            delete pkt;
        }
    }
}

void BluetoothManager::bleTxWorker(void *pv)
{
    auto mgr = static_cast<BluetoothManager *>(pv);
    BleOut *pkt;
    for (;;)
    {
        if (xQueueReceive(mgr->_bleTxQueue, &pkt, portMAX_DELAY) == pdTRUE)
        {
            std::string raw;
            switch (pkt->type)
            {
            case BleType::BLE_UnicastUser:
                Serial.printf("Received user_msg from %u, to %u\n", pkt->from, pkt->to);
                raw = encodeMessage(USER_MSG, pkt->to, pkt->from, pkt->data);
                mgr->sendToClient(pkt->connHandle, raw);
                break;
            case BleType::BLE_USER_GATEWAY:
                Serial.printf("Received Gateway msg from %u, to %u\n", pkt->from, pkt->to);
                raw = encodeMessage(USER_MSG_GATEWAY, pkt->to, pkt->from, pkt->data);
                mgr->sendToClient(pkt->connHandle, raw);
                break;
            case BleType::BLE_GATEWAY:
            {
                Serial.printf("Received ble gateway announcement, gatway is ....");
                bool online = !pkt->data.empty() && pkt->data[0];
                uint8_t b = online ? GATEWAY_AVAILABLE
                                   : GATEWAY_OFFLINE;

                raw = encodeMessage(static_cast<BLEMessageType>(b),
                                    0, pkt->from, pkt->data);
                mgr->sendToClient(pkt->connHandle, raw);
                break;
            }
            case BleType::BLE_Node:
                raw = encodeMessage(NODE_MSG, pkt->to, pkt->from, pkt->data);
                mgr->sendBroadcast(raw);
                break;
            case BleType::BLE_Broadcast:
                raw = encodeMessage(BROADCAST, pkt->to, pkt->from, pkt->data);
                mgr->sendBroadcast(raw);
                break;
            case BleType::BLE_List_Users:
                mgr->sendToClient(pkt->connHandle, std::string(pkt->data.begin(), pkt->data.end()));
                break;
            case BleType::BLE_List_Nodes:
                mgr->sendToClient(pkt->connHandle, std::string(pkt->data.begin(), pkt->data.end()));
                break;
            case BleType::BLE_GATEWAY_STATUS:
            {
                bool online = !pkt->data.empty() && pkt->data[0];
                mgr->queueGatewayStatus(online); // call the old helper here
                break;
            }
            case BleType::BLE_ACK:
            {
                Serial.println("Sending ack");
                raw = mgr->encodeAck(pkt->pktId);
                mgr->sendToClient(pkt->connHandle, raw);
                break;
            }
            case BleType::BLE_PUBKEY_RESP:
            {
                Serial.printf("Sending public key for user %u\n", pkt->from);
                std::string raw = BluetoothManager::encodePubKey(pkt->from, pkt->data.data());
                mgr->sendBroadcast(raw);

                break;
            }
            case BleType::BLE_ENC_UnicastUser:
                Serial.printf("Received encrypted user_msg from %u, to %u\n", pkt->from, pkt->to);
                raw = encodeMessage(ENC_USER_MSG, pkt->to, pkt->from, pkt->data);
                mgr->sendToClient(pkt->connHandle, raw);
                break;
            case BleType::BLE_NODE_ID:
                raw = encodeMessage(NODE_ID, 0, pkt->from, pkt->data);
                mgr->sendBroadcast(raw);
                break;

            default:
                break;
            }
            delete pkt;
        }
    }
}

bool BluetoothManager::setGatewayState(bool online)
{
    uint8_t flag = online ? 1 : 0;
    std::vector<uint8_t> payload{flag};

    _gatewayOnline = online;

    /* snapshot the connection list, then release the mutex
       before we ever touch NimBLE */
    std::vector<uint16_t> handles;
    {
        xSemaphoreTake(_connectedDevicesMutex, portMAX_DELAY);
        for (auto &kv : _connectedDevices)
            handles.push_back(kv.first);
        xSemaphoreGive(_connectedDevicesMutex);
    }

    Serial.print("Reached here!\n");

    for (auto h : handles)
        enqueueBleOut(new BleOut{
            BleType::BLE_GATEWAY, h, 0, _nodeID, payload});

    Serial.print("Returning true\n");

    return true;
}

void BluetoothManager::startAdvertising()
{
    if (pAdvertising)
    {
        // Start advertising with no timeout.
        pAdvertising->start(0);
    }
}

void BluetoothManager::stopAdvertising()
{
    if (pAdvertising)
    {
        pAdvertising->stop();
    }
}

bool BluetoothManager::sendBroadcast(const std::string &message)
{
    if (!pRxCharacteristic)
        return false;
    pRxCharacteristic->setValue(message);
    pRxCharacteristic->notify();
    return true;
}

/**
 * @brief Provide connection handle to send data to specific device
 *
 * @param connHandle
 * @param message
 * @return true
 * @return false
 */
bool BluetoothManager::sendToClient(uint16_t connHandle, const std::string &message)
{
    if (!pRxCharacteristic)
        return false;
    pRxCharacteristic->setValue(message);
    return pRxCharacteristic->notify(message, connHandle);
}

bool BluetoothManager::notify(const Outgoing &o)
{
    BleOut *pkt;
    // only check the bleHandle if it is a user message
    // else find the
    if (o.type == BleType::BLE_USER_GATEWAY || o.type == BleType::BLE_UnicastUser || o.type == BleType::BLE_ENC_UnicastUser)
    {
        pkt = new BleOut{o.type, _userMgr->getBleHandle(o.to), o.to, o.from, std::vector<uint8_t>(o.data, o.data + o.length)};
    }

    else
    {
        pkt = new BleOut{o.type, uint16_t(0), o.to, o.from, std::vector<uint8_t>(o.data, o.data + o.length)};
    }

    return enqueueBleOut(pkt);
}

/**
 * @brief Procide connection information ie nimble connection info
 *
 * @param connInfo
 * @param message
 * @return true
 * @return false
 */
bool BluetoothManager::sendToClient(NimBLEConnInfo *connInfo, const std::string &message)
{
    if (!pRxCharacteristic)
    {
        return false;
    }
    uint16_t connHandle = connInfo->getConnHandle();
    pRxCharacteristic->setValue(message);
    return pRxCharacteristic->notify(message, connHandle);
}

void BluetoothManager::recordConnection(const NimBLEConnInfo &connInfo)
{
    xSemaphoreTake(_connectedDevicesMutex, portMAX_DELAY);
    _connectedDevices.emplace(connInfo.getConnHandle(), connInfo);
    xSemaphoreGive(_connectedDevicesMutex);
}

void BluetoothManager::removeConnection(const NimBLEConnInfo &connInfo)
{
    xSemaphoreTake(_connectedDevicesMutex, portMAX_DELAY);
    _connectedDevices.erase(connInfo.getConnHandle());
    xSemaphoreGive(_connectedDevicesMutex);
}

void BluetoothManager::processIncomingMessage(uint16_t connHandle, const std::string &msg)
{
    // Need at least 1 + 4 + 4 = 9 bytes of header
    if (msg.size() < 9)
    {
        Serial.println("Incoming BLE packet too short\n");
        return;
    }

    const uint8_t *data = reinterpret_cast<const uint8_t *>(msg.data());

    // parse little-endian
    uint8_t raw = data[0];
    auto type = static_cast<BLEMessageType>(raw);

    size_t idx = 1;
    uint32_t pktId = 0;

    auto hasPkt = (type == NODE_MSG ||
                   type == USER_MSG ||
                   type == USER_MSG_GATEWAY ||
                   type == BROADCAST || type == ENC_USER_MSG);

    if (hasPkt)
    {
        if (msg.size() < 13)
        {
            Serial.println("pkt too short");
            return;
        }
        pktId = uint32_t(data[1]) | (uint32_t(data[2]) << 8) |
                (uint32_t(data[3]) << 16) | (uint32_t(data[4]) << 24);
        idx += 4;
    }

    if (msg.size() < idx + 8)
    {
        Serial.println("pkt too short");
        return;
    }

    uint32_t dest = uint32_t(data[idx]) | (uint32_t(data[idx + 1]) << 8) | (uint32_t(data[idx + 2]) << 16) | (uint32_t(data[idx + 3]) << 24);
    uint32_t sender = uint32_t(data[idx + 4]) | (uint32_t(data[idx + 5]) << 8) | (uint32_t(data[idx + 6]) << 16) | (uint32_t(data[idx + 7]) << 24);
    idx += 8;

    std::string body;
    if (msg.size() > idx)
        body.assign(reinterpret_cast<const char *>(data + idx), msg.size() - idx);

    switch (type)
    {
    case USER_ID_UPDATE:
    {

        // This is legacy -- shouldn't be used by the phone maintained for old versions
        Serial.printf("Received user_ID_UPDATE for %u with connHandle %u\n", sender, connHandle);
        _userMgr->addOrRefresh(sender, connHandle);

        if (_gatewayOnline)
        {
            Serial.print("Sent Gateway available\n");
            auto pkt = new BleOut{
                BleType::BLE_GATEWAY,
                connHandle,
                0,
                _nodeID,
                std::vector<uint8_t>{1}};
            enqueueBleOut(pkt);
        }

        // Pop any messages in the users inbox
        std::vector<OfflineMsg> queued;
        if (_userMgr->popInbox(sender, queued))
        {
            Serial.println("Poping messages... ");
            for (const auto &q : queued)
            {
                Outgoing o{q.type,
                           q.to,
                           q.from,
                           q.data.data(),
                           q.data.size()};
                notify(o);
            }
        }

        {
            std::vector<uint8_t> pl;
            for (int b = 0; b < 4; ++b)
                pl.push_back((_nodeID >> (8 * b)) & 0xFF);
            auto pkt = new BleOut{BleType::BLE_NODE_ID, // reuse generic type
                                  connHandle, 0, _nodeID, std::move(pl)};
            enqueueBleOut(pkt);
        }
    }
    break;

    case LIST_NODES_REQ:
    {
        // grab a snapshot of all known nodes
        auto nodes = _netHandler->getKnownNodes();
        // serialize into a little‐endian BLE packet
        auto resp = encodeListResponse(LIST_NODE_RESP, nodes);
        // push it into the BLE‐TX queue as a unicast back to the requester
        auto pkt = new BleOut{
            BleType::BLE_List_Nodes,
            connHandle,
            0,
            0,
            std::move(resp)};
        enqueueBleOut(pkt);
        break;
    }

    case LIST_USERS_REQ:
    {
        auto users = _netHandler->getKnownUsers();
        auto resp = encodeListResponse(LIST_USERS_RESP, users);
        auto pkt = new BleOut{
            BleType::BLE_List_Users,
            connHandle,
            0,
            0,
            std::move(resp)};
        enqueueBleOut(pkt);
        break;
    }

    case NODE_MSG:
    {
        // destA = target nodeID
        Serial.printf("Node_msg for %u from %u\n", dest, sender);
        _netHandler->enqueueMessage(MsgKind::NODE, dest, reinterpret_cast<const uint8_t *>(body.data()), body.size());

        // Need to send the message back to the node as well for other users connected to see
        // TODO: this means that sender will get the message back. --> either phone needs to discard or we never send it to the phone
        std::vector<uint8_t> payload(body.begin(), body.end());
        auto pkt = new BleOut{
            BleType::BLE_Node,
            0,
            sender, // swapped so that the phone accepts the message correctly
            dest,   // swapped so that the phone accepts the message correctly
            std::move(payload)};
        enqueueBleOut(pkt);
        break;
    }

    case USER_MSG:
    {
        Serial.printf("Received user_msg from %u to %u\n", sender, dest);
        if (_userMgr->knowsUser(sender))
        {

            _netHandler->enqueueMessage(MsgKind::USER, dest, reinterpret_cast<const uint8_t *>(body.data()), body.size(), sender);

            if (pktId)
            {
                // TODO remove this is just a test
                if (pktId)
                { // only if phone supplied one
                    auto ackPkt = new BleOut{
                        BleType::BLE_ACK,
                        connHandle, // back to the same phone
                        0,
                        0,    // unused
                        {},   // no payload
                        pktId // <-- the 32-bit id
                    };
                    enqueueBleOut(ackPkt);
                }
            }
            else
            {
                Serial.println("Did not recognise sender");
            }
        }
        break;
    }

    case BROADCAST:
    {
        _netHandler->enqueueMessage(MsgKind::NODE, BROADCAST_ADDR, reinterpret_cast<const uint8_t *>(body.data()), body.size());

        // should send messages back to users that are connected to this node:

        std::vector<uint8_t> payload(body.begin(), body.end());
        auto pkt = new BleOut{
            BleType::BLE_Broadcast,
            0,
            sender, // swapped so that the phone accepts the message correctly --> not really required for broadcast
            dest,   // swapped so that the phone accepts the message correctly --> not really required for broadcast
            std::move(payload)};
        enqueueBleOut(pkt);
        break;
    }

    case USER_MSG_GATEWAY:
    {
        Serial.printf("Received user_msg gateway from %u to %u\n", sender, dest);
        if (_userMgr->knowsUser(sender))
        {

            _netHandler->enqueueMessage(MsgKind::TO_GATEWAY, dest, reinterpret_cast<const uint8_t *>(body.data()), body.size(), sender, TO_GATEWAY);
        }
        else
        {
            Serial.println("Did not recognise sender");
        }
        break;
    }

    case BLE_ANNOUNCE_KEY:
    {
        Serial.println("Provided user public key");
        if (body.size() != 32)
            break;
        _netHandler->announcePubKey(sender, reinterpret_cast<const uint8_t *>(body.data()));
        /* store local copy too: */
        // _netHandler->cacheMyKey(sender, reinterpret_cast<const uint8_t *>(body.data()));
        _userMgr->addOrRefresh(sender, connHandle);

        if (_gatewayOnline)
        {
            Serial.print("Sent Gateway available\n");
            auto pkt = new BleOut{
                BleType::BLE_GATEWAY,
                connHandle,
                0,
                _nodeID,
                std::vector<uint8_t>{1}};
            enqueueBleOut(pkt);
        }

        // Pop any messages in the users inbox
        std::vector<OfflineMsg> queued;
        if (_userMgr->popInbox(sender, queued))
        {
            Serial.println("Poping messages... ");
            for (const auto &q : queued)
            {
                Outgoing o{q.type,
                           q.to,
                           q.from,
                           q.data.data(),
                           q.data.size()};
                notify(o);
            }
        }

        // {
        //     std::vector<uint8_t> pl;
        //     for (int b = 0; b < 4; ++b)
        //         pl.push_back((_nodeID >> (8 * b)) & 0xFF);
        //     auto pkt = new BleOut{BleType::BLE_NODE_ID, // reuse generic type
        //                           connHandle, 0, _nodeID, std::move(pl)};
        //     enqueueBleOut(pkt);
        // }

        break;
    }

    case BLE_REQUEST_PUBKEY:
    {
        if (sender == 0)
        {
            Serial.println("ERROR: Sender equals 0 in BLE_REQUEST_PUBKEY breaking -- no PUBKEY REQUEST");
            break;
        }
        uint32_t target = dest; /* caller put userID in dest field   */
        Serial.printf("Requested user public key for user: %u\n", target);
        _netHandler->enqueueMessage(MsgKind::REQ_PUB_KEY, target, reinterpret_cast<const uint8_t *>(body.data()), body.size(), sender);
        /* remember who asked so we know which connection to answer on */
        // _userMgr->rememberKeyWaiter(target, connHandle);
        break;
    }

    case ENC_USER_MSG:
    {
        Serial.println("Received an encrypted message");
        _netHandler->enqueueMessage(MsgKind::ENC_USER, dest, reinterpret_cast<const uint8_t *>(body.data()), body.size(), sender, ENC_MSG);

        if (pktId)
        {
            // TODO remove this is just a test
            if (pktId)
            { // only if phone supplied one
                auto ackPkt = new BleOut{
                    BleType::BLE_ACK,
                    connHandle, // back to the same phone
                    0,
                    0,    // unused
                    {},   // no payload
                    pktId // <-- the 32-bit id
                };
                enqueueBleOut(ackPkt);
            }
        }
        else
        {
            Serial.println("Did not recognise sender");
        }
        break;
    }

    case USER_MOVED:
    {
        /* dest   = oldNodeID (where the inbox lives)
           sender = userID                                        */
        uint32_t oldNodeID = dest;
        uint32_t movedUser = sender;

        _netHandler->enqueueMessage(MsgKind::MOVE_USER_REQ,
                                    oldNodeID,
                                    {}, // do not need this
                                    movedUser);
        break;
    }

    default:

        break;
    }
}

std::vector<uint8_t> BluetoothManager::encodeListResponse(BLEMessageType type, const std::vector<uint32_t> &ids)
{
    size_t n = ids.size();
    std::vector<uint8_t> pkt;
    pkt.reserve(1 + 4 + 4 + 4 + 4 * n);

    // 1B type + 8B zeros (destA/destB)
    pkt.push_back(uint8_t(type));
    for (int i = 0; i < 8; ++i)
        pkt.push_back(0);

    // 4B count, little endian
    for (int b = 0; b < 4; ++b)
        pkt.push_back((n >> (8 * b)) & 0xFF);

    // each ID in LE
    for (auto id : ids)
    {
        for (int b = 0; b < 4; ++b)
            pkt.push_back((id >> (8 * b)) & 0xFF);
    }

    return pkt;
}

std::string BluetoothManager::encodeMessage(BLEMessageType type, uint32_t to, uint32_t from, const std::vector<uint8_t> &payload, uint32_t pktId)
{
    std::string pkt;
    pkt.reserve(1 + 4 + 4 + 4 + payload.size());

    pkt.push_back(static_cast<char>(type));

    for (int b = 0; b < 4; ++b) // pkt-id
        pkt.push_back(char((pktId >> (8 * b)) & 0xFF));

    for (int b = 0; b < 4; ++b) // dest
        pkt.push_back(char((to >> (8 * b)) & 0xFF));

    for (int b = 0; b < 4; ++b) // sender
        pkt.push_back(char((from >> (8 * b)) & 0xFF));

    pkt.insert(pkt.end(), payload.begin(), payload.end());
    return pkt;
}

// --- Implementation of the inner ServerCallbacks class ---

void BluetoothManager::ServerCallbacks::onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo)
{
    Serial.println("Device connected");
    Serial.printf("Latency: %x\n", connInfo.getConnLatency());
    // TODO: create a map to save connInfo and map to a user??? -> not sure how to do this yet
    _mgr->recordConnection(connInfo);
    // Restart advertising to allow additional connections.
    NimBLEDevice::startAdvertising();
}

void BluetoothManager::ServerCallbacks::onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason)
{
    Serial.println("Device disconnected");
    Serial.printf("Reason: %x\n", reason);
    _mgr->_userMgr->handleBleDisconnect(connInfo.getConnHandle());
    _mgr->removeConnection(connInfo);
    // Restart advertising so that new clients can connect.
    NimBLEDevice::startAdvertising();
}
void BluetoothManager::CharacteristicCallbacks::onWrite(NimBLECharacteristic *pChr, NimBLEConnInfo &connInfo)
{
    uint16_t handle = connInfo.getConnHandle();
    std::string msg = pChr->getValue();
    Serial.printf("Received from conn %u (len=%u): ", handle, msg.length());
    for (uint8_t b : msg)
    {
        Serial.printf("%02X ", b);
    }
    Serial.println();

    BleIn *inPkt = new BleIn{handle, std::move(msg)};
    // if the queue is full, you might log or drop:
    if (xQueueSend(_mgr->_bleRxQueue, &inPkt, pdMS_TO_TICKS(10)) != pdPASS)
    {
        delete inPkt;
        Serial.println("BLE RX queue full, dropping packet");
    }
}

void BluetoothManager::queueGatewayStatus(bool online)
{
    uint8_t flag = online ? 1 : 0; // tiny payload distinguishes state
    std::vector<uint8_t> payload{flag};

    xSemaphoreTake(_connectedDevicesMutex, portMAX_DELAY);
    for (auto &kv : _connectedDevices)
    {
        auto pkt = new BleOut{
            BleType::BLE_GATEWAY,
            kv.first, // connHandle
            0,
            _nodeID,
            payload};
        enqueueBleOut(pkt);
    }
    xSemaphoreGive(_connectedDevicesMutex);
}

std::string BluetoothManager::encodeAck(uint32_t pktId)
{
    std::string p;
    p.reserve(1 + 4);
    p.push_back(char(ACK_SUCCESS));
    for (int i = 0; i < 4; ++i)
        p.push_back(char((pktId >> (8 * i)) & 0xFF));
    return p;
}
