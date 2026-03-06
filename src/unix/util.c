#include "../util.h"
#include "time.h"

uint64_t get_usec() {
    // ns (10e9) -> usec (10e6)
    return clock_gettime_nsec_np(CLOCK_MONOTONIC) / 1000;
}
