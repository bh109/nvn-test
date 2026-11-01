#pragma once
#include <stdint.h>
#include <windows.h>
namespace opensmash {
// Pure schedule model. The only source of refreshes is WindowSmash's vi thread.
struct FrameClockPlan {
    uint64_t target=0;
    unsigned interval=1;
    bool pending=false,planned=false;
    void plan(uint64_t now){
        uint64_t step=interval;
        // Keep bounded frame debt after a load/pause; never invent refreshes.
        uint64_t earliest=now==~0ull?now:now+1;
        uint64_t previous=target<=~0ull-step?target+step:~0ull;
        target=step?((previous>earliest&&previous-earliest<=5)?previous:earliest):now;
        pending=step!=0;planned=true;
    }
};
void attach_window(HWND window);
HWND attached_window();
struct DisplayAdapterStats {uint64_t frames,shown,waits,timeouts,target;};
DisplayAdapterStats display_adapter_stats();
}
// Names deliberately differ from WindowSmash's display_* functions. The NVN
// sources must be compiled with explicit symbol mappings at the port boundary.
extern "C" {
void opensmash_display_fit(int sw,int sh,int dw,int dh,int out[4]);
void opensmash_display_set_interval(int interval);
void opensmash_display_present_plan(int vulkan);
int opensmash_display_wait_refresh(void);
void opensmash_display_before_present(void);
void opensmash_display_pace(void);
void opensmash_display_present_done(int fight);
}
