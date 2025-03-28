#ifndef PINGPONGROUTER_H
#define PINGPONGROUTER_H

#include <Arduino.h>
#include <FreeRTOS.h>
#include <queue.h>
#include "IRadioManager.h"

/**
 * @brief A simple ping-pong router that processes incoming packets and replies.
 *
 * This task blocks on the RX queue from the RadioManager.
 * If it sees a "PING", it enqueues a "PONG" response into the TX queue.
 */
class PingPongRouter
{
public:
    /**
     * @brief Construct a new Ping Pong Router object.
     * @param radioManager A pointer to the RadioManager that owns the RX/TX queues.
     */
    PingPongRouter(IRadioManager *radioManager);

    /**
     * @brief Initialize and create the router task.
     * @return true if task creation was successful.
     */
    bool begin();

private:
    IRadioManager *_radioManager;
    TaskHandle_t _routerTaskHandle;

    // The router task function.
    static void routerTask(void *pvParameters);
};

#endif // PINGPONGROUTER_H
