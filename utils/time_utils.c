#include "time_utils.h"
#include <stdint.h>
#include <linux/time.h>

uint64_t app_now_monotonic_us(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }

    return (uint64_t)ts.tv_sec * 1000000ULL +
           (uint64_t)(ts.tv_nsec / 1000ULL);
}