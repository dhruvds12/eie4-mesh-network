#pragma once

#include "IRadioManager.h"
#include <queue>
#include <vector>
#include <string.h>

class MockRadioManager : public IRadioManager
{
public:
    MockRadioManager() = default;
    ~MockRadioManager() {}

    struct TxPacket
    {
        std::vector<uint8_t> data;
    };

    std::vector<TxPacket> txPacketsSent;

    bool enqueueTxPacket(const uint8_t *data, size_t len)
    {
        TxPacket p;
        p.data.assign(data, data + len);
        txPacketsSent.push_back(p);
        return true;
    }

    bool dequeueRxPacket(RadioPacket **packet)
    {
        if (!rxQueue.empty())
        {
            auto front = rxQueue.front();
            rxQueue.pop();
            *packet = new RadioPacket;
            memcpy((*packet)->data, front.data, front.len);
            (*packet)->len = front.len;
            return true;
        }
        return false;
    }

    void enqueueRxPacket(const uint8_t *data, size_t len)
    {
        RadioPacket p;
        memcpy(p.data, data, len);
        p.len = len;
        rxQueue.push(p);
    }

private:
    std::queue<RadioPacket> rxQueue;
};

