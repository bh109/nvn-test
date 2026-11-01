#include "display_adapter.h"
#include "present_clock.h"
#include <cstdio>
extern "C" {
void *gfx_window_handle(void);
void rt_fatal(const char *,...);
}
extern "C" void opensmash_clock_prepare(){present_clock_start();}
extern "C" void opensmash_clock_observe_present(){
    static volatile LONG checked=0;
    static volatile LONG64 window_seen_tick=0;
    HWND window=(HWND)gfx_window_handle();auto before=present_clock_snapshot();
    // The existing renderer creates its window at the first vi presentation.
    if(!window||!before.tick)return;
    // HWND publication precedes device initialization. Do not hold the guest's
    // first present before the old renderer has completed that initialization.
    // Observe progress on the actual vi clock rather than a guessed delay.
    uint64_t seen=(uint64_t)InterlockedCompareExchange64(&window_seen_tick,(LONG64)before.tick,0);
    if(!seen)return;
    if(before.tick-seen<2||InterlockedCompareExchange(&checked,1,0))return;
    opensmash::attach_window(window);
    opensmash_display_set_interval(1);opensmash_display_present_plan(1);
    opensmash_display_pace();opensmash_display_wait_refresh();
    auto after=present_clock_snapshot();auto stats=opensmash::display_adapter_stats();
    if(after.tick<=before.tick||after.tick<stats.target||stats.frames!=1||stats.timeouts||opensmash::attached_window()!=window)rt_fatal("OpenSmash shared window/clock integration check failed");
    fprintf(stderr,"[OpenSmash display] PASS: existing HWND %p; vi refresh %llu -> %llu, target %llu; no extra clock thread\n",window,before.tick,after.tick,stats.target);
}
