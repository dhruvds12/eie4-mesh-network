#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "HT_SSD1306Wire.h"

class DisplayManager
{
public:
    DisplayManager();
    void initialise();
    void displayString(int x, int y, String str);
    void displayNewString(int x, int y, String str);

    // Not private because it is used in the LoRaManager
    // Procides direct access to the display
    SSD1306Wire oled_display;
};
#endif