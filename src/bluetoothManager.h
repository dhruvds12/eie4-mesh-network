#ifndef BLUETOOTH_MANAGER_H
#define BLUETOOTH_MANAGER_H

#include <NimBLEDevice.h>
#include <string>

class BluetoothManager
{
public:
    BluetoothManager();
    ~BluetoothManager();

    // Initialises the BLE stack, creates the server, service, and characteristic.
    // deviceName is used for both the device name and in advertisement data.
    void init(const std::string &deviceName);

    // Start advertising (for example, after initialization or when needed)
    void startAdvertising();

    // Stop advertising if necessary.
    void stopAdvertising();

    // Send a message to all connected clients (broadcast via notification)
    void sendBroadcast(const std::string &message);

    // Send a message to a specific client.
    // The connection info pointer must refer to the client you wish to target.
    // Returns true on success.
    bool sendToClient(uint16_t connHandle, const std::string &message);

    bool sendToClient(NimBLEConnInfo *connInfo, const std::string &message);

    // Accessor to the server
    NimBLEServer *getServer() { return pServer; }

private:
    NimBLEServer *pServer;
    NimBLECharacteristic *pCharacteristic;
    NimBLEAdvertising *pAdvertising;

    // Inner class to handle connection callbacks.
    class ServerCallbacks : public NimBLEServerCallbacks
    {
    public:
        ServerCallbacks(BluetoothManager *manager) : manager(manager) {}
        void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) override;
        void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason) override;

    private:
        BluetoothManager *manager;
    };

    ServerCallbacks *serverCallbacks;
};

#endif
