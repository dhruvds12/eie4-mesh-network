#pragma once
#include <cstdint>
#include <cstddef>

enum class BleType : uint8_t
{
    BLE_Broadcast,
    BLE_UnicastUser,
    BLE_Node,
    BLE_List_Users,
    BLE_List_Nodes,
    BLE_GATEWAY,
    BLE_USER_GATEWAY,
    BLE_GATEWAY_STATUS,
    BLE_ACK,
    BLE_PUBKEY_RESP,
    BLE_ENC_UnicastUser,
    BLE_NODE_ID
};

struct Outgoing
{
    BleType type;
    uint32_t to;   // to user or node
    uint32_t from; // from user or node
    const uint8_t *data;
    size_t length;
    uint32_t pktId;

    explicit Outgoing(BleType t,
                      uint32_t to_ = 0,
                      uint32_t fr = 0,
                      const uint8_t *d = 0,
                      size_t len = 0,
                      uint32_t pid = 0)

        : type(t),
          to(to_),
          from(fr),
          data(d),
          length(len),
          pktId(pid)
    {
    }
};

class IClientNotifier
{
public:
    virtual ~IClientNotifier() = default;
    virtual bool notify(const Outgoing &o) = 0;
    virtual bool setGatewayState(bool online) = 0;
};