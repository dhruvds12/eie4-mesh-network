#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "IRadioManager.h"
#include <ArduinoJson.h>
#include "NetworkMessageHandler.h"

extern "C"
{
#include "mqtt_client.h"
}

#define MQTT_TOPIC_MAX_LEN 128
#define MQTT_PAYLOAD_MAX_LEN 512

static const uint8_t ACTION_MESSAGE = 0x01;
static const uint8_t ACTION_UPDATE_ROUTE = 0x02;
static const uint8_t ACTION_INVALIDATE_ROUTE = 0x03;

#define REGISTRATION_TOPIC "simulation/register"

typedef struct
{
    char topic[MQTT_TOPIC_MAX_LEN];
    char payload[MQTT_PAYLOAD_MAX_LEN];
    int payload_len;
} mqtt_message_t;

class MQTTManager
{
public:
    // Constructor
    MQTTManager(const char *brokerURI, uint32_t nodeId, IRadioManager *radioManager, NetworkMessageHandler *networkMessageHandler);

    // Begin
    void begin();

    // Publish Messages to any topic
    void publishMessage(const char *topic, const char *payload, int payload_len);

    // Enque to send queue
    void enqueueSendMQTTQueue(const char *payload, int payload_len);

    bool connected = false;

    void publishUpdateRoute(uint32_t dest, uint32_t nextHop, uint8_t hopCount);
    void publishInvalidateRoute(uint32_t dest);
    void publishPacket(uint32_t packetID, const uint8_t *buffer, size_t length);

private:
    const char *brokerURI;
    uint32_t nodeId;
    char commandTopic[MQTT_TOPIC_MAX_LEN];
    char processTopic[MQTT_TOPIC_MAX_LEN];
    char sendMessageTopic[MQTT_TOPIC_MAX_LEN];
    IRadioManager *_radioManager;
    NetworkMessageHandler *_networkHandler;

    esp_mqtt_client_handle_t client;
    QueueHandle_t receivedMQTTMessageQueue;
    QueueHandle_t sendMQTTMessageQueue;

    // static event handler for the esp-idf MQTT client
    static void mqttEventHandler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

    // Task function to process the queue
    static void receivedMQTTQueueTask(void *pvParametere);

    // Task function to process the queue
    static void sendMQTTQueueTask(void *pvParametere);

    // Process an individual MQTT message
    void processMessage(const mqtt_message_t &msg);

    // Singleton pointer to access instance variables from static functions
    // static MQTTManager *instance;
};

extern MQTTManager *mqttManager;

#endif