#define _CRT_SECURE_NO_WARNINGS
#include "host_adapter.h"
#include "display_adapter.h"
#include "module.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <dbghelp.h>
extern "C" {
void rt_fatal(const char *,...);
extern const RtModule rt_module_main;
struct PortFunction {uint64_t address;opensmash::Function function;};
// Diagnostics only: the reference watchdog reads these addresses, never calls
// these metadata function pointers using the foreign Context ABI.
const PortFunction *opensmash_fn_table=(const PortFunction *)rt_module_main.funcs;
int opensmash_fn_table_count=(int)rt_module_main.func_count;
volatile LONG g_display_close=0;
int g_hitch_ms=0;
const char *volatile g_hitch_rt=nullptr;
volatile uint64_t g_hitch_rt_arg=0;
}
static_assert(sizeof(PortFunction)==sizeof(RtFunc),"Function metadata ABI");
namespace {
volatile LONG64 clock_index=0,submits=0,early_skips=0;
void disabled(const char *what){rt_fatal("OpenSmash port feature is explicitly disabled: %s",what);}
}
extern "C" void opensmash_optional_policy(){
    for(const char *name:{"SSBU_L1_PIPE","SSBU_L1_EARLY","SSBU_MENU","SSBU_RUNAHEAD","SSBU_PACE_COUNTWAIT"}){
        const char *value=getenv(name);if(value&&atoi(value))disabled(name);
    }
    if(getenv("SSBU_SPEED"))disabled("custom game speed");
    _putenv_s("SSBU_L1_PIPE","0");_putenv_s("SSBU_MENU","0");
    fprintf(stderr,"[NVN port] normal frame clock; early/deferred present, runahead, upstream UI/OSK, album encoder and upstream hitch tracing disabled\n");
}
extern "C" void opensmash_display_clock_sample(uint64_t address){InterlockedExchange64(&clock_index,(LONG64)address);}
extern "C" int opensmash_display_l1_early(){return 0;}
extern "C" void opensmash_display_l1_early_skip(){InterlockedIncrement64(&early_skips);}
extern "C" double opensmash_display_l1_ms(){return (double)GetTickCount64();}
extern "C" int opensmash_display_l1_pipe_default(){return 0;}
extern "C" void opensmash_display_l1_submit(){InterlockedIncrement64(&submits);}
extern "C" int opensmash_display_l1_trace(){return 0;}
extern "C" int opensmash_display_present_defer(){return 0;}
extern "C" int opensmash_display_present_due(){return 0;}
extern "C" void opensmash_display_present_flushed(){}
extern "C" void opensmash_display_title_note(const char *note){char title[320];snprintf(title,sizeof title,"WindowSmash (NVN Vulkan port)%s%.240s",note&&*note?" - ":"",note?note:"");if(auto window=opensmash::attached_window())SetWindowTextA(window,title);}
extern "C" int opensmash_menu_enabled(){return 0;}
extern "C" int opensmash_menu_frame(int,int,void *){return 0;}
extern "C" const unsigned char *opensmash_menu_font_rgba(int *,int *){disabled("upstream menu font");return nullptr;}
extern "C" int opensmash_menu_shot_wanted(char *,int){return 0;}
extern "C" void opensmash_menu_shot_done(const char *,int,int,int){disabled("upstream menu screenshot");}
extern "C" int opensmash_osk_active(){return 0;}
extern "C" void opensmash_osk_size(int *w,int *h){if(w)*w=0;if(h)*h=0;}
extern "C" int opensmash_osk_take(uint32_t *,LONG *){return 0;}
extern "C" uint64_t album_movie_native_window(){return 0;}
extern "C" void album_movie_frame_source_attach(){}
extern "C" int album_movie_push_frame(const uint8_t *,int,int,int,int){disabled("album video encoding");return 0;}
extern "C" int r1_on(){return 0;}
extern "C" int r1_ra_phase(){return 0;}
extern "C" int r1_ra_present_mode(){return 0;}
extern "C" int r1_ra_quiet(){return 0;}
extern "C" void hitch_wait_render(int){}
extern "C" void hitch_frame(long long,double,int){}
extern "C" void hitch_set_render(){}
extern "C" void hitch_report(FILE *out){fputs("[NVN port] upstream hitch tracing disabled\n",out);}
extern "C" void input_frame_draws(int){} // reference input-layer diagnostics; our HLE input remains active
extern "C" const char *guest_thread_name_of(DWORD id){
    thread_local char text[256];text[0]=0;HANDLE thread=OpenThread(THREAD_QUERY_LIMITED_INFORMATION,FALSE,id);if(!thread)return nullptr;
    PWSTR wide=nullptr;if(SUCCEEDED(GetThreadDescription(thread,&wide))&&wide){WideCharToMultiByte(CP_UTF8,0,wide,-1,text,sizeof text,nullptr,nullptr);LocalFree(wide);}CloseHandle(thread);return text[0]?text:nullptr;
}
extern "C" int diag_report_path(char *out,size_t size,const char *suffix){
    const char *root=getenv("SSBU_NVN_REPORT_DIR");if(!root||!*root)root="build/nvn-opensmash-port/reports";
    CreateDirectoryA(root,nullptr);SYSTEMTIME time;GetLocalTime(&time);
    int n=snprintf(out,size,"%s/nvn_%04u%02u%02u_%02u%02u%02u_%lu%s",root,time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,GetCurrentProcessId(),suffix?suffix:"");return n>0&&(size_t)n<size;
}
extern "C" int diag_minidump(EXCEPTION_POINTERS *exception,const char *){
    const char *mode=getenv("SSBU_MINIDUMP");if(!mode||strcmp(mode,"freeze"))return 0;
    char path[MAX_PATH];if(!diag_report_path(path,sizeof path,".dmp"))return 0;
    HANDLE file=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)return 0;
    MINIDUMP_EXCEPTION_INFORMATION info={GetCurrentThreadId(),exception,FALSE};BOOL ok=MiniDumpWriteDump(GetCurrentProcess(),GetCurrentProcessId(),file,MiniDumpNormal,exception?&info:nullptr,nullptr,nullptr);CloseHandle(file);return ok!=FALSE;
}
