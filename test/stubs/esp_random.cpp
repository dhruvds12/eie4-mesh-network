#include <cstdlib>
#include <stdint.h>

extern "C" uint32_t esp_random() {
    return (uint32_t)rand();
}
