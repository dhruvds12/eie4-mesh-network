#include "BluetoothManager.h"
#include <NimBLEDevice.h>

// Define UUIDs for the service and characteristic
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

BluetoothManager::BluetoothManager()
    : pServer(nullptr), pCharacteristic(nullptr), pAdvertising(nullptr), serverCallbacks(nullptr) {}

BluetoothManager::~BluetoothManager()
{
    // Clean up dynamic memory if needed.
    if (serverCallbacks)
    {
        delete serverCallbacks;
        serverCallbacks = nullptr;
    }
}

void BluetoothManager::init(const std::string &deviceName)
{
    // Initialize NimBLE with a device name and set it for advertisement.
    NimBLEDevice::init(deviceName);
    NimBLEDevice::setDeviceName(deviceName);

    // Create the BLE server and assign callbacks.
    pServer = NimBLEDevice::createServer();
    serverCallbacks = new ServerCallbacks(this);
    pServer->setCallbacks(serverCallbacks);

    // Create the BLE service.
    NimBLEService *pService = pServer->createService(SERVICE_UUID);

    // Create a characteristic with READ, WRITE, and NOTIFY properties.
    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);

    // Set an initial value.
    pCharacteristic->setValue("Initial Value");

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

void BluetoothManager::sendBroadcast(const std::string &message)
{
    if (pCharacteristic)
    {
        pCharacteristic->setValue(message);
        // notify() without specifying a connection sends to all subscribed clients.
        pCharacteristic->notify();
    }
}

bool BluetoothManager::sendToClient(uint16_t connHandle, const std::string &message)
{
    if (pCharacteristic)
    {
        pCharacteristic->setValue(message);
        // The overloaded notify sends only to the specified client.
        return pCharacteristic->notify(message, connHandle);
    }
    return false;
}

bool BluetoothManager::sendToClient(NimBLEConnInfo* connInfo, const std::string &message)
{
    if (pCharacteristic)
    {
        uint16_t connHandle = connInfo->getConnHandle();
        pCharacteristic->setValue(message); // TODO: is this needed?
        // The overloaded notify sends only to the specified client.
        return pCharacteristic->notify(message, connHandle);
    }
    return false;
}

// --- Implementation of the inner ServerCallbacks class ---

void BluetoothManager::ServerCallbacks::onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo)
{
    Serial.println("Device connected");
    Serial.printf("Latency: %x\n", connInfo.getConnLatency());
    // TODO: create a map to save connInfo and map to a user??? -> not sure how to do this yet
    // Restart advertising to allow additional connections.
    NimBLEDevice::startAdvertising();
}

void BluetoothManager::ServerCallbacks::onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason)
{
    Serial.println("Device disconnected");
    Serial.printf("Reason: %x\n", reason);
    // Restart advertising so that new clients can connect.
    NimBLEDevice::startAdvertising();
}
