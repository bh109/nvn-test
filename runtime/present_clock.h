#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct PresentClockSample {uint64_t tick,ns;} PresentClockSample;
// Start the existing vi consumer, not a second refresh thread.
void present_clock_start(void);
// Called by that consumer AFTER signaling the game's vsync event.
void present_clock_publish(uint64_t ns);
PresentClockSample present_clock_snapshot(void);
int present_clock_wait_until(uint64_t tick,uint32_t timeout_ms);
#ifdef __cplusplus
}
#endif
