#define _CRT_SECURE_NO_WARNINGS
#include "nvn/host_adapter.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <stdexcept>
#include <thread>
#include <atomic>
#include <vector>
static GuestFn *functions[2048];static unsigned function_count;
static void require(bool condition,const char *message){if(!condition)throw std::runtime_error(message);}
extern "C" uint64_t rt_native(GuestFn *fn,const char *){unsigned i=function_count++;functions[i]=fn;return 0x7300000000ull+16*i;}
extern "C" void rt_fatal(const char *format,...){char message[512];va_list args;va_start(args,format);vsnprintf(message,sizeof message,format,args);va_end(args);throw std::runtime_error(message);}
static thread_local void *expected_thread;
static uint64_t nested_address;
extern "C" void rt_call(Cpu *cpu,uint64_t address){
    if(address>=0x7300000000ull&&address<0x7300000000ull+function_count*16){functions[(address-0x7300000000ull)/16](cpu);return;}
    if(cpu->thread)require(cpu->thread==expected_thread&&cpu->tpidrro==0x888&&cpu->fpcr==0x123,"guest callback lost kernel/TLS/FP state");
    else require(!expected_thread&&!cpu->tpidrro&&!cpu->fpcr,"worker borrowed kernel/TLS/FP state");
    if(address==0x100){rt_call(cpu,nested_address);return;}
    require(address==0x71094278b0ull,"wrong SDK dispatch mapping");cpu->x[0]=256;
}
static void inner(opensmash::Context *context){require(!strcmp(hostfn_current_name(),"inner"),"inner callback name lost");context->x[0]+=10;context->v[0].f[0]+=2;}
static void outer(opensmash::Context *context){
    require(!strcmp(hostfn_current_name(),"outer"),"outer callback name lost");
    require(context->in_import&&!strcmp(context->in_import,"outer"),"import context metadata missing");
    call_guest(context,0x100);
    require(!strcmp(hostfn_current_name(),"outer"),"reentrant callback failed to restore outer name");
    sdk_dispatch(context,0x75004278b0ull);require(context->x[0]==256,"SDK return value lost");
    sdk_dispatch(context,0x71094278b0ull);require(context->x[0]==256,"mapped SDK model pointer rejected");
}
static void throwing(opensmash::Context *){throw std::runtime_error("fixture callback failure");}
extern "C" void register_nvn_imports(void);
extern "C" unsigned fixture_pool_made,fixture_retired,fixture_flushed;
static uint64_t resolve(Cpu &cpu,uint64_t resolver,const char *name){cpu.x[0]=(uint64_t)name;rt_call(&cpu,resolver);return cpu.x[0];}
int main(){
    try{
        bool rejected=false;try{opensmash::sdk_guest_address(0x75004278b0ull);}catch(const std::runtime_error &){rejected=true;}require(rejected&&!sdk_image_ready(),"unverified SDK accepted");
        opensmash::configure_sdk(0x7500000000ull,0x7109000000ull,0xcfb000);
        require(sdk_image_ready()&&opensmash::sdk_guest_address(0x7500000000ull)==0x7109000000ull,"SDK mapping not configured");
        for(uint64_t bad:{0x74ffffffffull,0x7500cfb000ull}){rejected=false;try{opensmash::sdk_guest_address(bad);}catch(const std::runtime_error &){rejected=true;}require(rejected,"SDK bounds not checked");}
        opensmash::Context no_thread={};no_thread.x[31]=0x7700001000;rejected=false;try{call_guest(&no_thread,0x100);}catch(const std::runtime_error &){rejected=true;}require(rejected,"worker borrowed an unrelated guest thread");
        nested_address=hostfn_address("inner",inner);uint64_t entry=hostfn_address("outer",outer);
        require(hostfn_address("outer",outer)==entry,"host function address not stable");
        auto run=[entry]{Cpu cpu={};cpu.sp=0x7700001000;cpu.tpidrro=0x888;cpu.fpcr=0x123;cpu.thread=expected_thread=(void *)0x456;cpu.x[0]=7;cpu.v[0].f[0]=3;rt_call(&cpu,entry);require(cpu.x[0]==256&&cpu.v[0].f[0]==5&&cpu.thread==expected_thread&&cpu.tpidrro==0x888,"callback state did not roundtrip");require(!strcmp(hostfn_current_name(),"<no OpenSmash callback>"),"callback metadata leaked");};
        run();std::vector<std::thread> threads;for(int i=0;i<8;i++)threads.emplace_back([&]{for(int j=0;j<100;j++)run();});for(auto &thread:threads)thread.join();
        Cpu cpu={};cpu.sp=0x7700001000;cpu.tpidrro=0x888;cpu.fpcr=0x123;cpu.thread=expected_thread=(void *)0x456;
        uint64_t fail=hostfn_address("throwing",throwing);try{rt_call(&cpu,fail);}catch(const std::runtime_error &){}require(!strcmp(hostfn_current_name(),"<no OpenSmash callback>"),"exception leaked active context");
        opensmash::GameAddress mapping[]={{0x71039a4040ull,0x100}};opensmash::configure_game_addresses(mapping,1);require(ga(0x71039a4040ull)==0x100,"verified GA mapping lost");
        rejected=false;try{ga(0x71039a4044ull);}catch(const std::runtime_error &){rejected=true;}require(rejected,"unverified GA address accepted");
        uint64_t worker_entries[]={0x100};opensmash::configure_worker_roots(worker_entries,1);
        auto run_worker=[](){
            auto *stack=(uint8_t *)VirtualAlloc(nullptr,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);require(stack!=nullptr,"worker stack allocation");
            opensmash::Context context={};context.x[31]=(uint64_t)(stack+65536-256);context.x[0]=2;expected_thread=nullptr;
            opensmash_worker_enter(&context,stack,65536);
            call_guest(&context,ga(0x71039a4040ull));require(context.x[0]==12,"worker/native callback did not roundtrip");
            bool blocked=false;try{call_guest(&context,0x200);}catch(const std::runtime_error &){blocked=true;}require(blocked,"unapproved worker root allowed");
            context.x[31]=(uint64_t)(stack+16);blocked=false;try{call_guest(&context,0x100);}catch(const std::runtime_error &){blocked=true;}require(blocked,"out-of-bounds worker stack allowed");context.x[31]=(uint64_t)(stack+65536-256);
            std::atomic<bool> overlap_blocked{false};std::thread overlap([&]{opensmash::Context other=context;try{opensmash_worker_enter(&other,stack,65536);}catch(const std::runtime_error &){overlap_blocked=true;}});overlap.join();require(overlap_blocked,"two workers shared a stack");
            opensmash_worker_leave(&context);blocked=false;try{call_guest(&context,0x100);}catch(const std::runtime_error &){blocked=true;}require(blocked,"unregistered worker kept calling guest");
            VirtualFree(stack,0,MEM_RELEASE);
        };
        threads.clear();for(int n=0;n<8;n++)threads.emplace_back(run_worker);for(auto &thread:threads)thread.join();
        expected_thread=(void *)0x456;
        // Call unmodified OpenSmash nvn_stub.c through the real host adapter.
        register_nvn_imports();auto bootstrap=opensmash::find_import("nvnBootstrapLoader");require(bootstrap!=nullptr,"upstream bootstrap not registered");
        uint64_t loader=hostfn_address("nvnBootstrapLoader",bootstrap);
        uint64_t builder=0x222,pool=0x333,buffer_builder=0x444,buffer=0x555;uint8_t memory[512]={};
        uint64_t set=resolve(cpu,loader,"nvnMemoryPoolBuilderSetStorage");cpu.x[0]=builder;cpu.x[1]=(uint64_t)memory;cpu.x[2]=sizeof memory;rt_call(&cpu,set);
        uint64_t init=resolve(cpu,loader,"nvnMemoryPoolInitialize");cpu.x[0]=pool;cpu.x[1]=builder;rt_call(&cpu,init);require(cpu.x[0]==1&&fixture_pool_made==1,"actual upstream memory pool initialization failed");
        uint64_t map=resolve(cpu,loader,"nvnMemoryPoolMap");cpu.x[0]=pool;rt_call(&cpu,map);require(cpu.x[0]==(uint64_t)memory,"upstream guest memory pool address changed");
        set=resolve(cpu,loader,"nvnBufferBuilderSetStorage");cpu.x[0]=buffer_builder;cpu.x[1]=pool;cpu.x[2]=64;cpu.x[3]=128;rt_call(&cpu,set);
        init=resolve(cpu,loader,"nvnBufferInitialize");cpu.x[0]=buffer;cpu.x[1]=buffer_builder;rt_call(&cpu,init);require(cpu.x[0]==1,"upstream buffer initialize failed");
        map=resolve(cpu,loader,"nvnBufferMap");cpu.x[0]=buffer;rt_call(&cpu,map);require(cpu.x[0]==(uint64_t)(memory+64),"upstream buffer offset lost");
        set=resolve(cpu,loader,"nvnBufferFlushMappedRange");cpu.x[0]=buffer;cpu.x[1]=4;cpu.x[2]=8;rt_call(&cpu,set);require(fixture_flushed==1,"upstream flush notification missing");
        set=resolve(cpu,loader,"nvnMemoryPoolFinalize");cpu.x[0]=pool;rt_call(&cpu,set);require(fixture_retired==1,"upstream retirement callback missing");
        puts("PASS: host registry, reentrant guest callbacks, SDK dispatch bounds, thread isolation and actual upstream NVN pool/buffer calls");return 0;
    }catch(const std::exception &error){fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}
