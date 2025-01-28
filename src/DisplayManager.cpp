#include "DisplayManager.h"

DisplayManager::DisplayManager() 
  : oled_display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED) {
}

void DisplayManager::initialise()
{
  oled_display.init();
  oled_display.clear();
  oled_display.display();
}

void DisplayManager::displayString(int x, int y, String str)
{
  oled_display.drawString(x, y, str);
  oled_display.display();
}

void DisplayManager::displayNewString(int x, int y, String str)
{
  oled_display.clear();
  oled_display.display();
  oled_display.drawString(x, y, str);
  oled_display.display();
}