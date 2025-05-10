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
    auto pkt = new BleOut{o.type, _userMgr->getBleHandle(o.to), o.to, o.from, std::vector<uint8_t>(o.data, o.data + o.length)};
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

    uint32_t dest = uint32_t(data[1]) | (uint32_t(data[2]) << 8) | (uint32_t(data[3]) << 16) | (uint32_t(data[4]) << 24);
    uint32_t sender = uint32_t(data[5]) | (uint32_t(data[6]) << 8) | (uint32_t(data[7]) << 16) | (uint32_t(data[8]) << 24);

    // remaining bytes are the UTF-8 payload
    std::string body;
    if (msg.size() > 9)
    {
        body.assign(reinterpret_cast<const char *>(data + 9),
                    msg.size() - 9);
    }

    switch (type)
    {
    case USER_ID_UPDATE:
    {
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
    }
    break;

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
    }
    break;

    case NODE_MSG:
        // destA = target nodeID
        Serial.printf("Node_msg for %u from %u\n", dest, sender);
        _netHandler->enqueueMessage(MsgKind::NODE, dest, body.c_str());
        break;

    case USER_MSG:
    {
        Serial.printf("Received user_msg from %u to %u\n", sender, dest);
        if (_userMgr->knowsUser(sender))
        {

            _netHandler->enqueueMessage(MsgKind::USER, dest, body.c_str(), sender);
        }
        else
        {
            Serial.println("Did not recognise sender");
        }
    }
    break;

    case BROADCAST:
    {
        _netHandler->enqueueMessage(MsgKind::NODE, BROADCAST_ADDR, body.c_str());
    }
    break;

    case USER_MSG_GATEWAY:
    {
        Serial.printf("Received user_msg gateway from %u to %u\n", sender, dest);
        if (_userMgr->knowsUser(sender))
        {

            _netHandler->enqueueMessage(MsgKind::TO_GATEWAY, dest, body.c_str(), sender, TO_GATEWAY);
        }
        else
        {
            Serial.println("Did not recognise sender");
        }
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

std::string BluetoothManager::encodeMessage(BLEMessageType type, uint32_t to, uint32_t from, const std::vector<uint8_t> &payload)
{
    std::string pkt;
    pkt.reserve(1 + 4 + 4 + payload.size());
    pkt.push_back(static_cast<char>(type));

    for (int b = 0; b < 4; ++b)
        pkt.push_back(static_cast<char>((to >> (8 * b)) & 0xFF));

    for (int b = 0; b < 4; ++b)
        pkt.push_back(static_cast<char>((from >> (8 * b)) & 0xFF));

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
