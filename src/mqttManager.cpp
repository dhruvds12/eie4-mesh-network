#include "mqttManager.h"
#include <string.h>
// TODO: Using old version of the ESP-IDF library do not have MQTT v5
// Initialise static instance pointer to NULL.
MQTTManager *MQTTManager::instance = nullptr;

// Constructor: store configuration and create a FreeRTOS queue.
MQTTManager::MQTTManager(const char *brokerURI, const char *subscribeTopic)
    : brokerURI(brokerURI), subscribeTopic(subscribeTopic), client(nullptr)
{
    // Create a queue capable of holding 10 messages.
    messageQueue = xQueueCreate(10, sizeof(mqtt_message_t));
    if (messageQueue == NULL)
    {
        Serial.println("Failed to create MQTT message queue");
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
    xTaskCreate(MQTTManager::mqttQueueTask, "MQTTQueueTask", 4096, this, 1, NULL);

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
        // Subscribe to the specified topic after connection.
        esp_mqtt_client_subscribe(mgr->client, mgr->subscribeTopic, 0);
        break;

    case MQTT_EVENT_DATA:
    {
        Serial.println("MQTT_EVENT_DATA received");
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

        // Place the message into the queue for later processing.
        if (xQueueSend(mgr->messageQueue, &msg, 0) != pdTRUE)
        {
            Serial.println("Failed to queue MQTT message");
        }
        break;
    }

    default:
        break;
    }
}

// FreeRTOS task function to process messages from the MQTT queue.
void MQTTManager::mqttQueueTask(void *pvParameters)
{
    MQTTManager *mgr = (MQTTManager *)pvParameters;
    mqtt_message_t msg;
    for (;;)
    {
        if (xQueueReceive(mgr->messageQueue, &msg, portMAX_DELAY) == pdTRUE)
        {
            mgr->processMessage(msg);
        }
    }
}

// Process an individual MQTT message. Add your custom logic here.
void MQTTManager::processMessage(const mqtt_message_t &msg)
{
    Serial.printf("Processing MQTT message:\n  Topic: %s\n  Payload: %s\n", msg.topic, msg.payload);
}

MQTTManager mqttManager("mqtt://132.145.67.221:1883", "simulation/to/hardware");
