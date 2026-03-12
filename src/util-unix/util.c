#include "../util.h"
#include <time.h>
#include <stdlib.h>

uint64_t get_usec() {
    // POSIX portable: man 3 clock_gettime
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000*1000 + ts.tv_nsec / 1000;
}

uint64_t random_timeout_usec(uint32_t min, uint32_t max) {
    if (max <= min) return min;
    return min + (rand() % (max - min));
}
