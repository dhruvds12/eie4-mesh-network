#ifndef IRADIOMANAGER_H
#define IRADIOMANAGER_H

#include <cstdint>
#include <cstdlib>

struct RadioPacket
{
    uint8_t data[256];
    size_t len;
};

/**
 * @brief IRadioManager Interface
 *
 * An interface provided for RadioManager used to overwrite functions for testing
 *
 */
class IRadioManager
{
public:
    virtual ~IRadioManager() {}

    virtual bool enqueueTxPacket(const uint8_t *data, size_t len) = 0;
    virtual bool dequeueRxPacket(RadioPacket **packet) = 0;
};

#endif