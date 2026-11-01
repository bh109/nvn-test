#include "host_adapter.h"
#include <windows.h>
#include <cstdio>
#include <initializer_list>
#include "verified_sdk_contract.h" // emitted only after full local SDK comparison
extern "C" {
void rt_call(Cpu *,u64);
void rt_fatal(const char *,...);
u64 rt_symbol(const char *);
struct SdkBuilder {uint32_t flags,target,width,height,depth,levels,format,samples,stride,glname,has_layout,layout;};
uint64_t nvn_sdk_texture_storage_alignment(opensmash::Context *,const SdkBuilder *);
uint64_t nvn_sdk_texture_storage_size(opensmash::Context *,const SdkBuilder *);
uint32_t nvn_sdk_texture_storage_class(opensmash::Context *,const SdkBuilder *);
int nvn_sdk_format_known(uint32_t);
int nvn_sdk_tex_storage_size(opensmash::Context *,const SdkBuilder *,uint32_t *);
}
namespace {
INIT_ONCE once=INIT_ONCE_STATIC_INIT;
uint64_t check_address;
void expect(uint64_t actual,uint64_t expected,const char *name){if(actual!=expected)rt_fatal("OpenSmash SDK check %s: got %#llx, expected %#llx",name,actual,expected);}
void check(opensmash::Context *context){
    SdkBuilder builder={};builder.flags=0x40;builder.target=1;builder.width=builder.height=64;builder.depth=builder.levels=1;builder.format=0x25;
    expect(nvn_sdk_texture_storage_alignment(context,&builder),128,"block alignment");
    builder.flags=0x10;expect(nvn_sdk_texture_storage_alignment(context,&builder),32,"linear alignment");
    builder.stride=256;expect(nvn_sdk_texture_storage_size(context,&builder),16384,"linear storage size");
    unsigned tested=3;
    for(uint32_t format:{0x25u,0x31u,0x3bu}){
        if(!nvn_sdk_format_known(format))rt_fatal("OpenSmash SDK check unknown format %#x",format);
        builder={};builder.target=1;builder.width=128;builder.height=64;builder.depth=builder.levels=1;builder.format=format;
        uint64_t size=nvn_sdk_texture_storage_size(context,&builder);uint32_t texture_size=0;
        if(!size||!nvn_sdk_tex_storage_size(context,&builder,&texture_size)||texture_size!=size)rt_fatal("OpenSmash SDK builder/texture storage mismatch for format %#x",format);
        if((nvn_sdk_texture_storage_class(context,&builder)&~255u)!=256u)rt_fatal("OpenSmash SDK unexpected texture storage class");
        tested+=2;
    }
    fprintf(stderr,"[OpenSmash SDK] PASS: %u actual recompiled SDK storage checks; explicit host callback and mapped SDK model\n",tested);
}
BOOL CALLBACK initialize(PINIT_ONCE,void *,void **){
    if(rt_symbol("nvnBootstrapLoader")!=NVN_VERIFIED_SDK_GUEST_BASE+0x454d90)rt_fatal("OpenSmash SDK contract does not match loaded SDK export");
    for(const auto &signature:nvn_sdk_signatures){
        auto *memory=(const uint8_t *)(uintptr_t)(NVN_VERIFIED_SDK_GUEST_BASE+signature.offset);
        if(memcmp(memory,signature.bytes,sizeof signature.bytes))rt_fatal("OpenSmash SDK loaded code differs at %#llx",signature.offset);
    }
    opensmash::configure_sdk(NVN_VERIFIED_SDK_REFERENCE_BASE,NVN_VERIFIED_SDK_GUEST_BASE,NVN_VERIFIED_SDK_BYTES);
    check_address=hostfn_address("WindowSmashOpenSmashSdkCheck",check);return TRUE;
}
}
extern "C" void opensmash_run_sdk_check(Cpu *cpu){
    InitOnceExecuteOnce(&once,initialize,nullptr,nullptr);
    // Check once per process, before the original SDK bootstrap is called.
    static volatile LONG done=0;if(InterlockedCompareExchange(&done,1,0))return;
    Cpu isolated=*cpu;rt_call(&isolated,check_address);
}
