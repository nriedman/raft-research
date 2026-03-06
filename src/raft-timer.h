#pragma once

#include "util.h"
#include <stdint.h>

typedef struct {
    uint64_t start_usec;
    uint64_t duration_usec;
} timer_t;

static inline void timer_reset(timer_t *timer) {
    timer->start_usec = get_usec();
}

static inline int timer_expired(timer_t *timer) {
    return (get_usec() - timer->start_usec) >= timer->duration_usec;
}

static inline uint64_t time_remaining_usec(timer_t *timer) {
    uint64_t time_passed_usec = get_usec() - timer->start_usec;
    if (time_passed_usec >= timer->duration_usec) {
        return 0;
    }
    return timer->duration_usec - time_passed_usec;
}
