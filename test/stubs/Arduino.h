#ifndef ARDUINO_H
#define ARDUINO_H

#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <stdint.h>

extern "C" uint32_t esp_random() {
    return (uint32_t)rand();
}

class SerialClass {
public:
    void println(const char *s) { printf("%s\n", s); }
    void printf(const char *fmt, ...) {
        va_list args;
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
    }
};

extern SerialClass Serial;

SerialClass Serial;

#endif // ARDUINO_H
