#ifndef FREERTOS_H
#define FREERTOS_H

#include <stdlib.h>

// Redirect FreeRTOS memory allocation functions to standard malloc/free.
#define pvPortMalloc(size) malloc(size)
#define vPortFree(ptr) free(ptr)

// Provide a minimal definition for TaskHandle_t.
typedef void *TaskHandle_t;
typedef void *TimerHandle_t;

typedef uint32_t TickType_t;

inline TickType_t xTaskGetTickCount(void)
{
    static TickType_t tick = 0;
    return tick++;
}

#endif
