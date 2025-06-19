#include "DisplayManager.h"

SemaphoreHandle_t DisplayManager::_spiMtx = nullptr;

DisplayManager::DisplayManager()
: oled_display(0x3c, 500000, SDA_OLED, SCL_OLED,
               GEOMETRY_128_64, RST_OLED),
  _q(nullptr) {}

void DisplayManager::initialise(uint32_t nodeId)
{
    if (!_spiMtx) _spiMtx = xSemaphoreCreateMutex();
    oled_display.init(); oled_display.clear(); oled_display.display();

    /* --- build status bar text ----------------------------------- */
    char tmp[24];                                                 // enough
    snprintf(tmp, sizeof(tmp), "NODE %-4lu  WiFi:N",
             static_cast<unsigned long>(nodeId));
    _status = String(tmp);                                        // assign

    _q = xQueueCreate(4, sizeof(Msg));
    xTaskCreate(_task, "OLED", 2048, this, 1, nullptr);
}

void DisplayManager::setWifi(bool up)
{
    _status.setCharAt(_status.length()-1, up ? 'Y' : 'N');
    _body = String();              // keep current message
    _render();
}

void DisplayManager::showMsg(uint32_t from, const char *txt, size_t len)
{
    Msg m{from, String(txt).substring(0, len)};
    xQueueSend(_q, &m, 0);         // drop if queue full
}

String DisplayManager::_wrap(const String &s, uint8_t cols)
{
    String out; uint8_t n=0;
    for (auto c: s) {
        if (n==cols) { out += '\n'; n=0; }
        out += c; ++n;
    }
    return out;
}

void DisplayManager::_task(void *pv)
{
    auto self = static_cast<DisplayManager*>(pv);
    Msg m;
    for (;;) {
        if (xQueueReceive(self->_q, &m, portMAX_DELAY) == pdPASS) {
            self->_body = _wrap(String("From ") + m.from + ": " + m.text, 25);
            self->_render();
        }
    }
}

void DisplayManager::_render()
{
    if (xSemaphoreTake(_spiMtx, portMAX_DELAY)!=pdTRUE) return;
    oled_display.clear();

    oled_display.setFont(ArialMT_Plain_10);
    oled_display.drawString(0, 0, _status);              // status bar

    oled_display.setFont(ArialMT_Plain_10);
    oled_display.drawString(0, 12, _body);               // message

    oled_display.display();
    xSemaphoreGive(_spiMtx);
}
