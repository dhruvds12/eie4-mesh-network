#pragma once
#include <cstdint>
#include <cstddef>

enum class BleType : uint8_t { BLE_Broadcast, BLE_UnicastUser };

struct Outgoing
{
    BleType type;
    uint32_t userID;
    const uint8_t *data;
    size_t length;
};

class IClientNotifier
{
public:
    virtual ~IClientNotifier() = default;
    virtual bool notify(const Outgoing &o) = 0;
};