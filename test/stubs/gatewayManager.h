#ifndef GATEWAY_MANAGER_H          // keep the *real* guard so we shadow it
#define GATEWAY_MANAGER_H

#include <cstdint>

/* Forward declarations so we don’t need the heavy headers */
class NetworkMessageHandler;
class UserSessionManager;
class BluetoothManager;
class AODVRouter;

/* ----------------------------------------------------------------- */
/*                Lightweight replacement used in unit-tests         */
/* ----------------------------------------------------------------- */
class GatewayManager
{
public:
    GatewayManager(const char*                /*url*/,
                   uint32_t                   /*id*/,
                   NetworkMessageHandler*     /*nmh*/,
                   UserSessionManager*        /*usm*/,
                   BluetoothManager*          /*btm*/,
                   AODVRouter*                /*router*/)
    {}

    /* APIs that AODVRouter (and tests) may call -------------------- */
    void begin() {}

    /* Called from AODVRouter when a user message needs to reach the
       cloud gateway – empty body is fine for unit tests.            */
    void uplink(uint32_t /*srcUser*/,
                uint32_t /*dstUser*/,
                const char* /*text*/)
    {}

    /* Wi-Fi event hooks – no-ops here */
    void onWifiUp()   {}
    void onWifiDown() {}

    /* Router checks this flag when building broadcasts */
    bool isOnline() const { return true; }   // pretend gateway is always up
};

#endif   // GATEWAY_MANAGER_H
