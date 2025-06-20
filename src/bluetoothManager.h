#ifndef BLUETOOTH_MANAGER_H
#define BLUETOOTH_MANAGER_H

#include <NimBLEDevice.h>
#include <string>
#include <map>
#include "freertos/semphr.h"
#include "userSessionManager.h"
#include "NetworkMessageHandler.h"
#include "IClientNotifier.h"
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// Define UUIDs for the service and characteristic
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
// TX: Phone → Node
#define TX_CHAR_UUID "0000feed-0001-1000-8000-00805f9b34fb"
// RX: Node → Phone
#define RX_CHAR_UUID "0000feed-0002-1000-8000-00805f9b34fb"

struct BleOut
{
    BleType type;
    uint16_t connHandle;
    uint32_t to;   // to a node or user -> depends on type
    uint32_t from; // from a node or a user -> depends on type
    std::vector<uint8_t> data;
    uint32_t pktId;

    explicit BleOut(BleType t,
                    uint16_t ch,
                    uint32_t to_ = 0,
                    uint32_t from_ = 0,
                    std::vector<uint8_t> d = {},
                    uint32_t id = 0)
        : type(t),
          connHandle(ch),
          to(to_),
          from(from_),
          data(std::move(d)),
          pktId(id) {}
};

struct BleIn
{
    uint16_t connHandle;
    std::string data;
};

class BluetoothManager : public IClientNotifier
{
public:
    BluetoothManager(UserSessionManager *sessionMgr, NetworkMessageHandler *networkHandler, uint32_t nodeID);
    ~BluetoothManager();

    void setNetworkMessageHandler(NetworkMessageHandler *nh)
    {
        _netHandler = nh;
    }

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

    bool notify(const Outgoing &o) override;

    bool setGatewayState(bool on);

    bool enqueueBleOut(BleOut *pkt)
    {
        if (xQueueSend(_bleTxQueue, &pkt, pdMS_TO_TICKS(10)) != pdPASS)
        {
            delete pkt;
            return false;
        }
        return true;
    }

private:
    NimBLEServer *pServer;
    NimBLEService *pService;
    NimBLEAdvertising *pAdvertising;
    NimBLECharacteristic *pTxCharacteristic;
    NimBLECharacteristic *pRxCharacteristic;

    UserSessionManager *_userMgr;
    NetworkMessageHandler *_netHandler;

    std::map<uint16_t, NimBLEConnInfo> _connectedDevices;
    SemaphoreHandle_t _connectedDevicesMutex;

    QueueHandle_t _bleRxQueue;
    static void bleRxWorker(void *pv);

    QueueHandle_t _bleTxQueue;
    static void bleTxWorker(void *);

    bool _gatewayOnline = false;

    uint32_t _nodeID;

    enum BLEMessageType : uint8_t
    {
        BROADCAST = 0x01,
        NODE_MSG = 0x02,
        USER_MSG = 0x03,
        USER_ID_UPDATE = 0x04,
        LIST_NODES_REQ = 0x05,
        LIST_USERS_REQ = 0x06,
        LIST_NODE_RESP = 0x07,
        LIST_USERS_RESP = 0x08,
        GATEWAY_AVAILABLE = 0x09,
        USER_MSG_GATEWAY = 0x0A,
        GATEWAY_OFFLINE = 0x0B,
        ACK_SUCCESS = 0x0C,
        BLE_ANNOUNCE_KEY = 0x0D,
        BLE_REQUEST_PUBKEY = 0x0E,
        BLE_PUBKEY_RESP = 0x0F,
        USER_MSG_ACCEPTED = 0x10,
        ENC_USER_MSG = 0x11,
        USER_MOVED = 0x12, // phone → node
        NODE_ID = 0x13,    // node  → phone
        REQUEST_NODE_ID = 0x14,
        USER_MSG_REQ_ACK = 0x15, /* phone → node, ask for ACK, user-to-user  */
        NODE_MSG_REQ_ACK = 0x16, /* phone → node, ask for ACK, node-to-node  */
        ACK_FAILED = 0x17,        /* node  → phone, delivery could not finish */
        ENC_USER_MSG_REQ_ACK = 0x18, 
    };

    static std::string encodePubKey(uint32_t userID, const uint8_t pk[32])
    {
        std::string s;
        s.reserve(1 + 4 + 4 + 32);
        s.push_back(char(BLE_PUBKEY_RESP));
        /* to = userID, from = 0 */
        for (int b = 0; b < 4; ++b)
            s.push_back(char((userID >> (8 * b)) & 0xFF));
        for (int b = 0; b < 4; ++b)
            s.push_back(0);
        s.append(reinterpret_cast<const char *>(pk), 32);
        return s;
    }

    /* helper to build the 5-byte “ACK_FAILED” packet */
    static std::string encodeAckFailure(uint32_t pktId)
    {
        std::string p;
        p.reserve(1 + 4);
        p.push_back(char(ACK_FAILED));
        for (int i = 0; i < 4; ++i)
            p.push_back(char((pktId >> (8 * i)) & 0xFF));
        return p;
    }

    void recordConnection(const NimBLEConnInfo &connInfo);
    void removeConnection(const NimBLEConnInfo &connInfo);
    void processIncomingMessage(uint16_t connHandle, const std::string &msg);
    void onLoRaMessage(uint32_t targetUserId, const std::string &payload);

    std::string encodeAck(uint32_t pktId);

    void queueGatewayStatus(bool online);

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

    static std::vector<uint8_t> encodeListResponse(BLEMessageType type, const std::vector<uint32_t> &ids);
    static std::string encodeMessage(BLEMessageType type, uint32_t to, uint32_t from, const std::vector<uint8_t> &payload, uint32_t pktId = 0);
};

#endif
