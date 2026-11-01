#include "host_adapter.h"
#include <windows.h>
#include <cstdlib>
#include <cstdio>
#include <atomic>
#include <algorithm>
#include <vector>
#include "worker_compressed_fixture.h"
#include "verified_game_contract.h"
extern "C" void rt_fatal(const char *,...);
namespace {
INIT_ONCE once=INIT_ONCE_STATIC_INIT;
HANDLE start_event;
uint64_t allocator,deleter,decompress,release;
std::atomic<unsigned> failures{0},frames{0},compressed_frames{0};
void allocate(opensmash::Context *context){context->x[0]=(uint64_t)(uintptr_t)malloc(context->x[1]?(size_t)context->x[1]:1);}
void deallocate(opensmash::Context *context){free((void *)(uintptr_t)context->x[1]);}
BOOL CALLBACK initialize(PINIT_ONCE,void *,void **){
    for(const auto &signature:nvn_game_signatures)if(memcmp((const void *)(uintptr_t)signature.address,signature.bytes,sizeof signature.bytes))rt_fatal("OpenSmash worker loaded main differs at %#llx",signature.address);
    opensmash::configure_game_addresses(nvn_game_addresses,(unsigned)(sizeof nvn_game_addresses/sizeof nvn_game_addresses[0]));
    decompress=ga(0x71039a4040ull);release=ga(0x71039a3e70ull);
    uint64_t roots[]={decompress,release};opensmash::configure_worker_roots(roots,2);
    allocator=hostfn_address("vk_store_zstd_alloc",allocate);deleter=hostfn_address("vk_store_zstd_free",deallocate);return TRUE;
}
// Frames use the raw/RLE encodings defined by upstream Zstandard's format:
// https://github.com/facebook/zstd/blob/dev/doc/zstd_compression_format.md
bool decode(opensmash::Context &context,unsigned worker,unsigned iteration){
    bool compressed=iteration%3==2,rle=iteration%3==1;
    unsigned length=compressed?sizeof worker_compressed_payload:rle?192:96;
    std::vector<uint8_t> source,expected(length),output(length);
    if(compressed){source.assign(worker_compressed_frame,worker_compressed_frame+sizeof worker_compressed_frame);memcpy(expected.data(),worker_compressed_payload,length);}
    else{
        source.resize(9+(rle?1:length));source[0]=0x28;source[1]=0xb5;source[2]=0x2f;source[3]=0xfd;source[4]=0x20;source[5]=(uint8_t)length;
        unsigned block=(length<<3)|(rle?3:1);source[6]=(uint8_t)block;source[7]=(uint8_t)(block>>8);source[8]=(uint8_t)(block>>16);
        for(unsigned i=0;i<length;i++)expected[i]=rle?(uint8_t)(worker+iteration):(uint8_t)(i*7+worker+iteration);
        if(rle)source[9]=expected[0];else memcpy(source.data()+9,expected.data(),length);
    }
    uint64_t input[]={ (uint64_t)(uintptr_t)source.data(),source.size(),0 };
    auto *decoder=(uint8_t *)calloc(1,0x271f0);if(!decoder)return false;
    // Same host-allocated decoder model and allocation hooks as vks_dctx_new.
    memcpy(decoder+28960,&allocator,8);memcpy(decoder+28968,&deleter,8);
    uint64_t largest_window=0x8000001;memcpy(decoder+29064,&largest_window,8);
    uint64_t result=1;size_t produced=0;bool valid=true;
    for(unsigned calls=0;result&&calls<512;calls++){
        uint64_t destination[]={(uint64_t)(uintptr_t)(output.data()+produced),(uint64_t)std::min<size_t>(7,length-produced),0};
        uint64_t previous=input[2];context.x[0]=(uint64_t)(uintptr_t)decoder;context.x[1]=(uint64_t)(uintptr_t)destination;context.x[2]=(uint64_t)(uintptr_t)input;
        call_guest(&context,decompress);result=context.x[0];produced+=(size_t)destination[2];
        if(result>=0xffffffffffffff89ull||produced>length||(result&&!destination[2]&&input[2]==previous)){valid=false;break;}
    }
    valid=valid&&!result&&produced==length&&input[2]==input[1]&&!memcmp(output.data(),expected.data(),length);
    context.x[0]=(uint64_t)(uintptr_t)decoder;call_guest(&context,release);
    if(valid){frames++;if(compressed)compressed_frames++;}return valid;
}
DWORD WINAPI run(void *argument){
    unsigned worker_id=(unsigned)(uintptr_t)argument;
    WaitForSingleObject(start_event,INFINITE);
    constexpr uint64_t stack_bytes=4u<<20;
    auto *stack=(uint8_t *)VirtualAlloc(nullptr,stack_bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!stack){failures++;return 1;}
    DWORD previous;VirtualProtect(stack,4096,PAGE_NOACCESS,&previous);
    opensmash::Context context={};context.x[31]=(uint64_t)(uintptr_t)(stack+stack_bytes-256);
    opensmash_worker_enter(&context,stack,stack_bytes);
    for(unsigned i=0;i<16;i++)if(!decode(context,worker_id,i)){failures++;break;}
    opensmash_worker_leave(&context);VirtualFree(stack,0,MEM_RELEASE);return 0;
}
}
extern "C" void opensmash_run_worker_check(){
    InitOnceExecuteOnce(&once,initialize,nullptr,nullptr);
    static volatile LONG done=0;if(InterlockedCompareExchange(&done,1,0))return;
    start_event=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!start_event)rt_fatal("Cannot create shader worker test gate");
    HANDLE workers[4]={};for(unsigned i=0;i<4;i++){workers[i]=CreateThread(nullptr,0,run,(void *)(uintptr_t)i,0,nullptr);if(!workers[i])rt_fatal("Cannot create shader preparation worker");}
    SetEvent(start_event);DWORD wait=WaitForMultipleObjects(4,workers,TRUE,30000);
    if(wait!=WAIT_OBJECT_0)rt_fatal("Shader preparation worker check did not finish: %#x",wait);
    for(auto handle:workers)CloseHandle(handle);CloseHandle(start_event);
    if(failures||frames!=64||compressed_frames!=20)rt_fatal("OpenSmash shader workers decoded %u/64 frames, %u compressed; %u failures",frames.load(),compressed_frames.load(),failures.load());
    fprintf(stderr,"[OpenSmash workers] PASS: 4 private contexts/stacks, 64 streaming raw/RLE/compressed frames decoded byte-exactly by actual game code (20 compressed); no borrowed kernel thread\n");
}
