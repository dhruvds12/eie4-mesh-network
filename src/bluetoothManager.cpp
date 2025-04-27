#include "BluetoothManager.h"
#include <NimBLEDevice.h>
#include "packet.h"

BluetoothManager::BluetoothManager(UserSessionManager *sessionMgr, NetworkMessageHandler *networkHandler)
    : pServer(nullptr), pService(nullptr), pAdvertising(nullptr), _userMgr(sessionMgr), _netHandler(networkHandler), _serverCallbacks(nullptr), _txCallbacks(nullptr), _rxCallbacks(nullptr), pTxCharacteristic(nullptr), pRxCharacteristic(nullptr)
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
    // Initialize NimBLE with a device name and set it for advertisement.
    NimBLEDevice::init(deviceName);
    NimBLEDevice::setDeviceName(deviceName);

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
    pRxCharacteristic->setValue(encodeBroadcast(message));
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
    if (msg.size() < 9) {
        Serial.println("Incoming BLE packet too short");
        return;
    }

    const uint8_t* data = reinterpret_cast<const uint8_t*>(msg.data());

    // parse little-endian
    uint8_t  raw  = data[0];
    auto type = static_cast<BLEMessageType>(raw);

    uint32_t destA = uint32_t(data[1])
                   | (uint32_t(data[2]) << 8)
                   | (uint32_t(data[3]) << 16)
                   | (uint32_t(data[4]) << 24);
    uint32_t destB = uint32_t(data[5])
                   | (uint32_t(data[6]) << 8)
                   | (uint32_t(data[7]) << 16)
                   | (uint32_t(data[8]) << 24);

    // remaining bytes are the UTF-8 payload
    std::string body;
    if (msg.size() > 9) {
        body.assign(reinterpret_cast<const char*>(data + 9),
                    msg.size() - 9);
    }


    switch (type)
    {
    // case USER_ID_UPDATE:
    // {
    //     uint32_t userID = std::stoul(body);
    //     _userMgr->addOrRefresh(userID, connHandle);
    // }
    // break;

    // case LIST_NODES_REQ:
    // {
    //     // You’ll need a way to ask the router for its known node IDs:
    //     auto nodes = _router->getKnownNodeIDs();
    //     auto resp = encodeListResponse(LIST_NODES_RESP, nodes);
    //     sendToClient(connHandle, resp);
    // }
    // break;

    // case LIST_USERS_REQ:
    // {
    //     auto users = _userMgr->allUsers()
    //                      .filter(u→u.isConnected)
    //                      .map(u→u.userID);
    //     auto resp = encodeListResponse(LIST_USERS_RESP, users);
    //     sendToClient(connHandle, resp);
    // }
    // break;

    // case NODE_MSG:
    //     // destA = target nodeID
    //     _netHandler->enqueueMessage(destA, body.c_str());
    //     break;

    // case USER_MSG:
    // {
    //     // Who am I? I need the sender’s userID:
    //     uint32_t fromUser = _userMgr->getUserIDForHandle(connHandle);
    //     uint32_t toUser = destA;
    //     _router->sendUserMessage(fromUser, toUser,
    //                              (const uint8_t *)body.data(),
    //                              body.size());
    // }
    // break;

    case BROADCAST:
    {
        _netHandler->enqueueMessage(BROADCAST_ADDR, body.c_str());
    }
    break;

    default:
      
        break;
    }
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

    _mgr->processIncomingMessage(handle, msg);
}
