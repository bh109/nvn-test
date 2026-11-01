#include "present_clock.h"
#include <windows.h>
namespace {
SRWLOCK lock=SRWLOCK_INIT;
CONDITION_VARIABLE changed=CONDITION_VARIABLE_INIT;
PresentClockSample sample={};
}
extern "C" void present_clock_publish(uint64_t ns){
    AcquireSRWLockExclusive(&lock);sample.tick++;sample.ns=ns;
    WakeAllConditionVariable(&changed);ReleaseSRWLockExclusive(&lock);
}
extern "C" PresentClockSample present_clock_snapshot(){
    AcquireSRWLockShared(&lock);auto result=sample;ReleaseSRWLockShared(&lock);return result;
}
extern "C" int present_clock_wait_until(uint64_t tick,uint32_t timeout_ms){
    ULONGLONG start=GetTickCount64();AcquireSRWLockExclusive(&lock);
    while(sample.tick<tick){
        ULONGLONG elapsed=GetTickCount64()-start;
        if(elapsed>=timeout_ms){ReleaseSRWLockExclusive(&lock);return 0;}
        DWORD remaining=(DWORD)(timeout_ms-elapsed);
        if(!SleepConditionVariableSRW(&changed,&lock,remaining,0)&&GetLastError()!=ERROR_TIMEOUT){ReleaseSRWLockExclusive(&lock);return 0;}
    }
    ReleaseSRWLockExclusive(&lock);return 1;
}
