#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <atomic>
#include "native_fence.h"
namespace nativevk {
static std::atomic<bool> enabled{false};
static std::atomic<uint64_t> calls{0},waits{0},short_timeouts{0},notifications{0},failures{0},wait_ns{0};
void configure_fence_sleep(bool value){enabled.store(value,std::memory_order_relaxed);}
void pause_fence(void *word,uint32_t target,uint64_t remaining_us){
    if(!enabled.load(std::memory_order_relaxed))return;
    calls.fetch_add(1,std::memory_order_relaxed);
    // Keep the original sub-millisecond polling path and timeout checks. A
    // bounded wait also tolerates writers that do not emit a notification.
    if(remaining_us<1000){short_timeouts.fetch_add(1,std::memory_order_relaxed);return;}
    if(!word||((uintptr_t)word&3))return;
    uint32_t current=*(volatile uint32_t *)word;
    if((int32_t)(current-target)>=0)return;
    LARGE_INTEGER start,end,frequency;QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&start);
    waits.fetch_add(1,std::memory_order_relaxed);
    if(!WaitOnAddress(word,&current,sizeof current,1)&&GetLastError()!=ERROR_TIMEOUT)failures.fetch_add(1,std::memory_order_relaxed);
    QueryPerformanceCounter(&end);wait_ns.fetch_add((uint64_t)(end.QuadPart-start.QuadPart)*1000000000ull/(uint64_t)frequency.QuadPart,std::memory_order_relaxed);
    // No result is fabricated: the original SDK loop rechecks the word and
    // its original clock/deadline after this returns, including spurious wakes.
}
void notify_fence(void *word){if(enabled.load(std::memory_order_relaxed)){notifications.fetch_add(1,std::memory_order_relaxed);WakeByAddressAll(word);}}
FenceSleepStats fence_sleep_stats(){return {calls.load(),waits.load(),short_timeouts.load(),notifications.load(),failures.load(),wait_ns.load()};}
}
