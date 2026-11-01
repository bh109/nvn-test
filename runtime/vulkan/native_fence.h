#pragma once
#include <cstdint>
namespace nativevk {
struct FenceSleepStats {uint64_t calls,waits,short_timeouts,notifications,failures,wait_ns;};
void configure_fence_sleep(bool enabled);
void pause_fence(void *word,uint32_t target,uint64_t remaining_us);
void notify_fence(void *word);
FenceSleepStats fence_sleep_stats();
}
