#include "mqttManager.h"
#include <cstdio>

// TODO: Using old version of the ESP-IDF library do not have MQTT v5
// WIP - likely will need to clone esp-idf v5.1 and add arduino as a component to maintain functionality

extern uint32_t getNodeID();

// Constructor: store configuration and create a FreeRTOS queue.
MQTTManager::MQTTManager(const char *brokerURI, uint32_t nodeId, IRadioManager *radioManager, NetworkMessageHandler *networkMessageHandler)
    : brokerURI(brokerURI), nodeId(nodeId), _radioManager(radioManager), _networkHandler(networkMessageHandler), client(nullptr)
{
    // Create a queue capable of holding 10 messages.
    receivedMQTTMessageQueue = xQueueCreate(10, sizeof(mqtt_message_t));
    if (receivedMQTTMessageQueue == NULL)
    {
        Serial.println("Failed to create receive MQTT message queue");
    }

    sendMQTTMessageQueue = xQueueCreate(10, sizeof(mqtt_message_t));
    if (sendMQTTMessageQueue == NULL)
    {
        Serial.println("Failed to create send MQTT message queue");
    }
    // Save the singleton instance pointer.
}

// Begin: initialise and start the MQTT client and create the task for processing messages.
void MQTTManager::begin()
{
    // create the mqtt topics
    snprintf(commandTopic, sizeof(commandTopic), "physical/node%u/command", nodeId);
    snprintf(processTopic, sizeof(processTopic), "physical/node%u/process_message", nodeId);
    snprintf(sendMessageTopic, sizeof(sendMessageTopic), "physical/node%u/send_message", nodeId);

    // Configure the MQTT client using the provided broker URI and enable MQTT v5 -> using an old version of mqtt as old version of espidf
    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.uri = brokerURI;
    mqtt_cfg.user_context = this;
    mqtt_cfg.protocol_ver = MQTT_PROTOCOL_V_3_1_1;

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, MQTTManager::mqttEventHandler, this);
    esp_mqtt_client_start(client);

    // Create a FreeRTOS task to process incoming MQTT messages.
    xTaskCreate(MQTTManager::receivedMQTTQueueTask, "ReceiveMQTTQueueTask", 4096, this, 3, NULL);
    xTaskCreate(MQTTManager::sendMQTTQueueTask, "SendMQTTQueueTask", 4096, this, 2, NULL);

    Serial.println("MQTT Manager started.");
}

// Publish a message to a given topic.
void MQTTManager::publishMessage(const char *topic, const char *payload, int payload_len)
{
    if (client == nullptr)
    {
        Serial.println("MQTT client not initialized");
        return;
    }
    esp_mqtt_client_publish(client, topic, payload, payload_len, 0, 0);
}

void MQTTManager::enqueueSendMQTTQueue(const char *payload, int payload_len)
{
    mqtt_message_t msg;
    // Ensure we do not exceed the buffer size.
    int p_len = (payload_len < MQTT_PAYLOAD_MAX_LEN) ? payload_len : MQTT_PAYLOAD_MAX_LEN;
    memcpy(msg.payload, payload, p_len);
    msg.payload_len = p_len;
    // For binary data, we omit null termination unless required.
    msg.payload[p_len] = '\0';

    // Enqueue to the send queue (NOT the received queue).
    if (xQueueSend(sendMQTTMessageQueue, &msg, 0) != pdTRUE)
    {
        Serial.println("Failed to queue MQTT send message");
    }
}

void MQTTManager::publishUpdateRoute(uint32_t destination, uint32_t nextHop, uint8_t hopCount)
{
    if (connected)
    {
        // Allocate a JSON document (adjust size as needed)
        JsonDocument doc;
        doc["action"] = ACTION_UPDATE_ROUTE;
        doc["destination"] = destination;
        doc["next_hop"] = nextHop;
        doc["hop_count"] = hopCount;

        // Serialize JSON to a buffer (or directly to a String)
        char jsonBuffer[255];
        size_t n = serializeJson(doc, jsonBuffer);

        // Manually set the null terminator (if there is room).
        // if (n < sizeof(jsonBuffer))
        //     jsonBuffer[n] = '\0';
        // else
        //     jsonBuffer[sizeof(jsonBuffer) - 1] = '\0';

        // Serial.print("[MQTTManager] Raw bytes: ");
        // for (size_t i = 0; i < n; i++)
        // {
        //     Serial.printf("%02x ", (unsigned char)jsonBuffer[i]);
        // }
        // Serial.println();

        // Serial.println("[MQTTManager] Queued update route");
        enqueueSendMQTTQueue(jsonBuffer, n);
    }
}

void MQTTManager::publishInvalidateRoute(uint32_t destination)
{
    if (connected)
    {
        // Allocate a JSON document (adjust size as needed)
        JsonDocument doc;
        doc["action"] = ACTION_INVALIDATE_ROUTE;
        doc["destination"] = destination;

        // Serialize JSON to a buffer (or directly to a String)
        char jsonBuffer[256];
        size_t n = serializeJson(doc, jsonBuffer);
        Serial.println("[MQTTManager] Queued invalidate route");
        enqueueSendMQTTQueue(jsonBuffer, n);
    }
}

void MQTTManager::publishPacket(uint32_t packetID, const uint8_t *buffer, size_t length)
{
    if (connected)
    {
        JsonDocument doc;
        doc["action"] = ACTION_MESSAGE;
        doc["packet_id"] = packetID;

        // Using MsgPackBinary to wrap our binary payload.
        // Alternative would be use base64 encoding
        doc["payload"] = MsgPackBinary(buffer, length);
        doc["payload_len"] = length;

        char jsonBuffer[512];
        size_t n = serializeMsgPack(doc, jsonBuffer);
        // Serial.println("[MQTTManager] Queued packet");

        // Serial.print("[MQTTManager] Raw bytes: ");
        // for (size_t i = 0; i < n; i++)
        // {
        //     Serial.printf("%02x ", (unsigned char)jsonBuffer[i]);
        // }
        // Serial.println();

        enqueueSendMQTTQueue(jsonBuffer, n);
    }
}

void MQTTManager::publishUserAdded(uint32_t userID)
{
    if (connected)
    {
        JsonDocument doc;
        doc["action"] = ACTION_USER_ADDED;
        doc["user_id"] = userID;

        char jsonBuffer[256];
        size_t n = serializeJson(doc, jsonBuffer);
        Serial.println("[MQTTManager] Queued new user");
        enqueueSendMQTTQueue(jsonBuffer, n);
    }
}

// Static event handler called on MQTT events.
void MQTTManager::mqttEventHandler(void *handler_args, esp_event_base_t base,
                                   int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    // Retrieve the instance pointer.
    auto mgr = static_cast<MQTTManager *>(handler_args);

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        Serial.println("MQTT_EVENT_CONNECTED");
        mgr->connected = true;
        esp_mqtt_client_subscribe(mgr->client, mgr->processTopic, 0);
        esp_mqtt_client_subscribe(mgr->client, mgr->sendMessageTopic, 0);
        // Build the registration message.
        // Format: {"node_id": "node123", "command_topic": "physical/node123/command",
        //          "status_topic": "physical/node123/status", "event": "register", "lat": 1000, "long": 1000}
        {
            JsonDocument doc;
            doc["node_id"] = mgr->nodeId;
            doc["command_topic"] = mgr->commandTopic;
            doc["process_topic"] = mgr->processTopic;
            doc["send_topic"] = mgr->sendMessageTopic;
            doc["event"] = "register";
            doc["lat"] = 1000;
            doc["long"] = 1000;

            char regMsg[256];
            size_t rn = serializeJson(doc, regMsg);
            esp_mqtt_client_publish(mgr->client, REGISTRATION_TOPIC, regMsg, rn, 0, 0);
            Serial.printf("Published registration message: %s\n", regMsg);
        }
        break;

    case MQTT_EVENT_DATA:
    {
        Serial.printf("MQTT_EVENT_DATA received: data_len=%d\n", event->data_len);
        mqtt_message_t msg;
        memset(&msg, 0, sizeof(msg));

        // Copy topic ensuring buffer is not overrun.
        int t_len = (event->topic_len < MQTT_TOPIC_MAX_LEN - 1) ? event->topic_len : MQTT_TOPIC_MAX_LEN - 1;
        memcpy(msg.topic, event->topic, t_len);
        msg.topic[t_len] = '\0'; // not required for binary data

        // Copy payload ensuring buffer is not overrun.
        int p_len = (event->data_len < MQTT_PAYLOAD_MAX_LEN - 1) ? event->data_len : MQTT_PAYLOAD_MAX_LEN - 1;
        memcpy(msg.payload, event->data, p_len);
        msg.payload[p_len] = '\0'; // not required for binary data
        msg.payload_len = p_len;
        for (int i = 0; i < p_len; i++)
        {
            Serial.printf("%02x", msg.payload[i]);
        }
        Serial.println();

        // Place the message into the queue for later processing.
        if (xQueueSend(mgr->receivedMQTTMessageQueue, &msg, 0) != pdTRUE)
        {
            Serial.println("Failed to queue MQTT message");
        }
        break;
    }

    case MQTT_EVENT_DISCONNECTED:
        // set some variable so that mqtt actions are stopped
        mgr->connected = false;
        break;

    default:
        break;
    }
}

// FreeRTOS task function to process messages from the MQTT queue.
void MQTTManager::receivedMQTTQueueTask(void *pvParameters)
{
    MQTTManager *mgr = (MQTTManager *)pvParameters;
    mqtt_message_t msg;
    for (;;)
    {
        if (xQueueReceive(mgr->receivedMQTTMessageQueue, &msg, portMAX_DELAY) == pdTRUE)
        {
            mgr->processMessage(msg);
        }
    }
}

void MQTTManager::sendMQTTQueueTask(void *pvParameters)
{
    MQTTManager *mgr = (MQTTManager *)pvParameters;
    mqtt_message_t msg;
    for (;;)
    {
        if (xQueueReceive(mgr->sendMQTTMessageQueue, &msg, portMAX_DELAY) == pdTRUE)
        {
            mgr->publishMessage(mgr->commandTopic, msg.payload, msg.payload_len);
        }
    }
}

// Process an individual MQTT message. Add your custom logic here.
void MQTTManager::processMessage(const mqtt_message_t &msg)
{
    if (strncmp(msg.topic, processTopic, strlen(processTopic)) == 0)
    {
        Serial.printf("Processing via radioManager: topic=%s\n", msg.topic);
        bool ok = _radioManager->enqueueRxPacket((const uint8_t *)msg.payload, msg.payload_len);
        Serial.println(ok ? "Enqueued RX packet" : "Failed to enqueue RX packet");
    }
    else if (strncmp(msg.topic, sendMessageTopic, strlen(sendMessageTopic)) == 0)
    {
        JsonDocument doc;
        auto err = deserializeJson(doc, msg.payload, msg.payload_len);
        if (err)
        {
            Serial.printf("Failed to parse send_message JSON: %s\n", err.c_str());
            return;
        }
        uint32_t dest = doc["destination"];
        const char *mt = doc["message"];
        if (!_networkHandler->enqueueMessage(dest, false, mt))
        {
            Serial.println("Failed to enqueue network message");
        }
    }
    else
    {
        Serial.printf("Unknown topic: %s\n", msg.topic);
    }
}
