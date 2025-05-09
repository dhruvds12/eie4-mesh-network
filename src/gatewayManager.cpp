#include "gatewayManager.h"
#include "packet.h"
#include <ArduinoJson.h>

GatewayManager::GatewayManager(const char *url,
                               uint32_t id,
                               NetworkMessageHandler *nmh,
                               UserSessionManager *usm,
                               BluetoothManager *btm,
                               AODVRouter *router)
    : _api(url), _me(id), _nmh(nmh), _usm(usm), _btMgr(btm), _router(router)
{
    _txQ = xQueueCreate(30, sizeof(UplinkMsg));

    // Used to signal the WIFI is ready flag
    _evt = xEventGroupCreate();
}

void GatewayManager::begin()
{
    _nmh->setGatewayManager(this);
    // TODO: DO these need to be pinned to core 1
    xTaskCreate(syncTask, "GW-SYNC", 8192, this, 1, nullptr);
    xTaskCreate(
        [](void *pv)
        { static_cast<GatewayManager *>(pv)->wifiEventTask(); },
        "GW-WIFI",
        2048, this, 3, nullptr);
}

void GatewayManager::uplink(uint32_t src, uint32_t dst, const char *msg)
{

    UplinkMsg m;

    for (int i = 0; i < 12; ++i)
    {
        uint8_t v = esp_random() % 36;
        m.id[i] = v < 10 ? '0' + v : 'a' + (v - 10);
    }

    m.id[12] = '\0';

    m.from = src;
    m.to = dst;
    strlcpy(m.body, msg, sizeof(m.body));

    /* enqueue immediately, no block if queue is full */
    xQueueSend(_txQ, &m, 0);
}

void GatewayManager::onWifiUp()
{
    xEventGroupSetBits(_evt, WIFI_READY | GW_EVT_ON);

    // Kick off registration if needed:
    if (!_registered)
    {
        // we can launch it in a detached task so we don't block the event-handler
        xTaskCreate([](void *pv)
                    {
              auto self = static_cast<GatewayManager*>(pv);
              while (!self->_registered) {
                if (self->registerNode()) break;
                vTaskDelay(pdMS_TO_TICKS(2000));
             }
              vTaskDelete(nullptr); }, "GW-REG", 4096, this, 2, nullptr);
    }
}
void GatewayManager::onWifiDown()
{
    /* clear the level bit, raise the “edge” */
    xEventGroupClearBits(_evt, WIFI_READY);
    xEventGroupSetBits(_evt, GW_EVT_OFF);
}

void GatewayManager::syncTask(void *pv)
{
    auto self = static_cast<GatewayManager *>(pv);

    for (;;)
    {
        /* BLOCK here until Wi-Fi is up.  ClearOnExit (pdTRUE) so we
   return immediately the next time Wi-Fi drops.              */
        xEventGroupWaitBits(self->_evt,
                            WIFI_READY,
                            pdFALSE, // do not clear on entry
                            pdTRUE,  // wait for *all* bits (just one)
                            portMAX_DELAY);

        if (!self->_registered)
        {
            Serial.println("Waiting to register before syncing…");
            // back off a bit
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        /* Do ONE “/syncNode” round-trip.  If it returns false
        (HTTP error / JSON parse fail) wait 2 s and retry.         */
        if (!self->oneSync())
            Serial.println("oneSync() failed; retrying in 2 s");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void GatewayManager::wifiEventTask()
{
    for (;;)
    {
        EventBits_t bits = xEventGroupWaitBits(
            _evt, GW_EVT_ON | GW_EVT_OFF,
            pdTRUE,  // clear on exit
            pdFALSE, // any bit
            portMAX_DELAY);

        if (bits & GW_EVT_ON)
        {
            _btMgr->setGatewayState(true);
        }
        else if (bits & GW_EVT_OFF)
        {
            _btMgr->setGatewayState(false);
        }
    }
}

bool GatewayManager::oneSync()
{
    Serial.print("Started sync\n");
    /* -------- assemble request JSON -------- */
    JsonDocument req;
    req["gwId"] = String(_me);

    // SEEN list
    JsonArray aSeen = req["seen"].to<JsonArray>();
    Serial.println("requesting build seen");
    buildSeen(aSeen);
    Serial.println("Received build seen");

    // UPLINK msgs
    JsonArray aUp = req["uplink"].to<JsonArray>();
    UplinkMsg upl;
    while (uxQueueMessagesWaiting(_txQ) && aUp.size() < 10) // batch ≤10
    {
        if (xQueueReceive(_txQ, &upl, 0) == pdTRUE)
        {
            JsonObject m = aUp.add<JsonObject>();
            m["msgId"] = upl.id;
            m["src"] = String(upl.from);
            m["dst"] = String(upl.to);
            m["body"] = upl.body;
        }
    }

    String body;
    serializeJson(req, body);

    /* -------- HTTP POST /syncNode -------- */
    Serial.print("Sent http\n");
    HTTPClient http;
    http.setTimeout(35000);
    http.begin(_api + "/sync/node");
    http.addHeader("Content-Type", "application/json");
    int rc = http.POST(body);
    if (rc != 200)
    {
        http.end();
        Serial.print("Failed http request -- 401/404 most likely\n");
        return false;
    }

    /* -------- parse response -------- */
    Serial.println("Parsing response");
    JsonDocument resp;
    DeserializationError e = deserializeJson(resp, http.getStream());
    http.end();
    if (e)
        return false;

    /* Remove acked uplinks that may still be in queue */
    for (const char *id : resp["ack"].as<JsonArray>())
    {
        // naive: nothing to do because we already popped them
        (void)id;
    }

    /* Downlink → mesh */
    for (JsonObject j : resp["down"].as<JsonArray>())
    {
        const char *txt = j["body"] | "";
        uint32_t src = strtoul(j["src"], nullptr, 10);
        uint32_t dst = strtoul(j["dst"], nullptr, 10);

        if (_usm->knowsUser(dst))
        {
            // the user is locally connected
            // send connection directly over ble if connected or queue the msg
            if (_usm->isOnline(dst))
            {
                auto pkt = new BleOut{
                    BleType::BLE_USER_GATEWAY,
                    _usm->getBleHandle(dst),
                    dst,
                    src,
                    std::vector<uint8_t>(
                        (const uint8_t *)txt,
                        (const uint8_t *)txt + strlen(txt))};

                _btMgr->enqueueBleOut(pkt);
            }
            else
            {
                //TODO store in the userInfo
            }
        }
        else
        {

            _nmh->enqueueMessage(MsgKind::FROM_GATEWAY,
                                 dst,
                                 txt,
                                 src,
                                 FROM_GATEWAY);
        }
    }

    /* sleep suggested by server */
    uint32_t s = resp["sleep"] | 15;
    vTaskDelay(pdMS_TO_TICKS(s * 1000));
    return true;
}

void GatewayManager::buildSeen(JsonArray &arr)
{
    // 1. locally connected phones
    Serial.println("Getting local users");
    for (uint32_t uid : _usm->getConnectedUsers())
        arr.add(String(uid));

    Serial.println("Getting global users");
    // 2. remote users that can be routed via mesh
    for (uint32_t uid : _router->getKnownUserIDs())
        arr.add(String(uid));

    Serial.println("Finished getting users");
}

bool GatewayManager::registerNode()
{
    Serial.print("Registering gateway… ");
    // build payload {"gwId": "<id>"}
    JsonDocument doc;
    doc["gwId"] = String(_me);
    String body;
    serializeJson(doc, body);

    HTTPClient http;
    http.setTimeout(10000);
    http.begin(_api + "/register/node");
    http.addHeader("Content-Type", "application/json");
    int rc = http.POST(body);
    http.end();

    if (rc == 200)
    {
        Serial.println("ok");
        _registered = true;
        return true;
    }
    else
    {
        Serial.printf("failed (%d)\n", rc);
        return false;
    }
}
