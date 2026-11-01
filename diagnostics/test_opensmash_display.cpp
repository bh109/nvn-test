#include "nvn/display_adapter.h"
#include "present_clock.h"
#include <cstdio>
#include <stdexcept>
#include <cstdarg>
#include <thread>
#include <atomic>
#include <vector>
static std::atomic<unsigned> starts{0};
extern "C" void present_clock_start(){starts++;} // fixture owns synthetic ticks; no second thread
extern "C" void rt_fatal(const char *fmt,...){char message[512];va_list args;va_start(args,fmt);vsnprintf(message,sizeof message,fmt,args);va_end(args);throw std::runtime_error(message);}
static void require(bool value,const char *text){if(!value)throw std::runtime_error(text);}
int main(){try{
    opensmash::FrameClockPlan schedule;schedule.plan(100);require(schedule.target==101&&schedule.pending,"initial refresh target");
    schedule.plan(100);require(schedule.target==102,"same-tick frame interval lost");
    schedule.plan(500);require(schedule.target==501,"late frame carried refresh debt");
    schedule.interval=2;schedule.plan(501);require(schedule.target==503,"swap interval two");
    schedule.interval=0;schedule.plan(600);require(schedule.target==600&&!schedule.pending,"unpaced frame should not wait");
    schedule.interval=1;schedule.plan(~0ull);require(schedule.target==~0ull,"refresh target overflow");
    int fit[4];opensmash_display_fit(16,9,1920,1200,fit);require(fit[0]==0&&fit[1]==60&&fit[2]==1920&&fit[3]==1080,"letterbox dimensions");
    opensmash_display_fit(16,9,2000,900,fit);require(fit[0]==200&&fit[1]==0&&fit[2]==1600&&fit[3]==900,"pillarbox dimensions");
    opensmash_display_fit(0,9,2000,900,fit);require(!fit[0]&&!fit[1]&&!fit[2]&&!fit[3],"invalid aspect ratio");
    HWND window=CreateWindowExW(0,L"STATIC",L"NVN adapter fixture",0,0,0,16,16,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    require(window!=nullptr,"test window creation");opensmash::attach_window(window);require(opensmash::attached_window()==window,"existing window was not retained");
    bool rejected=false;try{opensmash::attach_window(GetDesktopWindow());}catch(const std::runtime_error &){rejected=true;}require(rejected,"foreign window accepted");
    auto before=present_clock_snapshot();require(!before.tick,"clock invented initial ticks");
    opensmash_display_present_plan(1);opensmash_display_pace();require(opensmash::display_adapter_stats().target==1,"pace double-planned refresh");
    std::atomic<unsigned> waiting{0},completed{0};std::vector<std::thread> waiters;
    for(unsigned n=0;n<4;n++)waiters.emplace_back([&]{waiting++;if(present_clock_wait_until(1,1000))completed++;});
    while(waiting<4)Sleep(1);
    require(!present_clock_snapshot().tick,"waiting generated a refresh");
    present_clock_publish(1000);for(auto &thread:waiters)thread.join();require(completed==4,"refresh did not wake all consumers");
    require(opensmash_display_wait_refresh()==0,"already completed refresh slept again");
    require(!present_clock_wait_until(2,10)&&present_clock_snapshot().tick==1,"timeout generated a refresh");
    opensmash_display_present_plan(1);opensmash_display_pace();
    std::thread producer([]{Sleep(20);present_clock_publish(2000);});
    require(opensmash_display_wait_refresh()==1,"frame pacing did not wait for vi refresh");producer.join();
    require(present_clock_snapshot().tick==2&&present_clock_snapshot().ns==2000,"published clock sample changed");
    opensmash_display_present_done(0);auto stats=opensmash::display_adapter_stats();require(stats.frames==2&&stats.shown==1&&stats.waits==1&&!stats.timeouts,"frame statistics diverged");
    opensmash_display_set_interval(0);opensmash_display_present_plan(1);opensmash_display_pace();require(!opensmash_display_wait_refresh(),"interval zero blocked");
    require(starts==1,"adapter started refresh service multiple times");DestroyWindow(window);
    puts("PASS: shared refresh waits, multi-consumer wakeups, no synthetic ticks, bounded frame debt, intervals, aspect fit and existing-window attachment");return 0;
}catch(const std::exception &error){fprintf(stderr,"FAIL: %s\n",error.what());return 1;}}
