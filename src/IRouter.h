#ifndef IROUTER_H
#define IROUTER_H

#include <cstdint>
#include <cstdlib>

// Basic interface that will allow swapping between routers if needed 
// TODO: Expand the interface as required
class IRouter
{
public:
    virtual ~IRouter() {}

    virtual bool begin() = 0;
    virtual void sendData(uint32_t destNodeID, const uint8_t *data, size_t len) = 0;
};

#endif