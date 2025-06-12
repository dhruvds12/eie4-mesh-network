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
    _router->setGatewayManager(this);
    // TODO: DO these need to be pinned to core 1
    xTaskCreate(syncTask, "GW-SYNC", 8192, this, 1, nullptr);
    xTaskCreate(
        [](void *pv)
        { static_cast<GatewayManager *>(pv)->wifiEventTask(); },
        "GW-WIFI",
        2048, this, 3, nullptr);
}

void GatewayManager::uplink(uint32_t src, uint32_t dst,
                            const uint8_t *data, size_t len)
{
    UplinkMsg m{};

    /* random 12-char msgId */
    for (int i = 0; i < 12; ++i)
    {
        uint8_t v = esp_random() % 36;
        m.id[i] = (v < 10) ? ('0' + v) : ('a' + v - 10);
    }
    m.id[12] = '\0';

    m.from = src;
    m.to = dst;
    m.len = (len > sizeof(m.body)) ? sizeof(m.body) : len;
    memcpy(m.body, data, m.len);

    /* make sure body is NUL-terminated so String() stops cleanly */
    if (m.len < sizeof(m.body))
        m.body[m.len] = '\0';
    else
        m.body[sizeof(m.body) - 1] = '\0';

    xQueueSend(_txQ, &m, 0);
}

bool GatewayManager::isOnline() const
{
    return _registered && (xEventGroupGetBits(_evt) & WIFI_READY);
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
        // if (!self->oneSync())
        //     Serial.println("oneSync() failed; retrying in 2 s");
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
    Serial.printf("Seen: %s", aSeen);

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
        size_t txtLen = strlen(txt);
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
                // TODO store in the userInfo
            }
        }
        else
        {
            Serial.printf("Received gatewat message for user %u\n", dst);
            _nmh->enqueueMessage(MsgKind::FROM_GATEWAY,
                                 dst,
                                 (const uint8_t *)txt, txtLen,
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

void GatewayManager::broadcastUtc()
{
    if (!isOnline())
        return; // Wi-Fi & registered?

    /* ---- throttle attempts ---- */
    const uint32_t ATTEMPT_PAUSE = 5000; // 10 s cadence
    uint32_t nowMs = millis();
    if (nowMs - _lastAttempt < ATTEMPT_PAUSE)
        return;
    _lastAttempt = nowMs;

    /* ---- try to get fresh UTC ---- */
    uint64_t utcMs;
    bool ok = fetchUtc(utcMs);

    if (ok)
    {
        _utcBaseMs = utcMs;
        _millisBase = nowMs;
        _haveUtc = true;
    }
    else if (_haveUtc)
    {
        /* estimate: add elapsed millis to the last known base */
        uint32_t delta = nowMs - _millisBase;
        utcMs = _utcBaseMs + delta;
        // keep _utcBaseMs/_millisBase unchanged – they anchor drift
    }
    else
    {
        Serial.println("[GW] UTC fetch failed (no baseline yet)");
        return; // nothing to send
    }

    /* ---- wrap and broadcast ---- */
    char payload[32];
    snprintf(payload, sizeof(payload),
             "UTC:%llu",
             (unsigned long long)(utcMs / 1000ULL)); // keep it compact

    _router->sendData(BROADCAST_ADDR,
                      reinterpret_cast<uint8_t *>(payload),
                      strlen(payload));
}

bool GatewayManager::fetchUtc(uint64_t &utcMs)
{
    HTTPClient http;
    http.setTimeout(8000);
    http.begin("http://worldtimeapi.org/api/timezone/Etc/UTC");
    int rc = http.GET();
    if (rc != 200)
    {
        http.end();
        return false;
    }

    JsonDocument doc;
    if (deserializeJson(doc, http.getStream()))
    {
        http.end();
        return false;
    }
    http.end();

    uint32_t sec = doc["unixtime"] | 0;
    utcMs = (uint64_t)sec * 1000ULL; // ms since Epoch
    return true;
}
