#define _CRT_SECURE_NO_WARNINGS
#include "port_runtime.h"
#include "host_adapter.h"
#include "display_adapter.h"
#include "present_clock.h"
#include "verified_sdk_contract.h"
#include "verified_game_contract.h"
#include <cstdio>
#include <cstdlib>
extern "C" {
void rt_fatal(const char *,...);void rt_call(Cpu *,u64);
void gfx_nvn_ensure_window(void);
void register_nvn_imports(void);void nvn_gl_start(void *);void nvn_gl_report(FILE *);
void nvn_gl_memory_written(uint64_t,uint64_t);void nvn_front_memory_retire(uint64_t,uint64_t);
void opensmash_optional_policy(void);extern volatile LONG g_display_close;
void watchdog_present(void);extern u64 gfx_stat_presented;
}
namespace {
INIT_ONCE once=INIT_ONCE_STATIC_INIT;
u64 bootstrap=0;
volatile LONG closed=0;
volatile LONG port_ready=0;
BOOL CALLBACK initialize(PINIT_ONCE,void *parameter,void **){
    HWND window=(HWND)parameter;opensmash::attach_window(window);
    for(const auto &signature:nvn_sdk_signatures)if(memcmp((const void *)(uintptr_t)(NVN_VERIFIED_SDK_GUEST_BASE+signature.offset),signature.bytes,sizeof signature.bytes))rt_fatal("NVN port loaded SDK mismatch");
    for(const auto &signature:nvn_game_signatures)if(memcmp((const void *)(uintptr_t)signature.address,signature.bytes,sizeof signature.bytes))rt_fatal("NVN port loaded main mismatch");
    opensmash::configure_sdk(NVN_VERIFIED_SDK_REFERENCE_BASE,NVN_VERIFIED_SDK_GUEST_BASE,NVN_VERIFIED_SDK_BYTES);
    opensmash::configure_game_addresses(nvn_game_addresses,(unsigned)(sizeof nvn_game_addresses/sizeof nvn_game_addresses[0]));
    u64 roots[]={ga(0x71039a4040ull),ga(0x71039a3e70ull)};opensmash::configure_worker_roots(roots,2);
    opensmash_optional_policy();present_clock_start();
    register_nvn_imports();auto function=opensmash::find_import("nvnBootstrapLoader");if(!function)rt_fatal("Upstream NVN bootstrap missing");
    bootstrap=hostfn_address("nvnBootstrapLoader",function);
    fprintf(stderr,"[NVN port] START: OpenSmash NVN frontend and Vulkan renderer; WindowSmash networking retained\n");fflush(stderr);
    nvn_gl_start(window);InterlockedExchange(&port_ready,1);return TRUE;
}
}
extern "C" int opensmash_port_selected(){const char *mode=getenv("SSBU_NVN_FRONTEND");return mode&&!strcmp(mode,"opensmash");}
extern "C" void opensmash_port_start(void *window){InitOnceExecuteOnce(&once,initialize,window,nullptr);}
extern "C" void opensmash_port_bootstrap(Cpu *cpu){gfx_nvn_ensure_window();if(!bootstrap)rt_fatal("NVN port did not initialize");rt_call(cpu,bootstrap);}
extern "C" void opensmash_port_memory_written(u64 address,u64 bytes){if(port_ready)nvn_gl_memory_written(address,bytes);}
extern "C" void opensmash_port_memory_retire(u64 address,u64 bytes){if(port_ready)nvn_front_memory_retire(address,bytes);}
extern "C" void opensmash_port_report(){auto stats=opensmash::display_adapter_stats();fprintf(stderr,"[NVN port] %llu game frames, %llu presentations, %llu clock waits, %llu timeouts\n",stats.frames,stats.shown,stats.waits,stats.timeouts);}
extern "C" void opensmash_port_close(){InterlockedExchange(&g_display_close,1);if(port_ready&&!InterlockedExchange(&closed,1))nvn_gl_report(stderr);}
extern "C" void opensmash_port_frame(){
    static uint64_t limit=getenv("SSBU_NVN_EXIT_FRAME")?strtoull(getenv("SSBU_NVN_EXIT_FRAME"),nullptr,10):0;
    static uint64_t logged=0;auto stats=opensmash::display_adapter_stats();
    gfx_stat_presented=stats.frames;watchdog_present();
    if(stats.frames>=logged+60){logged=stats.frames;opensmash_port_report();}
    if(limit&&stats.frames>=limit){fprintf(stderr,"[NVN port] bounded run completed: %llu frames\n",stats.frames);opensmash_port_close();fflush(stdout);fflush(stderr);ExitProcess(0);}
}
