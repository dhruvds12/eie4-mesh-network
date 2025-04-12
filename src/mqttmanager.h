#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

extern "C"
{
#include "mqtt_client.h"
}

#define MQTT_TOPIC_MAX_LEN 128
#define MQTT_PAYLOAD_MAX_LEN 512

#define REGISTRATION_TOPIC "simulation/register"


typedef struct
{
    char topic[MQTT_TOPIC_MAX_LEN];
    char payload[MQTT_PAYLOAD_MAX_LEN];
} mqtt_message_t;

class MQTTManager
{
public:
    // Constructor
    MQTTManager(const char *brokerURI, const char *subscribeTopic);

    // Begin
    void begin();

    // Publish Messages to any topic
    void publishMessage(const char *topic, const char *payload);

private:
    const char *brokerURI;
    const char *subscribeTopic;

    esp_mqtt_client_handle_t client;
    QueueHandle_t messageQueue;

    // static event handler for the esp-idf MQTT client
    static void mqttEventHandler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

    // Task function to process the queue
    static void mqttQueueTask(void *pvParametere);

    // Process an individual MQTT message
    void processMessage(const mqtt_message_t &msg);

    // Singleton pointer to access instance variables from static functions
    static MQTTManager *instance;
};

extern MQTTManager mqttManager;

#endif