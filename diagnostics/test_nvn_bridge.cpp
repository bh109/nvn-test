#include "nvn/bridge.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <stdexcept>
#include <thread>
#include <vector>
static GuestFn *registered[1024];static unsigned native_count;
extern "C" u64 rt_native(GuestFn *fn,const char *){registered[native_count]=fn;return 0x7300000000ull+16*native_count++;}
extern "C" void rt_fatal(const char *fmt,...){va_list args;va_start(args,fmt);vfprintf(stderr,fmt,args);va_end(args);throw std::runtime_error("bridge fatal");}
extern "C" void nvn_sdk_bootstrap_original(Cpu *c){c->x[0]=!strcmp((const char *)c->x[0],"nvnDeviceGetProcAddress")?0x200:0x100;}
extern "C" void rt_call(Cpu *c,u64 address){
    if(address>=0x7300000000ull){registered[(address-0x7300000000ull)/16](c);return;}
    if(address==0x200){c->x[0]=!strcmp((const char *)c->x[1],"missing")?0:0x100;return;}
    if(address!=0x100)throw std::runtime_error("unexpected original target");
    c->x[0]=c->x[1]+5;c->v[0].f[0]+=2;
}
static u64 resolve(Cpu &c,const char *name){c.x[0]=(u64)name;nvn_bridge_bootstrap(&c);return c.x[0];}
int main(int argc,char **argv){
    try{
        bool audit=argc>1&&!strcmp(argv[1],"audit");_putenv_s("SSBU_NVN_FRONTEND",audit?"audit":"off");
        Cpu c={};c.sp=0x7700010000;c.tpidr=123;c.tpidrro=456;c.fpcr=789;c.fpsr=12;c.thread=(void *)0x1234;
        u64 function=resolve(c,"nvnExample");c.x[1]=37;c.v[0].f[0]=4;rt_call(&c,function);
        if(c.x[0]!=42||c.v[0].f[0]!=6||c.sp!=0x7700010000||c.tpidr!=123||c.tpidrro!=456||c.fpcr!=789||c.fpsr!=12||c.thread!=(void *)0x1234)throw std::runtime_error("passthrough corrupted CPU state/results");
        if(!audit){if(nvn_bridge_entry_count())throw std::runtime_error("disabled mode registered wrappers");puts("PASS: disabled bridge preserves original SDK target and CPU state");return 0;}
        if(resolve(c,"nvnExample")!=function)throw std::runtime_error("unstable wrapper address");
        u64 resolver=resolve(c,"nvnDeviceGetProcAddress");c.x[1]=(u64)"nvnExample";rt_call(&c,resolver);if(c.x[0]!=function)throw std::runtime_error("device resolver did not share bootstrap wrapper");
        c.x[1]=(u64)"missing";rt_call(&c,resolver);if(c.x[0])throw std::runtime_error("missing function changed from null");
        std::vector<std::thread> threads;for(unsigned n=0;n<8;n++)threads.emplace_back([function]{for(unsigned i=0;i<100;i++){Cpu local={};if(resolve(local,"nvnExample")!=function)std::abort();local.x[1]=10;rt_call(&local,function);if(local.x[0]!=15)std::abort();}});for(auto &t:threads)t.join();
        if(nvn_bridge_entry_count()!=2||native_count!=2)throw std::runtime_error("concurrent lookups duplicated wrappers");
        puts("PASS: NVN bootstrap/device lookup interception, stable concurrent dispatch, null results and CPU state");return 0;
    }catch(const std::exception &e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
