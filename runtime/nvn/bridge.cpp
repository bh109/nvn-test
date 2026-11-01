#include "bridge.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#ifdef SSBU_OPENSMASH_PORT
#include "port_runtime.h"
#endif

extern "C" {
void rt_call(Cpu *,u64);
u64 rt_native(GuestFn *,const char *);
void rt_fatal(const char *,...);
void nvn_sdk_bootstrap_original(Cpu *);
#ifdef SSBU_NVN_SDK_CHECK
void opensmash_run_sdk_check(Cpu *);
#endif
#ifdef SSBU_NVN_CLOCK_CHECK
void opensmash_clock_prepare(void);
void opensmash_clock_observe_present(void);
#endif
#ifdef SSBU_NVN_WORKER_CHECK
void opensmash_run_worker_check(void);
#endif
}

namespace {
constexpr unsigned capacity=1024;
struct Entry {char name[160];u64 original,native;volatile LONG64 calls;};
Entry entries[capacity]={};
unsigned used=0;
SRWLOCK lock=SRWLOCK_INIT;
INIT_ONCE once=INIT_ONCE_STATIC_INIT;
bool auditing=false;
BOOL CALLBACK initialize(PINIT_ONCE,void *,void **){
    const char *mode=getenv("SSBU_NVN_FRONTEND");
#ifdef SSBU_OPENSMASH_PORT
    if(opensmash_port_selected())return TRUE;
#endif
    if(mode&&*mode&&strcmp(mode,"off")&&strcmp(mode,"audit"))
        rt_fatal("NVN frontend '%s' is not implemented; supported modes: off, audit",mode);
    auditing=mode&&!strcmp(mode,"audit");
    if(auditing){fprintf(stderr,"[NVN bridge] transparent SDK audit; original graphics and WindowSmash networking retained\n");atexit(nvn_bridge_report);}
    return TRUE;
}
u64 intercept(const char *,u64);
void invoke(unsigned index,Cpu *c){
    auto &entry=entries[index];
    auto count=InterlockedIncrement64(&entry.calls);
    // Resolve names before invoking: x0 will contain the resolved address on return.
    char resolved[160]={};
    bool resolver=!strcmp(entry.name,"nvnDeviceGetProcAddress");
    if(resolver){const char *name=(const char *)(uintptr_t)c->x[1];if(!name||strlen(name)>=sizeof resolved)rt_fatal("Invalid NVN procedure name");strcpy(resolved,name);}
    if(count==1)fprintf(stderr,"[NVN bridge] call %s\n",entry.name);
    rt_call(c,entry.original);
#ifdef SSBU_NVN_CLOCK_CHECK
    const char *clock_check=getenv("SSBU_NVN_CLOCK_CHECK");
    if(clock_check&&!strcmp(clock_check,"1")&&!strcmp(entry.name,"nvnQueuePresentTexture"))opensmash_clock_observe_present();
#endif
    if(resolver&&c->x[0])c->x[0]=intercept(resolved,c->x[0]);
}
template<size_t N> void thunk(Cpu *c){invoke((unsigned)N,c);}
template<size_t... N> constexpr auto thunks(std::index_sequence<N...>){
    struct Table {GuestFn *fn[sizeof...(N)];};return Table{{&thunk<N>...}};
}
constexpr auto table=thunks(std::make_index_sequence<capacity>{});
u64 intercept(const char *name,u64 original){
    if(!original)return 0;
    if(!name||strlen(name)>=sizeof entries[0].name)rt_fatal("Invalid NVN procedure name");
    AcquireSRWLockExclusive(&lock);
    for(unsigned i=0;i<used;i++)if(entries[i].original==original&&!strcmp(entries[i].name,name)){
        u64 result=entries[i].native;ReleaseSRWLockExclusive(&lock);return result;
    }
    if(used==capacity){ReleaseSRWLockExclusive(&lock);rt_fatal("NVN bridge exhausted %u entry slots",capacity);}
    unsigned index=used++;auto &entry=entries[index];strcpy(entry.name,name);entry.original=original;
    entry.native=rt_native(table.fn[index],entry.name);
    fprintf(stderr,"[NVN bridge] resolve %s -> %#llx\n",entry.name,entry.original);
    u64 result=entry.native;ReleaseSRWLockExclusive(&lock);return result;
}
}
extern "C" void nvn_bridge_bootstrap(Cpu *c){
    InitOnceExecuteOnce(&once,initialize,nullptr,nullptr);
#ifdef SSBU_OPENSMASH_PORT
    if(opensmash_port_selected()){opensmash_port_bootstrap(c);return;}
#endif
#ifdef SSBU_NVN_CLOCK_CHECK
    const char *clock_check=getenv("SSBU_NVN_CLOCK_CHECK");
    if(clock_check&&!strcmp(clock_check,"1"))opensmash_clock_prepare();
#endif
#ifdef SSBU_NVN_SDK_CHECK
    const char *check=getenv("SSBU_NVN_SDK_CHECK");
    if(check&&!strcmp(check,"1"))opensmash_run_sdk_check(c);
#endif
#ifdef SSBU_NVN_WORKER_CHECK
    const char *worker_check=getenv("SSBU_NVN_WORKER_CHECK");
    if(worker_check&&!strcmp(worker_check,"1"))opensmash_run_worker_check();
#endif
    if(!auditing){nvn_sdk_bootstrap_original(c);return;}
    char name[160];const char *source=(const char *)(uintptr_t)c->x[0];
    if(!source||strlen(source)>=sizeof name)rt_fatal("Invalid NVN bootstrap name");
    strcpy(name,source);nvn_sdk_bootstrap_original(c);
    if(c->x[0])c->x[0]=intercept(name,c->x[0]);
}
extern "C" unsigned nvn_bridge_entry_count(){AcquireSRWLockShared(&lock);unsigned count=used;ReleaseSRWLockShared(&lock);return count;}
extern "C" void nvn_bridge_report(){
    AcquireSRWLockShared(&lock);fprintf(stderr,"[NVN bridge] summary: %u resolved entries (SDK passthrough)\n",used);
    for(unsigned i=0;i<used;i++)fprintf(stderr,"[NVN bridge] total %s %llu\n",entries[i].name,(unsigned long long)InterlockedCompareExchange64(&entries[i].calls,0,0));
    ReleaseSRWLockShared(&lock);
}
// Replaces only the public SDK bootstrap root in the isolated candidate build.
extern "C" void f_7109454d90(Cpu *c){nvn_bridge_bootstrap(c);}
