#ifndef FREERTOS_H
#define FREERTOS_H

#include <stdlib.h>

// Redirect FreeRTOS memory allocation functions to standard malloc/free.
#define pvPortMalloc(size) malloc(size)
#define vPortFree(ptr) free(ptr)

// Provide a minimal definition for TaskHandle_t.
typedef void* TaskHandle_t;

#endif 
