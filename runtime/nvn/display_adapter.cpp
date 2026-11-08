#include "display_adapter.h"
#include "present_clock.h"
#ifdef SSBU_OPENSMASH_PORT
#include "settings_controls.h"
#endif
#include <algorithm>
#include <cstdio>
#ifdef SSBU_OPENSMASH_PORT
extern "C" void opensmash_port_frame(void);
#endif
extern "C" void rt_fatal(const char *,...);
namespace {
SRWLOCK lock=SRWLOCK_INIT;
INIT_ONCE once=INIT_ONCE_STATIC_INIT;
opensmash::FrameClockPlan plan;
opensmash::DisplayAdapterStats stats={};
HWND window;
BOOL CALLBACK start_clock(PINIT_ONCE,void *,void **){present_clock_start();return TRUE;}
void ensure_clock(){InitOnceExecuteOnce(&once,start_clock,nullptr,nullptr);}
}
void opensmash::attach_window(HWND candidate){
    DWORD process=0;if(!candidate||!IsWindow(candidate)||!GetWindowThreadProcessId(candidate,&process)||process!=GetCurrentProcessId())rt_fatal("OpenSmash renderer requires an existing window in this game process");
    AcquireSRWLockExclusive(&lock);
    bool conflict=window&&window!=candidate;if(!conflict)window=candidate;
    ReleaseSRWLockExclusive(&lock);
    if(conflict)rt_fatal("OpenSmash renderer cannot replace its attached game window");
}
HWND opensmash::attached_window(){AcquireSRWLockShared(&lock);HWND result=window;ReleaseSRWLockShared(&lock);return result;}
opensmash::DisplayAdapterStats opensmash::display_adapter_stats(){AcquireSRWLockShared(&lock);auto result=stats;result.target=plan.target;ReleaseSRWLockShared(&lock);return result;}
extern "C" void opensmash_display_fit(int sw,int sh,int dw,int dh,int out[4]){
    if(!out)return;out[0]=out[1]=out[2]=out[3]=0;if(sw<=0||sh<=0||dw<=0||dh<=0)return;
    int width=dw,height=dh;
    if((int64_t)dw*sh>(int64_t)dh*sw)width=(int)((int64_t)dh*sw/sh);
    else height=(int)((int64_t)dw*sh/sw);
    out[0]=(dw-width)/2;out[1]=(dh-height)/2;out[2]=width;out[3]=height;
}
extern "C" void opensmash_display_set_interval(int interval){AcquireSRWLockExclusive(&lock);plan.interval=(unsigned)std::clamp(interval,0,5);if(!plan.interval)plan.pending=false;ReleaseSRWLockExclusive(&lock);}
extern "C" void opensmash_display_present_plan(int vulkan){
    if(!vulkan)rt_fatal("OpenSmash display adapter supports only the Vulkan backend");
    ensure_clock();auto now=present_clock_snapshot().tick;
    AcquireSRWLockExclusive(&lock);plan.plan(now);ReleaseSRWLockExclusive(&lock);
}
extern "C" int opensmash_display_wait_refresh(){
    ensure_clock();AcquireSRWLockExclusive(&lock);
    bool pending=plan.pending;uint64_t target=plan.target;if(pending)plan.pending=false;
    ReleaseSRWLockExclusive(&lock);
    if(!pending||present_clock_snapshot().tick>=target)return 0;
    bool completed=present_clock_wait_until(target,2000)!=0;
    AcquireSRWLockExclusive(&lock);stats.waits++;if(!completed)stats.timeouts++;ReleaseSRWLockExclusive(&lock);
    if(!completed)rt_fatal("OpenSmash display clock stopped before refresh %llu",target);
    return 1;
}
extern "C" void opensmash_display_before_present(){opensmash_display_wait_refresh();}
extern "C" void opensmash_display_pace(){
    ensure_clock();auto now=present_clock_snapshot().tick;
    AcquireSRWLockExclusive(&lock);
    if(!plan.planned)plan.plan(now);plan.planned=false;stats.frames++;
    ReleaseSRWLockExclusive(&lock);
#ifdef SSBU_OPENSMASH_PORT
    opensmash_port_frame();
#endif
}
extern "C" void opensmash_display_present_done(int){AcquireSRWLockExclusive(&lock);stats.shown++;ReleaseSRWLockExclusive(&lock);
#ifdef SSBU_OPENSMASH_PORT
    nvn_settings_presented();
#endif
}
