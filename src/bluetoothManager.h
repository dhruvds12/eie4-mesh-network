#ifndef BLUETOOTH_MANAGER_H
#define BLUETOOTH_MANAGER_H

#include <NimBLEDevice.h>
#include <string>
#include <map>
#include "freertos/semphr.h"
#include "userSessionManager.h"

// Define UUIDs for the service and characteristic
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
// TX: Phone → Node
#define TX_CHAR_UUID "0000feed-0001-1000-8000-00805f9b34fb"
// RX: Node → Phone
#define RX_CHAR_UUID "0000feed-0002-1000-8000-00805f9b34fb"

class BluetoothManager
{
public:
    BluetoothManager(UserSessionManager *sessionMgr);
    ~BluetoothManager();

    // Initialises the BLE stack, creates the server, service, and characteristic.
    // deviceName is used for both the device name and in advertisement data.
    void init(const std::string &deviceName);

    // Start advertising (for example, after initialization or when needed)
    void startAdvertising();

    // Stop advertising if necessary.
    void stopAdvertising();

    // Send a message to all connected clients (broadcast via notification)
    bool sendBroadcast(const std::string &message);

    // Send a message to a specific client.
    // The connection info pointer must refer to the client you wish to target.
    // Returns true on success.
    bool sendToClient(uint16_t connHandle, const std::string &message);

    bool sendToClient(NimBLEConnInfo *connInfo, const std::string &message);

    // Accessor to the server
    NimBLEServer *getServer() { return pServer; }

private:
    NimBLEServer *pServer;
    NimBLEService *pService;
    NimBLEAdvertising *pAdvertising;
    NimBLECharacteristic *pTxCharacteristic;
    NimBLECharacteristic *pRxCharacteristic;

    UserSessionManager *_userMgr;

    std::map<uint16_t, NimBLEConnInfo> _connectedDevices;
    SemaphoreHandle_t _connectedDevicesMutex;

    void recordConnection(const NimBLEConnInfo &connInfo);
    void removeConnection(const NimBLEConnInfo &connInfo);
    void processIncomingMessage(uint16_t connHandle, const std::string &msg);
    void onLoRaMessage(uint32_t targetUserId, const std::string &payload);

    // Inner class to handle connection callbacks.
    class ServerCallbacks : public NimBLEServerCallbacks
    {
    public:
        ServerCallbacks(BluetoothManager *manager) : _mgr(manager) {}
        void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) override;
        void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason) override;

    private:
        BluetoothManager *_mgr;
    };

    class CharacteristicCallbacks : public NimBLECharacteristicCallbacks
    {
    public:
        CharacteristicCallbacks(BluetoothManager *mgr) : _mgr(mgr) {}
        void onWrite(NimBLECharacteristic *pChr, NimBLEConnInfo &connInfo) override;

    private:
        BluetoothManager *_mgr;
    };

    ServerCallbacks *_serverCallbacks;
    CharacteristicCallbacks *_txCallbacks;
    CharacteristicCallbacks *_rxCallbacks;
};

#endif
