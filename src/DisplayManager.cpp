#include "DisplayManager.h"

DisplayManager::DisplayManager()
    : oled_display(0x3c, 500000, SDA_OLED, SCL_OLED,
                   GEOMETRY_128_64, RST_OLED) {}

void DisplayManager::initialise()
{
  oled_display.init();
  oled_display.clear();
  oled_display.display();

  _q = xQueueCreate(4, sizeof(char[32]));
  xTaskCreate(_task, "OLED", 2048, this, 1, nullptr);
}

void DisplayManager::post(const String &txt)
{
  if (_q == nullptr)
    return;
  char buf[32] = {0};
  txt.substring(0, 31).toCharArray(buf, sizeof(buf));
  xQueueSend(_q, &buf, 0); // fail-safe: drop if queue full
}

void DisplayManager::_task(void *pv)
{
  auto self = static_cast<DisplayManager *>(pv);
  char buf[32];

  for (;;)
  {
    if (xQueueReceive(self->_q, &buf, portMAX_DELAY) == pdPASS)
    {
      self->oled_display.clear();
      self->oled_display.drawString(0, 0, String(buf));
      self->oled_display.display();
    }
  }
}
