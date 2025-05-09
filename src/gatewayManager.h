#ifndef GATEWAY_MANAGER_H
#define GATEWAY_MANAGER_H

#include <Arduino.h>
#include <HTTPClient.h>
#include "NetworkMessageHandler.h"
#include "userSessionManager.h" // this is wrong actually only tracks local users :( --> need to global user table in the router
#include <ArduinoJson.h>
#include "bluetoothManager.h"
#include "aodvRouter.h"
class GatewayManager
{
public:
    GatewayManager(const char *url,
                   uint32_t id,
                   NetworkMessageHandler *nmh,
                   UserSessionManager *usm,
                   BluetoothManager *btm,
                   AODVRouter *router);

    void begin();

    //  called by NMH for TO_GATEWAY traffic
    void uplink(uint32_t srcUser,
                uint32_t dstUser,
                const char *text);

    //  Wi-Fi event hooks
    void onWifiUp();
    void onWifiDown();

private:
    struct UplinkMsg
    {
        char id[13]; // 12-char randID + '\0'
        uint32_t from;
        uint32_t to;
        char body[128];
    };

    static void syncTask(void *pv);

    bool oneSync();               // does a single /syncNode round
    void buildSeen(JsonArray &a); // push currently connected user-IDs

    QueueHandle_t _txQ;
    EventGroupHandle_t _evt;
    static constexpr EventBits_t WIFI_READY = 0x01;

    String _api;
    uint32_t _me;
    NetworkMessageHandler *_nmh;
    UserSessionManager *_usm;
    BluetoothManager *_btMgr;
    AODVRouter *_router;
};

#endif