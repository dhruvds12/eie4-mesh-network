#ifndef MQTT_MANAGER_H      
#define MQTT_MANAGER_H

#include "IRadioManager.h"

class MQTTManager
{
public:
    // ctor signature must match what the code calls
    MQTTManager(const char*, uint32_t, IRadioManager*, void* = nullptr) {}

    /* Public data members used in AODVRouter */
    bool connected = false;

    /* Methods that AODVRouter calls – provide empty bodies */
    void begin() {}

    void publishUpdateRoute(uint32_t, uint32_t, uint8_t) {}
    void publishInvalidateRoute(uint32_t) {}
    void publishPacket(uint32_t, const uint8_t*, size_t) {}
    void publishUserAdded(uint32_t) {}

    void publishMessage(const char*, const char*, int) {}
    void enqueueSendMQTTQueue(const char*, int) {}
};

#endif
