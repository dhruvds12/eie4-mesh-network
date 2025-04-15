#include "mqttManager.h"
#include <string.h>

// TODO: Using old version of the ESP-IDF library do not have MQTT v5
// WIP - likely will need to clone esp-idf v5.1 and add arduino as a component to maintain functionality

extern uint32_t getNodeID();

// Initialise static instance pointer to NULL.
MQTTManager *MQTTManager::instance = nullptr;

// Constructor: store configuration and create a FreeRTOS queue.
MQTTManager::MQTTManager(const char *brokerURI, const char *subscribeTopic, IRadioManager *radioManager)
    : brokerURI(brokerURI), subscribeTopic(subscribeTopic), client(nullptr), _radioManager(radioManager)
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
    instance = this;
}

// Begin: initialise and start the MQTT client and create the task for processing messages.
void MQTTManager::begin()
{
    // Configure the MQTT client using the provided broker URI and enable MQTT v5.
    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.uri = brokerURI;
    mqtt_cfg.protocol_ver = MQTT_PROTOCOL_V_3_1_1;

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, MQTTManager::mqttEventHandler, client);
    esp_mqtt_client_start(client);

    // Create a FreeRTOS task to process incoming MQTT messages.
    xTaskCreate(MQTTManager::receivedMQTTQueueTask, "ReceiveMQTTQueueTask", 4096, this, 3, NULL);
    xTaskCreate(MQTTManager::sendMQTTQueueTask, "ReceiveMQTTQueueTask", 4096, this, 3, NULL);

    Serial.println("MQTT Manager started.");
}

// Publish a message to a given topic.
void MQTTManager::publishMessage(const char *topic, const char *payload)
{
    if (client == nullptr)
    {
        Serial.println("MQTT client not initialized");
        return;
    }
    esp_mqtt_client_publish(client, topic, payload, 0, 0, 0);
}

void MQTTManager::enqueueSendMQTTQueue(const char *payload, int payload_len)
{
    mqtt_message_t msg;
    // Ensure we do not exceed the buffer size.
    int p_len = (payload_len < MQTT_PAYLOAD_MAX_LEN) ? payload_len : MQTT_PAYLOAD_MAX_LEN;
    memcpy(msg.payload, payload, p_len);
    msg.payload_len = p_len;
    // For binary data, we omit null termination unless required.
    // msg.payload[p_len] = '\0';

    // Enqueue to the send queue (NOT the received queue).
    if (xQueueSend(sendMQTTMessageQueue, &msg, 0) != pdTRUE)
    {
        Serial.println("Failed to queue MQTT send message");
    }
    Serial.println("Sent message over MQTT");
}

// Static event handler called on MQTT events.
void MQTTManager::mqttEventHandler(void *handler_args, esp_event_base_t base,
                                   int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    // Retrieve the instance pointer.
    MQTTManager *mgr = MQTTManager::instance;

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        Serial.println("MQTT_EVENT_CONNECTED");
        mgr->connected = true;
        // Subscribe to the configured subscription topic.
        esp_mqtt_client_subscribe(mgr->client, mgr->subscribeTopic, 0);
        snprintf(mgr->sendTopic, sizeof(mgr->sendTopic), "physical/node%d/command", getNodeID());
        // Build the registration message.
        // Format: {"node_id": "node123", "command_topic": "physical/node123/command",
        //          "status_topic": "physical/node123/status", "event": "register", "lat": 1000, "long": 1000}
        {
            char reg_msg[256];
            uint32_t nodeId = getNodeID();
            // Here we use sprintf to embed the nodeID into the registration JSON.
            sprintf(reg_msg,
                    "{\"node_id\": %u, "
                    "\"command_topic\": \"physical/node%u/command\", "
                    "\"status_topic\": \"physical/node%u/status\", "
                    "\"event\": \"register\", "
                    "\"lat\": 1000, \"long\": 1000}",
                    nodeId, nodeId, nodeId);

            // Publish the registration message to the registration topic.
            esp_mqtt_client_publish(mgr->client, REGISTRATION_TOPIC, reg_msg, 0, 0, 0);
            Serial.printf("Published registration message: %s\n", reg_msg);
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
        msg.topic[t_len] = '\0';

        // Copy payload ensuring buffer is not overrun.
        int p_len = (event->data_len < MQTT_PAYLOAD_MAX_LEN - 1) ? event->data_len : MQTT_PAYLOAD_MAX_LEN - 1;
        memcpy(msg.payload, event->data, p_len);
        msg.payload[p_len] = '\0';
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
            mgr->publishMessage(mgr->sendTopic, msg.payload);
        }
    }
}

// Process an individual MQTT message. Add your custom logic here.
void MQTTManager::processMessage(const mqtt_message_t &msg)
{
    Serial.printf("Processing MQTT message:\n  Topic: %s\n  Payload: %s\n", msg.topic, msg.payload);
    bool success = _radioManager->enqueueRxPacket((const uint8_t *)msg.payload, msg.payload_len);
    if (success)
    {
        Serial.println("Enqueued RX packet successfully!");
    }
    else
    {
        Serial.println("Failed to enqueue RX packet!");
    }
}
