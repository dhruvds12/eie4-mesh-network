#include "BluetoothManager.h"
#include <NimBLEDevice.h>

BluetoothManager::BluetoothManager(UserSessionManager *sessionMgr)
    : pServer(nullptr), pService(nullptr), pAdvertising(nullptr), _userMgr(sessionMgr), _serverCallbacks(nullptr), _txCallbacks(nullptr), _rxCallbacks(nullptr), pTxCharacteristic(nullptr), pRxCharacteristic(nullptr)
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
    // Example protocol: "ID:<userID>" to register user
    if (msg.rfind("ID:", 0) == 0)
    {
        uint32_t userID = std::stoul(msg.substr(3));
        // Store or refresh session
        _userMgr->addOrRefresh(userID, connHandle);
        return;
    }
    // TODO: dispatch other message types here
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
    Serial.printf("Received from conn %u: %s\n", handle, msg.c_str());
    _mgr->processIncomingMessage(handle, msg);
}
