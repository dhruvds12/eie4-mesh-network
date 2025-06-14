#ifndef DISPLAY_H
#define DISPLAY_H
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "HT_SSD1306Wire.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class DisplayManager {
public:
    DisplayManager();
    void initialise();

    // NEW: enqueue a string from *any* task
    void post(const String &txt);

private:
    static void _task(void *pv);
    QueueHandle_t _q;               // 4-deep queue, each = 32-byte buffer
public:                             // keep old low-level access if you need it
    SSD1306Wire oled_display;
};
#endif
