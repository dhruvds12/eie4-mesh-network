#ifndef DISPLAY_H
#define DISPLAY_H
#include <Arduino.h>
#include "HT_SSD1306Wire.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class DisplayManager {
public:
    struct Msg { uint32_t from; String text; };           // queue payload

    DisplayManager();
    void   initialise(uint32_t nodeId);                   // pass once at boot
    void   setWifi(bool up);                              // WiFi event hook
    void   showMsg(uint32_t fromNode, const char *txt, size_t len);

private:
    static void _task(void *pv);
    void        _render();                                // draw whole frame
    static String _wrap(const String &src, uint8_t rowW); // helper

    QueueHandle_t _q;
    String  _status;                                      // first row
    String  _body;                                        // wrapped text

    static SemaphoreHandle_t _spiMtx;                     // shared with LoRa
public:                                                   // legacy direct access
    SSD1306Wire oled_display;
};
#endif
