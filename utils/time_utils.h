#ifndef TIME_UTILS_H
#define TIME_UTILS_H

#include<stdint.h>
#include<time.h>

static inline uint64_t app_now_monotonic_us(void){
    struct timespec ts;
    if(clock_gettime(CLOCK_MONOTONIC,&ts) != 0){
        return 0;
    }
    //tv_sec 和 tv_nsec 是 timespec 的两个字段，分别存整秒和不足一秒的纳秒部分，相加得到完整的微秒时间戳。
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}
#endif