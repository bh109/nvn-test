#define _CRT_SECURE_NO_WARNINGS
#include "host_adapter.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <utility>

extern "C" {
void rt_call(Cpu *,u64);
u64 rt_native(GuestFn *,const char *);
void rt_fatal(const char *,...);
}
namespace {
constexpr unsigned capacity=1024;
struct Entry {char name[160];opensmash::Function function;uint64_t native;volatile LONG64 calls;};
Entry entries[capacity]={};unsigned used=0;
struct ImportEntry {char name[160];opensmash::Function function;};
ImportEntry imports[capacity]={};unsigned import_count=0;
SRWLOCK lock=SRWLOCK_INIT;
struct Active {Entry *entry=nullptr;Cpu *cpu=nullptr;};
thread_local Active active;
struct Scope {
    Active previous;
    Scope(Entry *entry,Cpu *cpu):previous(active){active={entry,cpu};}
    ~Scope(){active=previous;}
};
uint64_t sdk_reference=0,sdk_guest=0,sdk_bytes=0;
opensmash::GameAddress game_addresses[16]={};unsigned game_address_count=0;
uint64_t worker_roots[8]={};unsigned worker_root_count=0;
struct Worker {opensmash::Context *owner=nullptr;uint64_t low=0,high=0;Cpu cpu={};unsigned slot=0;};
thread_local Worker worker;
struct WorkerRange {uint64_t low,high;DWORD thread;};
WorkerRange worker_ranges[32]={};
bool valid_name(const char *name){return name&&*name&&strlen(name)<sizeof entries[0].name;}
void invoke(unsigned index,Cpu *cpu){
    auto &entry=entries[index];InterlockedIncrement64(&entry.calls);
    Scope scope(&entry,cpu);
    auto context=opensmash::from_cpu(*cpu);
    context.in_import=entry.name;context.import_from=cpu->x[30];
    entry.function(&context);opensmash::to_cpu(context,*cpu);
}
template<size_t N> void thunk(Cpu *cpu){invoke((unsigned)N,cpu);}
template<size_t... N> constexpr auto thunks(std::index_sequence<N...>){
    struct Table {GuestFn *fn[sizeof...(N)];};return Table{{&thunk<N>...}};
}
constexpr auto table=thunks(std::make_index_sequence<capacity>{});
}
extern "C" uint64_t hostfn_address(const char *name,opensmash::Function function){
    if(!valid_name(name)||!function)rt_fatal("Invalid OpenSmash host function registration");
    AcquireSRWLockExclusive(&lock);
    for(unsigned i=0;i<used;i++)if(entries[i].function==function&&!strcmp(entries[i].name,name)){
        uint64_t address=entries[i].native;ReleaseSRWLockExclusive(&lock);return address;
    }
    if(used==capacity){ReleaseSRWLockExclusive(&lock);rt_fatal("OpenSmash host function registry exhausted");}
    unsigned index=used++;auto &entry=entries[index];strcpy(entry.name,name);entry.function=function;
    entry.native=rt_native(table.fn[index],entry.name);
    auto address=entry.native;ReleaseSRWLockExclusive(&lock);return address;
}
extern "C" const char *hostfn_current_name(){return active.entry?active.entry->name:"<no OpenSmash callback>";}
extern "C" unsigned long long hostfn_current_calls(){return active.entry?(unsigned long long)InterlockedCompareExchange64(&active.entry->calls,0,0):0;}
extern "C" void register_imports(const opensmash::Import *definitions,int count){
    if(count<0||(!definitions&&count))rt_fatal("Invalid OpenSmash import table");
    AcquireSRWLockExclusive(&lock);
    for(int n=0;n<count;n++){
        const auto &definition=definitions[n];
        if(!valid_name(definition.name)||!definition.function){ReleaseSRWLockExclusive(&lock);rt_fatal("Invalid OpenSmash import");}
        unsigned index=0;while(index<import_count&&strcmp(imports[index].name,definition.name))index++;
        if(index<import_count){
            if(imports[index].function!=definition.function){ReleaseSRWLockExclusive(&lock);rt_fatal("Conflicting OpenSmash import '%s'",definition.name);}
            continue;
        }
        if(import_count==capacity){ReleaseSRWLockExclusive(&lock);rt_fatal("OpenSmash import registry exhausted");}
        auto &entry=imports[import_count++];strcpy(entry.name,definition.name);entry.function=definition.function;
    }
    ReleaseSRWLockExclusive(&lock);
}
opensmash::Function opensmash::find_import(const char *name){
    if(!name)return nullptr;AcquireSRWLockShared(&lock);
    Function found=nullptr;for(unsigned i=0;i<import_count;i++)if(!strcmp(imports[i].name,name)){found=imports[i].function;break;}
    ReleaseSRWLockShared(&lock);return found;
}
extern "C" void call_guest(opensmash::Context *context,uint64_t address){
    // A callback borrows this guest thread's kernel identity, but runs on a
    // separate register context. Never borrow the main thread from a worker.
    Cpu *origin=active.cpu;
    if(worker.owner&&(!origin||!origin->thread)){
        if(!context||(!origin&&context!=worker.owner)||context->x[31]<worker.low+4096||context->x[31]>worker.high||(context->x[31]&15))rt_fatal("OpenSmash shader worker has an invalid private stack/context");
        AcquireSRWLockShared(&lock);bool allowed=false;for(unsigned i=0;i<worker_root_count;i++)if(worker_roots[i]==address)allowed=true;ReleaseSRWLockShared(&lock);
        if(!allowed)rt_fatal("Unapproved shader worker guest entry %#llx",address);
        if(!origin)origin=&worker.cpu;
    }
    if(!context||!origin||!context->x[31])rt_fatal("OpenSmash guest callback has no active guest thread/stack");
    Cpu callback=*origin;opensmash::to_cpu(*context,callback);
    Scope scope(active.entry,&callback);
    rt_call(&callback,address);
    memcpy(context->x,callback.x,sizeof callback.x);context->x[31]=callback.sp;
    memcpy(context->v,callback.v,sizeof callback.v);context->tpidr=callback.tpidr;
}
void opensmash::configure_game_addresses(const GameAddress *addresses,unsigned count){
    if(!addresses||!count||count>16)rt_fatal("Invalid verified game address contract");
    for(unsigned i=0;i<count;i++)if(!addresses[i].reference||!addresses[i].guest)rt_fatal("Invalid verified game address");
    AcquireSRWLockExclusive(&lock);
    bool conflict=game_address_count&&(game_address_count!=count||memcmp(game_addresses,addresses,count*sizeof *addresses));
    if(!conflict){memcpy(game_addresses,addresses,count*sizeof *addresses);game_address_count=count;}
    ReleaseSRWLockExclusive(&lock);if(conflict)rt_fatal("Conflicting verified game address contract");
}
extern "C" uint64_t ga(uint64_t reference){
    AcquireSRWLockShared(&lock);uint64_t result=0;for(unsigned i=0;i<game_address_count;i++)if(game_addresses[i].reference==reference)result=game_addresses[i].guest;
    ReleaseSRWLockShared(&lock);if(!result)rt_fatal("Unverified OpenSmash game address %#llx",reference);return result;
}
void opensmash::configure_worker_roots(const uint64_t *addresses,unsigned count){
    if(!addresses||!count||count>8)rt_fatal("Invalid verified shader worker roots");
    for(unsigned i=0;i<count;i++)if(!addresses[i])rt_fatal("Invalid shader worker entry");
    AcquireSRWLockExclusive(&lock);bool conflict=worker_root_count&&(worker_root_count!=count||memcmp(worker_roots,addresses,count*sizeof *addresses));
    if(!conflict){memcpy(worker_roots,addresses,count*sizeof *addresses);worker_root_count=count;}
    ReleaseSRWLockExclusive(&lock);if(conflict)rt_fatal("Conflicting shader worker roots");
}
extern "C" void opensmash_worker_enter(opensmash::Context *context,void *stack,uint64_t bytes){
    uint64_t low=(uint64_t)(uintptr_t)stack;
    if(active.cpu||worker.owner||!context||!low||bytes<65536||bytes>~0ull-low||context->x[31]<low+4096||context->x[31]>low+bytes||(context->x[31]&15)||context->tpidr)rt_fatal("Invalid shader worker context/private stack registration");
    AcquireSRWLockExclusive(&lock);bool overlap=false;unsigned slot=32;
    for(unsigned i=0;i<32;i++){if(worker_ranges[i].thread&&low<worker_ranges[i].high&&worker_ranges[i].low<low+bytes)overlap=true;if(!worker_ranges[i].thread&&slot==32)slot=i;}
    bool allowed=worker_root_count&&slot<32&&!overlap;
    if(allowed)worker_ranges[slot]={low,low+bytes,GetCurrentThreadId()};
    ReleaseSRWLockExclusive(&lock);if(!allowed)rt_fatal("Unverified, overlapping or exhausted shader worker registration");
    worker={};worker.owner=context;worker.low=low;worker.high=low+bytes;worker.slot=slot;
    // Deliberately no game kernel identity, main-thread TLS or live registers.
}
extern "C" void opensmash_worker_leave(opensmash::Context *context){
    if(active.cpu||!worker.owner||context!=worker.owner)rt_fatal("Invalid shader worker unregister");
    AcquireSRWLockExclusive(&lock);worker_ranges[worker.slot]={};ReleaseSRWLockExclusive(&lock);worker={};
}
void opensmash::configure_sdk(uint64_t reference_base,uint64_t guest_base,uint64_t bytes){
    if(!reference_base||!guest_base||!bytes||bytes>~0ull-reference_base||bytes>~0ull-guest_base)rt_fatal("Invalid verified SDK mapping contract");
    AcquireSRWLockExclusive(&lock);
    bool conflict=sdk_bytes&&(sdk_reference!=reference_base||sdk_guest!=guest_base||sdk_bytes!=bytes);
    if(!conflict){sdk_reference=reference_base;sdk_guest=guest_base;sdk_bytes=bytes;}
    ReleaseSRWLockExclusive(&lock);
    if(conflict)rt_fatal("Conflicting verified SDK mapping contract");
}
uint64_t opensmash::sdk_guest_address(uint64_t reference_address){
    AcquireSRWLockShared(&lock);uint64_t offset=reference_address-sdk_reference;
    bool valid=sdk_bytes&&offset<sdk_bytes;uint64_t address=sdk_guest+offset;
    ReleaseSRWLockShared(&lock);if(!valid)rt_fatal("Unverified or out-of-range OpenSmash SDK address %#llx",reference_address);return address;
}
extern "C" int sdk_image_ready(){AcquireSRWLockShared(&lock);bool ready=sdk_bytes!=0;ReleaseSRWLockShared(&lock);return ready;}
extern "C" void sdk_dispatch(opensmash::Context *context,uint64_t address){
    // The adapted nvn_sdk.c model contains real guest pointers (including SDK
    // vtables), while other upstream callers can still name the reference base.
    AcquireSRWLockShared(&lock);bool mapped=sdk_bytes&&address>=sdk_guest&&address-sdk_guest<sdk_bytes;ReleaseSRWLockShared(&lock);
    call_guest(context,mapped?address:opensmash::sdk_guest_address(address));
}
extern "C" void trace_printf(const char *format,...){va_list args;va_start(args,format);vfprintf(stderr,format,args);va_end(args);}
extern "C" void report_and_exit(int code){rt_fatal("OpenSmash graphics stopped with code %d in %s",code,hostfn_current_name());}
