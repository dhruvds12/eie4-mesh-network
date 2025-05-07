#pragma once
#include <cstdint>
#include <cstddef>

enum class BleType : uint8_t { BLE_Broadcast, BLE_UnicastUser, BLE_Node, BLE_List_Users, BLE_List_Nodes, BLE_GATEWAY };

struct Outgoing
{
    BleType type;
    uint32_t to; // to user or node
    uint32_t from; // from user or node
    const uint8_t *data;
    size_t length;
};

class IClientNotifier
{
public:
    virtual ~IClientNotifier() = default;
    virtual bool notify(const Outgoing &o) = 0;
};