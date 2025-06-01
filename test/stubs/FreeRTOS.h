#ifndef FREERTOS_H
#define FREERTOS_H

/*  A *very* small subset of FreeRTOS that’s sufficient for unit-testing
    on the host.  Nothing here does real scheduling – we just give the
    compiler the symbols it asks for.                                   */

#include <cstdint>
#include <cstdlib>

/* ───── basic scalar types ─────────────────────────────────────────── */
typedef void*      QueueHandle_t;
typedef void*      SemaphoreHandle_t;
typedef void*      TimerHandle_t;
typedef void*      TaskHandle_t;
typedef void*      EventGroupHandle_t;
typedef uint32_t   EventBits_t;
typedef uint32_t   TickType_t;

/* ───── common RTOS constants / macros ─────────────────────────────── */
#ifndef pdTRUE
#  define pdTRUE   1
#  define pdFALSE  0
#endif

#define pdPASS             1
#define pdFAIL             0
#define portMAX_DELAY      0xFFFFFFFFu
#define pdMS_TO_TICKS(ms)  (ms)          /* 1 ms == 1 tick on host */

/* Used by xTaskNotifyFromISR – no-op in the stub */
#define eSetBits           0
#define portYIELD_FROM_ISR(x)  do { (void)(x); } while(0)

/* ───── stub functions – all trivially succeed ─────────────────────── */
inline TickType_t xTaskGetTickCount(void)
{
    static TickType_t tick{};   /* monotonic counter */
    return ++tick;
}

/* ---------- semaphore ------------------------------------------------ */
/*  Under the host-side stub we don’t need a real semaphore – any
    unique, non-zero pointer value is good enough.                       */
inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex()
{
    /* Allocate one byte so the pointer is distinct from nullptr.
       (You can also just return a fixed non-zero integer cast to a
        pointer, but allocating makes ASan / Valgrind happier.)         */
    return std::malloc(1);
}
inline int  xSemaphoreTakeRecursive   (SemaphoreHandle_t, TickType_t) { return pdTRUE; }
inline int  xSemaphoreGiveRecursive   (SemaphoreHandle_t)            { return pdTRUE; }
inline int  xSemaphoreTake            (SemaphoreHandle_t, TickType_t) { return pdTRUE; }
inline int  xSemaphoreGive            (SemaphoreHandle_t)            { return pdTRUE; }

/* ---------- queues --------------------------------------------------- */
inline QueueHandle_t xQueueCreate(size_t, size_t)                       { return nullptr; }
inline int xQueueSend   (QueueHandle_t, const void*, TickType_t)        { return pdTRUE; }
inline int xQueueReceive(QueueHandle_t,       void*, TickType_t)        { return pdFALSE; }

/* ---------- tasks ---------------------------------------------------- */
inline int xTaskCreate(void (*)(void*), const char*, uint16_t,
                       void*, int, TaskHandle_t*)                       { return pdPASS; }
inline int xTaskNotifyWait(uint32_t, uint32_t, uint32_t*, TickType_t)   { return pdFALSE; }
inline int xTaskNotifyFromISR(TaskHandle_t, uint32_t, int, int*)        { return pdFALSE; }
inline void vTaskDelay(TickType_t)                                      {}

/* ---------- timers --------------------------------------------------- */
inline TimerHandle_t xTimerCreate(const char*, TickType_t, bool,
                                  void*, void (*)(TimerHandle_t))       { return nullptr; }
inline int xTimerStart(TimerHandle_t, TickType_t)                       { return pdPASS; }
inline void* pvTimerGetTimerID(TimerHandle_t)                           { return nullptr; }

/* ---------- event groups -------------------------------------------- */
inline EventGroupHandle_t xEventGroupCreate()                           { return nullptr; }
inline EventBits_t xEventGroupSetBits(EventGroupHandle_t, EventBits_t)  { return 0; }

/* ---------- heap ----------------------------------------------------- */
#define pvPortMalloc  std::malloc
#define vPortFree     std::free

#if !defined(configASSERT)
#  include <cassert>
#  define configASSERT(x)  assert(x)     
#endif

#endif 
