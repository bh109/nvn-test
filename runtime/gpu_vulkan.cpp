/* Experimental direct Vulkan implementation of gpu_backend.h.
 * Guest buffers are snapshotted per draw and compute writes are read back at
 * dispatch boundaries initially. This deliberately favors coherence over speed.
 */
#include "vulkan/native_maxwell.h"
#include "vulkan/native_present.h"
#include "vulkan/native_watch.h"
#include "vulkan/native_buffers.h"
#include "vulkan/native_fence.h"
#include "vulkan/native_descriptor_slots.h"
#include "vulkan/native_snapshot_cache.h"
#include "vulkan/native_vertex_input.h"
extern "C" {
#include "gpu_backend.h"
#include "gpu_shader.h"
#include "gpu_texture.h"
#include "display.h"
#include "debug_profile.h"
void watchdog_present(void);
int gfx_native_toggle_instance_batch(void);
}
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <unordered_map>
#include <memory>
#include <stdexcept>
#include <functional>
#ifdef SSBU_OPENSMASH_PORT
#include "nvn/port_runtime.h"
#include "nvn/online_menu.h"
#endif

using namespace nativevk;
static_assert(offsetof(::DrawParams,debug_draw_id)==sizeof(nativevk::DrawParams),"Vulkan and Maxwell translator parameter layouts must match");
extern "C" {
u64 gfx_stat_clears,gfx_stat_presented,gfx_stat_busy_ns,gfx_stat_upload_bytes,gfx_stat_gpu_waits,gfx_stat_composed,gfx_stat_upload_kind[4];
u64 gfx_stat_shaders,gfx_stat_shader_failures,gfx_stat_first_draws,gfx_stat_first_slow,gfx_stat_reused,gfx_stat_reuse_compared;
u64 gfx_stat_textures,gfx_stat_watch_us,gfx_stat_unpacked,gfx_stat_residents,gfx_stat_resident_pages,gfx_stat_residents_retired;
u64 gfx_stat_drawn,gfx_stat_skipped,gfx_skip_shader,gfx_skip_topology,gfx_skip_target,gfx_stat_kept_mismatches,gfx_stat_kept_made,gfx_stat_kept_applied,gfx_stat_kept_reloaded;
u64 gfx_stat_dispatched,gfx_stat_unmapped,gfx_stat_unmap_dropped,gfx_stat_blits;
}
namespace {
Device device;
Presentation presentation;
INIT_ONCE once=INIT_ONCE_STATIC_INIT;
CRITICAL_SECTION lock;
HWND window;
volatile LONG ready,keys;
DWORD last_thread;
float scale=1;
uint32_t instance_index;
bool logging;
WriteWatch memory_watch(ALIAS_BASE,ALIAS_SIZE);
BufferCache buffer_cache(device,memory_watch,getenv("SSBU_VK_APPEND_CACHE")&&strcmp(getenv("SSBU_VK_APPEND_CACHE"),"0"));
IndexBoundsCache index_bounds_cache(memory_watch);
ReadbackPool compute_readbacks(device);
ComputeBatch compute_batch(device);
bool batch_compute=false;
static bool hardware_vertices=getenv("SSBU_VK_HARDWARE_VERTEX")&&strcmp(getenv("SSBU_VK_HARDWARE_VERTEX"),"0");
static bool hardware_vertex_toggle=getenv("SSBU_VK_HARDWARE_VERTEX_TOGGLE")&&strcmp(getenv("SSBU_VK_HARDWARE_VERTEX_TOGGLE"),"0");
uint64_t hardware_vertex_draws=0,hardware_vertex_attributes=0;
static uint32_t early_submit_draws=[](){auto text=getenv("SSBU_VK_SPLIT_DRAWS");return text?std::clamp((uint32_t)strtoul(text,nullptr,10),0u,4096u):0u;}();
static bool early_submit_toggle=getenv("SSBU_VK_SPLIT_TOGGLE")&&strcmp(getenv("SSBU_VK_SPLIT_TOGGLE"),"0");
uint64_t early_submissions=0;
static bool skip_overlay_init=getenv("SSBU_VK_OVERLAY_INIT_SKIP")&&strcmp(getenv("SSBU_VK_OVERLAY_INIT_SKIP"),"0");
uint64_t overlay_init_skips=0,overlay_init_bytes=0;
uint64_t mixed_attachment_draws=0;
bool gpu_overlays=true,vertex_bounds_enabled=true;
uint64_t deferred_dispatches=0,resident_compute_reuses=0,resident_draw_reads=0,resident_fallbacks=0;
uint64_t resident_overlay_reads=0,resident_overlay_bytes=0;
uint64_t native_index_draws=0,converted_index_draws=0,index_conversion_ns=0;
uint64_t zero_snapshots_avoided=0,private_zero_snapshots=0;
uint64_t vertex_views_trimmed=0,vertex_span_before=0,vertex_span_after=0;
enum class UploadKind {Vertex,Constant,Global,Index,Other};
uint64_t cpu_buffer_bytes[5]={};
struct ConstantBucket {uint64_t requests=0,bytes=0,copied=0;};
ConstantBucket constant_sizes[7];
uint64_t constant_eligible=0,constant_nonlinear=0,constant_unmapped=0,constant_cache_calls=0;
bool zero_unmapped_constants=true;
bool sparse_constants=false; // opt-in until a match FPS gain is established
uint64_t draw_profile_after=1000;
volatile LONG draw_profile_pending=0;
uint64_t empty_constant_reuses=0,empty_constant_bytes=0,partial_constant_reads=0;
static size_t constant_bucket(size_t bytes){return bytes<256?0:bytes<1024?1:bytes<4096?2:bytes==4096?3:bytes<=16384?4:bytes<=65536?5:6;}
struct ConstantAccounting {
    ConstantBucket *bucket=nullptr;uint64_t before=0;
    ConstantAccounting(UploadKind kind,size_t bytes){if(kind==UploadKind::Constant){bucket=&constant_sizes[constant_bucket(bytes)];bucket->requests++;bucket->bytes+=bytes;before=buffer_cache.copied+gfx_stat_upload_bytes;}}
    ~ConstantAccounting(){if(bucket)bucket->copied+=buffer_cache.copied+gfx_stat_upload_bytes-before;}
};
static void constant_diagnostics(){
    auto fence=fence_sleep_stats();fprintf(stderr,"[native fence sleep] %llu calls, %llu waits, %llu short deadlines, %llu notifications, %llu failures, %.1f ms paused\n",fence.calls,fence.waits,fence.short_timeouts,fence.notifications,fence.failures,fence.wait_ns/1e6);
    const char *labels[]={"<256","256-1023","1024-4095","4096","4097-16384","16385-65536",">65536"};
    for(size_t i=0;i<7;i++){auto &b=constant_sizes[i];fprintf(stderr,"[native constant sizes] %s: %llu requests, %llu MiB requested, %llu MiB copied\n",labels[i],b.requests,b.bytes>>20,b.copied>>20);}
    fprintf(stderr,"[native constant eligibility] %llu cache calls, %llu exact-eligible, %llu nonlinear, %llu unmapped\n",constant_cache_calls,constant_eligible,constant_nonlinear,constant_unmapped);
    fprintf(stderr,"[native empty constants] %llu shared-zero reuses, %llu MiB snapshots avoided, %llu partial mappings retained\n",empty_constant_reuses,empty_constant_bytes>>20,partial_constant_reads);
    fprintf(stderr,"[native sparse constants] %llu checks, %llu unchanged GPU snapshots reused, %llu changed snapshots, %llu MiB mapped fragments read\n",buffer_cache.sparse_checks,buffer_cache.sparse_reuses,buffer_cache.sparse_changed,buffer_cache.sparse_read_bytes>>20);
    fprintf(stderr,"[native buffer cache reasons] small %llu, unwatched/large %llu, shadow-budget %llu, slab-full %llu; created %llu, rollovers %llu, invalidated %llu, dirty %llu, unequal %llu\n",buffer_cache.rejected_small,buffer_cache.rejected_unwatched,buffer_cache.rejected_budget,buffer_cache.rejected_full,buffer_cache.created,buffer_cache.rollovers,buffer_cache.invalidated,buffer_cache.dirty_checks,buffer_cache.shadow_changes);
}
struct UploadAccounting {size_t kind;uint64_t before;UploadAccounting(UploadKind k):kind((size_t)k),before(buffer_cache.copied+gfx_stat_upload_bytes){}~UploadAccounting(){cpu_buffer_bytes[kind]+=buffer_cache.copied+gfx_stat_upload_bytes-before;}};
uint64_t texture_epoch=0;
uint64_t cache_checkpoint_ns=0;
uint64_t texture_hits=0,texture_hash_bytes=0,texture_cpu_ns=0,shader_cpu_ns=0,shader_decodes=0,buffer_cpu_ns=0;
uint64_t translation_ns=0,compute_total_ns=0,compute_input_ns=0,compute_output_ns=0,compute_wait_ns=0;
uint64_t shader_code_hits=0,shader_hash_bytes=0,shader_variant_probes=0;
struct CpuTimer {uint64_t &total,start;CpuTimer(uint64_t &counter):total(counter),start(rt_clock_ns()){}~CpuTimer(){total+=rt_clock_ns()-start;}};
static void compare_attachment_phase(){
    static uint64_t seconds=getenv("SSBU_VK_COMPARE_ATTACHMENTS")?strtoull(getenv("SSBU_VK_COMPARE_ATTACHMENTS"),nullptr,10):0;
    if(!seconds||seconds>120||gfx_stat_dispatched<1000)return;
    static uint64_t start=0,frames=0,gpu=0,wait=0,compiler=0,phase=0;uint64_t now=rt_clock_ns();
    uint64_t compile=work_stats.dxc_ns+work_stats.graphics_ns+work_stats.compute_ns;
    if(start&&now-start<seconds*1000000000ull)return;
    if(start){double elapsed=(now-start)/1e9;fprintf(stderr,"[native attachment comparison] phase %llu %s: %.3f s, %llu frames, %.2f FPS; GPU submissions %.1f ms, fence %.1f ms, compiler/pipelines %.1f ms\n",phase,device.attachment_barriers?"ON":"OFF",elapsed,gfx_stat_presented-frames,(gfx_stat_presented-frames)/elapsed,(device.gpu_batch_ns-gpu)/1e6,(device.fence_wait_ns-wait)/1e6,(compile-compiler)/1e6);phase++;}
    device.attachment_barriers=(phase&1)!=0;start=now;frames=gfx_stat_presented;gpu=device.gpu_batch_ns;wait=device.fence_wait_ns;compiler=compile;
    fprintf(stderr,"[native attachment comparison] begin phase %llu %s at frame %llu, dispatch %llu\n",phase,device.attachment_barriers?"ON":"OFF",frames,gfx_stat_dispatched);
}
static void compare_sparse_phase(){
    static uint64_t seconds=getenv("SSBU_VK_COMPARE_SPARSE")?strtoull(getenv("SSBU_VK_COMPARE_SPARSE"),nullptr,10):0;
    if(!seconds||seconds>120||gfx_stat_dispatched<1000)return;
    static uint64_t start=0,frames=0,upload=0,wait=0,compiler=0,phase=0;uint64_t now=rt_clock_ns();
    uint64_t compile=work_stats.dxc_ns+work_stats.graphics_ns+work_stats.compute_ns;
    if(start&&now-start<seconds*1000000000ull)return;
    if(start){double elapsed=(now-start)/1e9;fprintf(stderr,"[native sparse comparison] phase %llu %s: %.3f s, %llu frames, %.2f FPS; uploads %.1f ms, fence %.1f ms, compiler/pipelines %.1f ms\n",phase,sparse_constants?"ON":"OFF",elapsed,gfx_stat_presented-frames,(gfx_stat_presented-frames)/elapsed,(buffer_cpu_ns-upload)/1e6,(device.fence_wait_ns-wait)/1e6,(compile-compiler)/1e6);phase++;}
    // A diagnostic opt-in only. Exact fragment comparisons remain necessary
    // on re-enabling after arbitrary CPU writes during the baseline phase.
    sparse_constants=(phase&1)!=0;start=now;frames=gfx_stat_presented;upload=buffer_cpu_ns;wait=device.fence_wait_ns;compiler=compile;
    fprintf(stderr,"[native sparse comparison] begin phase %llu %s at frame %llu, dispatch %llu\n",phase,sparse_constants?"ON":"OFF",frames,gfx_stat_dispatched);
}
SnapshotCache<Slice> draw_snapshots(getenv("SSBU_VK_FLAT_SNAPSHOTS")&&strcmp(getenv("SSBU_VK_FLAT_SNAPSHOTS"),"0"));
static bool snapshot_toggle=getenv("SSBU_VK_SNAPSHOT_TOGGLE")&&strcmp(getenv("SSBU_VK_SNAPSHOT_TOGGLE"),"0");
static void snapshot_diagnostics(){fprintf(stderr,"[native snapshot table] %s, %llu requests, %llu hits, %llu entries inserted, %llu overflow inserts, %llu maximum entries per command\n",draw_snapshots.flat()?"FLAT":"MAP",draw_snapshots.requests,draw_snapshots.hits,draw_snapshots.inserts,draw_snapshots.overflows,draw_snapshots.max_entries);}

static uint64_t hash_bytes(const void *data,size_t size,uint64_t hash=1469598103934665603ull){auto bytes=(const uint8_t *)data;for(size_t i=0;i<size;i++)hash=(hash^bytes[i])*1099511628211ull;return hash;}
static float floating(uint32_t u){float f;memcpy(&f,&u,4);return f;}
static void read_gpu(uint64_t address,size_t bytes,void *out){
    auto dest=(uint8_t *)out;while(bytes){size_t n=std::min<size_t>(bytes,4096-(address&4095));uint64_t guest=gpu_va_to_guest(address);if(guest)memcpy(dest,P(uint8_t,guest),n);else memset(dest,0,n);dest+=n;address+=n;bytes-=n;}
}
static void write_gpu(uint64_t address,size_t bytes,const void *source){auto src=(const uint8_t *)source;while(bytes){size_t n=std::min<size_t>(bytes,4096-(address&4095));uint64_t guest=gpu_va_to_guest(address);if(!guest)throw std::runtime_error("Compute write to unmapped GPU memory");memcpy(P(uint8_t,guest),src,n);address+=n;src+=n;bytes-=n;}}
static uint64_t linear_guest(uint64_t va,uint64_t bytes){
    if(!bytes||va+bytes<va)return 0;uint64_t guest=gpu_va_to_guest(va);if(!guest)return 0;
    for(uint64_t offset=std::min<uint64_t>(bytes,4096-(va&4095));offset<bytes;offset+=std::min<uint64_t>(bytes-offset,4096))if(gpu_va_to_guest(va+offset)!=guest+offset)return 0;
    return guest;
}
static bool pending_overlap(uint64_t va,uint64_t bytes){
    if(compute_batch.empty())return false;if(compute_batch.overlaps(va,bytes))return true;
    if(!bytes||va+bytes<va)return true;uint64_t guest=gpu_va_to_guest(va);
    if(!guest||gpu_va_to_guest(va+bytes-1)!=guest+bytes-1){
        // Holes in the conservative shader-code window are not dependencies.
        // Check each mapped piece for guest aliases instead of publishing every
        // pending result just because the window crosses an unmapped page.
        for(uint64_t at=va,left=bytes;left;){uint64_t n=std::min<uint64_t>(left,4096-(at&4095)),mapped=gpu_va_to_guest(at);if(mapped&&compute_batch.overlaps(mapped,n,true))return true;at+=n;left-=n;}
        return false;
    }
    return compute_batch.overlaps(guest,bytes,true);
}
static Slice resident_slice(uint64_t va,uint64_t bytes){
    if(compute_batch.empty()||!bytes)return {};uint64_t guest=gpu_va_to_guest(va);
    if(!guest||va+bytes<va||gpu_va_to_guest(va+bytes-1)!=guest+bytes-1)return {};
    auto slice=compute_batch.find(va,guest,bytes);if(slice.buffer&&slice.offset%device.properties.limits.minStorageBufferOffsetAlignment==0)return slice;return {};
}
static bool overlay_possible(uint64_t va,uint64_t bytes){
    if(!gpu_overlays||compute_batch.empty()||!bytes||bytes>(64ull<<20)||va+bytes<va)return false;uint64_t guest=gpu_va_to_guest(va);
    return guest&&gpu_va_to_guest(va+bytes-1)==guest+bytes-1&&compute_batch.can_overlay(guest,bytes);
}
struct PublicationSite {uint64_t count=0,wait_ns=0,copy_ns=0;const char *function=nullptr;};
std::map<std::pair<std::string,int>,PublicationSite> publication_sites;
static void publish_compute(const char *function,int line){if(compute_batch.empty())return;uint64_t wait_before=compute_wait_ns,copy_before=compute_output_ns;compute_batch.flush(write_gpu,compute_wait_ns,compute_output_ns);compute_readbacks.release_held();auto &site=publication_sites[{function,line}];site.function=function;site.count++;site.wait_ns+=compute_wait_ns-wait_before;site.copy_ns+=compute_output_ns-copy_before;gfx_stat_gpu_waits++;texture_epoch++;}
// Attribute actual publications, rather than calls that find no pending work.
#define flush_compute() publish_compute(__FUNCTION__,__LINE__)
static Slice upload_gpu(uint64_t address,size_t bytes,UploadKind kind=UploadKind::Other){CpuTimer timer(buffer_cpu_ns);UploadAccounting accounting(kind);bytes=std::max<size_t>(bytes,16);ConstantAccounting constants(kind,bytes);auto key=std::make_pair(address,bytes);auto existing=draw_snapshots.find(key);if(existing)return *existing;auto resident=resident_slice(address,bytes);if(resident.buffer){resident_draw_reads++;draw_snapshots.store(key,resident);return resident;}
    bool overlay=overlay_possible(address,bytes);if(pending_overlap(address,bytes)&&!overlay)throw std::runtime_error("Graphics CPU dependency missed its preflight");
    uint64_t guest=gpu_va_to_guest(address);
    Slice cached={};if(guest&&gpu_va_to_guest(address+bytes-1)==guest+bytes-1){
        const void *compare_source=nullptr;
        if(kind==UploadKind::Constant){constant_cache_calls++;if(bytes>=4096&&bytes<=65536){if(linear_guest(address,bytes)==guest){compare_source=P(uint8_t,guest);constant_eligible++;}else constant_nonlinear++;}}
        cached=buffer_cache.get(address,guest,bytes,read_gpu,compare_source);
    }else if(kind==UploadKind::Constant){
        constant_unmapped++;
        if(zero_unmapped_constants&&bytes<=65536){
            if(!guest&&all_unmapped(address,bytes,gpu_va_to_guest)){auto zero=device.read_only_zero(bytes);empty_constant_reuses++;empty_constant_bytes+=bytes;draw_snapshots.store(key,zero);return zero;}
            partial_constant_reads++;
        }
        if(sparse_constants)cached=buffer_cache.get_sparse(address,bytes,read_gpu,gpu_va_to_guest);
    }
    if(!overlay&&cached.buffer){draw_snapshots.store(key,cached);return cached;}
    auto slice=device.upload(bytes,device.properties.limits.minStorageBufferOffsetAlignment);
    if(overlay&&cached.buffer){
        // Keep cache lookups, allocations and dependencies identical to the
        // baseline. Isolate omission of the old-data GPU copy from the earlier
        // cache-bypass experiment, which suffered device loss in the game.
        device.barrier();
        if(skip_overlay_init&&compute_batch.covers(guest,bytes)){overlay_init_skips++;overlay_init_bytes+=bytes;}
        else{VkBufferCopy copy={cached.offset,slice.offset,bytes};vkCmdCopyBuffer(device.command,cached.buffer,slice.buffer,1,&copy);}
    }
    else{read_gpu(address,bytes,slice.data);gfx_stat_upload_bytes+=bytes;}
    if(overlay){compute_batch.overlay(slice,guest,bytes);resident_overlay_reads++;resident_overlay_bytes+=bytes;}
    draw_snapshots.store(key,slice);return slice;}

static LRESULT CALLBACK procedure(HWND h,UINT msg,WPARAM w,LPARAM l){
#ifdef SSBU_OPENSMASH_PORT
    if(opensmash_port_selected() && (msg==WM_APP+77 || (msg==WM_KEYDOWN && w==VK_ESCAPE && !(l&(1<<30))))){nvn_online_menu_toggle(h);return 0;}
#endif
    LRESULT result;if(display_message(h,msg,w,l,&result))return result;
    if(msg==WM_CLOSE){fprintf(stderr,"[native Vulkan] window closed\n");gfx_closing();fflush(stderr);ExitProcess(0);}
    if(msg==WM_KEYDOWN&&w>=VK_F1&&w<=VK_F6&&!(l&(1<<30)))InterlockedOr(&keys,1<<(w-VK_F1));
    return DefWindowProcW(h,msg,w,l);
}
static const wchar_t *game_window_title(){
#ifdef SSBU_OPENSMASH_PORT
    if(opensmash_port_selected())return L"WindowSmash NVN Vulkan";
#endif
    return L"Super Smash Bros. Ultimate (native Vulkan experimental)";
}
static DWORD WINAPI window_thread(void *event){WNDCLASSW cls={};cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandleW(nullptr);cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.lpszClassName=L"ssbu-native-vulkan";RegisterClassW(&cls);window=display_create_window(cls.hInstance,cls.lpszClassName,game_window_title());SetEvent((HANDLE)event);MSG message;while(GetMessageW(&message,nullptr,0,0)>0){
#ifdef SSBU_OPENSMASH_PORT
if(nvn_online_menu_message(&message))continue;
#endif
TranslateMessage(&message);DispatchMessageW(&message);}return 0;}
static BOOL CALLBACK initialize(PINIT_ONCE,PVOID,PVOID *){
    InitializeCriticalSection(&lock);HANDLE event=CreateEventW(nullptr,TRUE,FALSE,nullptr);HANDLE thread=CreateThread(nullptr,0,window_thread,event,0,nullptr);if(!thread||WaitForSingleObject(event,10000)!=WAIT_OBJECT_0||!window){fprintf(stderr,"[native Vulkan] failed to create game window\n");ExitProcess(1);}CloseHandle(thread);CloseHandle(event);
#ifdef SSBU_OPENSMASH_PORT
    if(opensmash_port_selected()){opensmash_port_start(window);ready=2;return TRUE;}
#endif
    try{device.init(display_gpu(),"build/native-vulkan/game.pipeline-cache");device.recipe_path=hardware_vertices?"build/native-vulkan/pipeline-recipes-hardware":"build/native-vulkan/pipeline-recipes";presentation.init(device,window,display_pacing()==PACING_DISPLAY);scale=display_render_scale();if(!(scale>0))scale=1;batch_compute=getenv("SSBU_VK_BATCH_COMPUTE")&&strcmp(getenv("SSBU_VK_BATCH_COMPUTE"),"0");gpu_overlays=!getenv("SSBU_VK_BUFFER_OVERLAYS")||strcmp(getenv("SSBU_VK_BUFFER_OVERLAYS"),"0");vertex_bounds_enabled=!getenv("SSBU_VK_VERTEX_BOUNDS")||strcmp(getenv("SSBU_VK_VERTEX_BOUNDS"),"0");zero_unmapped_constants=!getenv("SSBU_VK_ZERO_UNMAPPED")||strcmp(getenv("SSBU_VK_ZERO_UNMAPPED"),"0");sparse_constants=getenv("SSBU_VK_SPARSE_CONSTANTS")&&strcmp(getenv("SSBU_VK_SPARSE_CONSTANTS"),"0");configure_fence_sleep(getenv("SSBU_VK_FENCE_SLEEP")&&strcmp(getenv("SSBU_VK_FENCE_SLEEP"),"0"));draw_profile_after=getenv("SSBU_VK_DRAW_TIMINGS_AFTER")?strtoull(getenv("SSBU_VK_DRAW_TIMINGS_AFTER"),nullptr,10):1000;ready=1;if(batch_compute)fprintf(stderr,"[native Vulkan] GPU-resident compute chaining enabled (CPU visibility at explicit boundaries)\n");fprintf(stderr,"[native Vulkan] experimental game backend active, scale %.2f; synchronous compute coherence\n",scale);}catch(const std::exception &error){fprintf(stderr,"[native Vulkan] initialization failed: %s\n",error.what());fflush(stderr);ExitProcess(1);}return TRUE;
}
static void guarded(const char *operation,const std::function<void()> &work){InitOnceExecuteOnce(&once,initialize,nullptr,nullptr);
#ifdef SSBU_OPENSMASH_PORT
if(ready==2)rt_fatal("Unexpected legacy GPU operation under NVN frontend: %s",operation);
#endif
EnterCriticalSection(&lock);last_thread=GetCurrentThreadId();uint64_t start=rt_clock_ns();try{work();}catch(const std::exception &error){fprintf(stderr,"[native Vulkan] %s failed: %s\n",operation,error.what());fflush(stderr);ExitProcess(1);}gfx_stat_busy_ns+=rt_clock_ns()-start;LeaveCriticalSection(&lock);}

uint64_t surface_clock;
struct Surface {uint64_t guest,va=0,used;uint32_t width,height,format;bool depth;Image image;std::map<std::pair<int,int>,VkImageView> views;};
std::vector<std::unique_ptr<Surface>> surfaces;
static VkImageAspectFlags aspects(VkFormat format){return format==VK_FORMAT_D24_UNORM_S8_UINT||format==VK_FORMAT_D32_SFLOAT_S8_UINT?VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT:format==VK_FORMAT_D32_SFLOAT||format==VK_FORMAT_D16_UNORM?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT;}
static VkFormat linear_format(VkFormat f){switch(f){case VK_FORMAT_R8G8B8A8_SRGB:return VK_FORMAT_R8G8B8A8_UNORM;case VK_FORMAT_B8G8R8A8_SRGB:return VK_FORMAT_B8G8R8A8_UNORM;default:return f;}}
static Surface *surface(uint64_t guest,uint32_t width,uint32_t height,uint32_t format,bool depth){
    if(!guest||!width||!height||width>16384||height>16384)throw std::runtime_error("Invalid render target");
    VkFormat target=depth?depth_format(format):color_format(format);
    for(auto &s:surfaces)if(s->guest==guest&&s->depth==depth&&s->width==width&&s->height==height&&linear_format(s->image.format)==linear_format(target)){s->format=format;s->used=++surface_clock;return s.get();}
    auto s=std::make_unique<Surface>();s->guest=guest;s->used=++surface_clock;s->width=width;s->height=height;s->format=format;s->depth=depth;uint32_t w=std::max(1u,(uint32_t)(width*scale+.5f)),h=std::max(1u,(uint32_t)(height*scale+.5f));
    s->image=device.image(w,h,1,1,1,depth?target:linear_format(target),VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|(depth?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT),VK_IMAGE_VIEW_TYPE_2D,aspects(target));
    // Initialize before a partial clear, masked draw, or texture read.
    device.transition(s->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);VkImageSubresourceRange range={s->image.aspect,0,1,0,1};if(depth){VkClearDepthStencilValue clear={1,0};vkCmdClearDepthStencilImage(device.command,s->image.handle,s->image.layout,&clear,1,&range);}else{VkClearColorValue clear={};vkCmdClearColorImage(device.command,s->image.handle,s->image.layout,&clear,1,&range);}
    auto result=s.get();surfaces.push_back(std::move(s));return result;
}
static Surface *surface_gpu(uint64_t va,uint32_t width,uint32_t height,uint32_t format,bool depth){auto result=surface(gpu_va_to_guest(va),width,height,format,depth);result->va=va;return result;}
static VkImageView view(Surface &surface,VkFormat format,VkImageViewType type){
    auto &image=surface.image;if(image.aspect&VK_IMAGE_ASPECT_DEPTH_BIT)format=image.format;
    if(format==image.format&&type==image.type&&image.aspect==VK_IMAGE_ASPECT_COLOR_BIT)return image.view;
    auto key=std::make_pair((int)format,(int)type);auto found=surface.views.find(key);if(found!=surface.views.end())return found->second;
    VkImageViewCreateInfo info={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};info.image=image.handle;info.viewType=type;info.format=format;info.subresourceRange={(VkImageAspectFlags)(image.aspect&VK_IMAGE_ASPECT_DEPTH_BIT?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT),0,1,0,1};VkImageView result;check(vkCreateImageView(device.handle,&info,nullptr,&result),"render target sampling view");surface.views[key]=result;return result;
}

struct Shader {uint64_t key,table_hash;int stage;ShaderInfo info;Spirv spirv;std::string name;};
static uint64_t timing_key(const Shader &shader){return hash_bytes(&shader.table_hash,sizeof shader.table_hash,shader.key);}
std::vector<std::unique_ptr<Shader>> shaders;
static void draw_timing_diagnostics(){
    if(!ready||!device.gpu_draw_period)return;
    // Status logging can run on another thread. Copy/resolve the maps only
    // under the renderer lock; if busy, let the next present emit a snapshot.
    if(!TryEnterCriticalSection(&lock)){InterlockedExchange(&draw_profile_pending,1);return;}
    struct ReleaseProfileLock {~ReleaseProfileLock(){LeaveCriticalSection(&lock);}} release;
    InterlockedExchange(&draw_profile_pending,0);
    std::vector<std::pair<DrawTimingKey,DrawTiming>> rows(device.gpu_draw_times.begin(),device.gpu_draw_times.end());
    std::sort(rows.begin(),rows.end(),[](const auto &a,const auto &b){return a.second.ns>b.second.ns;});
    auto name=[](uint64_t key,int stage){for(auto &s:shaders)if(timing_key(*s)==key&&s->stage==stage)return s->name.c_str();return stage==1&&key==0?"depth-only":"unknown";};
    fprintf(stderr,"[native GPU draw profile] 1/%u draws sampled after %llu dispatches; %zu pairs, %llu pool-capacity skips; sampled durations include profiling dependencies\n",device.gpu_draw_period,draw_profile_after,rows.size(),device.gpu_draw_skipped);
    for(size_t i=0;i<std::min<size_t>(rows.size(),16);i++){auto &key=rows[i].first;auto &cost=rows[i].second;fprintf(stderr,"[native GPU draw] %s %s keys %llx/%llx: %llu samples, %.3f ms total, %.3f us mean, %llu vertices/sample, %llu target-pixels/sample\n",name(key.first,0),name(key.second,1),key.first,key.second,cost.samples,cost.ns/1e6,cost.ns/(1000.0*cost.samples),cost.vertices/cost.samples,cost.pixels/cost.samples);}
}
std::unordered_multimap<uint64_t,Shader *> shader_variants;
struct ProgramCode {uint32_t size=0;uint64_t hash=0,seen=0;};
std::map<std::pair<uint64_t,int>,ProgramCode> program_codes;
static uint32_t cb_word(void *user,uint32_t bank,uint32_t offset){auto cb=(const CbBinding *)user;uint32_t value=0;if(bank<18&&cb[bank].address&&offset+4<=cb[bank].size){if(pending_overlap(cb[bank].address+offset,4))flush_compute();read_gpu(cb[bank].address+offset,4,&value);}return value;}
static uint64_t tables_hash(const ShaderInfo &info,const CbBinding *cb){uint64_t hash=0;for(uint32_t i=0;i<info.ntables;i++)for(uint32_t word=0;word<info.table[i].count;word++){auto value=cb_word((void *)cb,info.table[i].bank,info.table[i].offset+4*word);hash=hash_bytes(&value,4,hash);}return hash;}
static Shader *shader(uint64_t guest,int stage,uint32_t outputs,const CbBinding *cb,const uint32_t *local=nullptr,uint32_t shared=0,bool alpha=false){
    CpuTimer timer(shader_cpu_ns);
    if(!guest)throw std::runtime_error("Unmapped shader program");auto program=stage==SHADER_COMPUTE?P(uint8_t,guest)-0x50:P(uint8_t,guest);uint32_t skip=stage==SHADER_COMPUTE?0x50:0;auto &code=program_codes[{guest,stage}];uint64_t raw=code.hash;
    if(code.size){uint64_t bytes=code.size-skip;if(memory_watch.contains(guest,bytes)&&!memory_watch.changed(guest,bytes,code.seen))shader_code_hits++;else{raw=hash_bytes(program+skip,bytes);shader_hash_bytes+=bytes;}}
    if(!code.size||raw!=code.hash){code.size=shader_size(program,0x40000);code.hash=raw=hash_bytes(program+skip,code.size-skip);shader_decodes++;}uint32_t size=code.size;uint64_t key=hash_bytes(&stage,sizeof stage,raw);key=hash_bytes(&outputs,4,key);key=hash_bytes(&alpha,sizeof alpha,key);if(local){key=hash_bytes(local,12,key);key=hash_bytes(&shared,4,key);}
    auto candidates=shader_variants.equal_range(key);for(auto i=candidates.first;i!=candidates.second;++i){shader_variant_probes++;auto s=i->second;if(s->table_hash==tables_hash(s->info,cb))return s;}
    auto s=std::make_unique<Shader>();s->key=key;s->stage=stage;char name[80];snprintf(name,sizeof name,"%s_%016llx%s",stage==0?"vs":stage==1?"ps":"cs",raw,alpha?"_alpha":"");s->name=name;
    if(getenv("SSBU_LOG_STATS")){fprintf(stderr,"[native work] translate begin %s\n",name);fflush(stderr);}
    uint64_t translation_start=rt_clock_ns();
    char *hlsl=stage==SHADER_COMPUTE?shader_hlsl_cs(program,size,local,shared,&s->info,cb_word,(void *)cb):alpha?shader_hlsl_alpha_tested(program,size,outputs,&s->info,cb_word,(void *)cb):shader_hlsl_ex(program,size,stage,outputs,&s->info,cb_word,(void *)cb);
    uint64_t duration=rt_clock_ns()-translation_start;translation_ns+=duration;
    if(getenv("SSBU_LOG_STATS"))fprintf(stderr,"[native work] translate end %s %.1f ms\n",name,duration/1e6);
    if(!hlsl)throw std::runtime_error("Maxwell shader translation failed: "+s->name);std::unique_ptr<char,decltype(&free)> owned(hlsl,free);if(s->info.unsupported[0])throw std::runtime_error(s->name+": "+s->info.unsupported);s->table_hash=tables_hash(s->info,cb);s->spirv=compile(hlsl,stage,s->name.c_str());gfx_stat_shaders++;fprintf(stderr,"[native Vulkan] shader ready %s\n",name);auto result=s.get();shaders.push_back(std::move(s));shader_variants.emplace(key,result);return result;
}
std::map<std::pair<Shader *,uint32_t>,Shader *> hardware_variants;
static Shader *hardware_shader(Shader *original,uint32_t mask){
    if(!mask)return original;auto key=std::make_pair(original,mask);auto found=hardware_variants.find(key);if(found!=hardware_variants.end())return found->second;
    std::string source=original->spirv.source;
    if(!hardware_vertex_fetch(source,mask))throw std::runtime_error("Native vertex interface rewrite did not match translator output");
    auto s=std::make_unique<Shader>();s->key=hash_bytes(&mask,4,original->key);s->table_hash=original->table_hash;s->stage=original->stage;s->info=original->info;s->name=original->name+"_hardware_"+std::to_string(mask);s->spirv=compile(source.c_str(),0,s->name.c_str());
    auto result=s.get();shaders.push_back(std::move(s));hardware_variants.emplace(key,result);return result;
}
static bool vertex_format_supported(VkFormat format){
    static std::map<VkFormat,bool> cache;auto found=cache.find(format);if(found!=cache.end())return found->second;
    VkFormatProperties properties;vkGetPhysicalDeviceFormatProperties(device.physical,format,&properties);return cache[format]=(properties.bufferFeatures&VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT)!=0;
}
struct CachedProgram {Program program;std::map<GraphicsKey,VkPipeline> pipelines;};
std::map<std::pair<Shader *,Shader *>,std::unique_ptr<CachedProgram>> programs;
static CachedProgram &program_for(Shader *vs,Shader *ps){auto key=std::make_pair(vs,ps);auto &p=programs[key];if(!p){p=std::make_unique<CachedProgram>();if(vs->stage==SHADER_COMPUTE)p->program.init_compute(device,vs->spirv);else p->program.init_graphics(device,vs->spirv,ps?&ps->spirv:nullptr);}return *p;}
static VkPipeline graphics_pipeline(CachedProgram &program,const GraphicsState &s){auto key=graphics_key(s);auto &pipeline=program.pipelines[key];if(!pipeline)pipeline=program.program.graphics(s);return pipeline;}
static VkFormat texture_format(DXGI_FORMAT f){switch(f){
#define FORMAT(dx,vk) case DXGI_FORMAT_##dx:return VK_FORMAT_##vk
FORMAT(R32G32B32A32_FLOAT,R32G32B32A32_SFLOAT);FORMAT(R32G32B32A32_SINT,R32G32B32A32_SINT);FORMAT(R32G32B32A32_UINT,R32G32B32A32_UINT);
FORMAT(R16G16B16A16_FLOAT,R16G16B16A16_SFLOAT);FORMAT(R16G16B16A16_UNORM,R16G16B16A16_UNORM);FORMAT(R16G16B16A16_SNORM,R16G16B16A16_SNORM);FORMAT(R16G16B16A16_SINT,R16G16B16A16_SINT);FORMAT(R16G16B16A16_UINT,R16G16B16A16_UINT);
FORMAT(R32G32_FLOAT,R32G32_SFLOAT);FORMAT(R32G32_SINT,R32G32_SINT);FORMAT(R32G32_UINT,R32G32_UINT);
FORMAT(R8G8B8A8_UNORM,R8G8B8A8_UNORM);FORMAT(R8G8B8A8_UNORM_SRGB,R8G8B8A8_SRGB);FORMAT(R8G8B8A8_SNORM,R8G8B8A8_SNORM);FORMAT(R8G8B8A8_SINT,R8G8B8A8_SINT);FORMAT(R8G8B8A8_UINT,R8G8B8A8_UINT);
FORMAT(R10G10B10A2_UNORM,A2B10G10R10_UNORM_PACK32);FORMAT(R10G10B10A2_UINT,A2B10G10R10_UINT_PACK32);
FORMAT(R16G16_FLOAT,R16G16_SFLOAT);FORMAT(R16G16_UNORM,R16G16_UNORM);FORMAT(R16G16_SNORM,R16G16_SNORM);FORMAT(R16G16_SINT,R16G16_SINT);FORMAT(R16G16_UINT,R16G16_UINT);
FORMAT(R32_FLOAT,R32_SFLOAT);FORMAT(R32_SINT,R32_SINT);FORMAT(R32_UINT,R32_UINT);FORMAT(R24G8_TYPELESS,D24_UNORM_S8_UINT);
FORMAT(R8G8_UNORM,R8G8_UNORM);FORMAT(R8G8_SNORM,R8G8_SNORM);FORMAT(R8G8_SINT,R8G8_SINT);FORMAT(R8G8_UINT,R8G8_UINT);
FORMAT(R16_FLOAT,R16_SFLOAT);FORMAT(R16_UNORM,R16_UNORM);FORMAT(R16_SNORM,R16_SNORM);FORMAT(R16_SINT,R16_SINT);FORMAT(R16_UINT,R16_UINT);
FORMAT(R8_UNORM,R8_UNORM);FORMAT(R8_SNORM,R8_SNORM);FORMAT(R8_SINT,R8_SINT);FORMAT(R8_UINT,R8_UINT);
FORMAT(B8G8R8A8_UNORM,B8G8R8A8_UNORM);FORMAT(B8G8R8A8_UNORM_SRGB,B8G8R8A8_SRGB);FORMAT(B4G4R4A4_UNORM,B4G4R4A4_UNORM_PACK16);FORMAT(B5G5R5A1_UNORM,A1R5G5B5_UNORM_PACK16);FORMAT(B5G6R5_UNORM,R5G6B5_UNORM_PACK16);
FORMAT(R9G9B9E5_SHAREDEXP,E5B9G9R9_UFLOAT_PACK32);FORMAT(R11G11B10_FLOAT,B10G11R11_UFLOAT_PACK32);
FORMAT(BC1_UNORM,BC1_RGBA_UNORM_BLOCK);FORMAT(BC1_UNORM_SRGB,BC1_RGBA_SRGB_BLOCK);FORMAT(BC2_UNORM,BC2_UNORM_BLOCK);FORMAT(BC2_UNORM_SRGB,BC2_SRGB_BLOCK);FORMAT(BC3_UNORM,BC3_UNORM_BLOCK);FORMAT(BC3_UNORM_SRGB,BC3_SRGB_BLOCK);FORMAT(BC4_UNORM,BC4_UNORM_BLOCK);FORMAT(BC4_SNORM,BC4_SNORM_BLOCK);FORMAT(BC5_UNORM,BC5_UNORM_BLOCK);FORMAT(BC5_SNORM,BC5_SNORM_BLOCK);FORMAT(BC6H_UF16,BC6H_UFLOAT_BLOCK);FORMAT(BC6H_SF16,BC6H_SFLOAT_BLOCK);FORMAT(BC7_UNORM,BC7_UNORM_BLOCK);FORMAT(BC7_UNORM_SRGB,BC7_SRGB_BLOCK);
#undef FORMAT
default:throw std::runtime_error("Unsupported texture storage format "+std::to_string((int)f));}}
static VkImageViewType texture_type(uint32_t type){switch(type){case TEX_1D:return VK_IMAGE_VIEW_TYPE_1D;case TEX_ARRAY_1D:return VK_IMAGE_VIEW_TYPE_1D_ARRAY;case TEX_2D:return VK_IMAGE_VIEW_TYPE_2D;case TEX_ARRAY_2D:return VK_IMAGE_VIEW_TYPE_2D_ARRAY;case TEX_3D:return VK_IMAGE_VIEW_TYPE_3D;case TEX_CUBE:return VK_IMAGE_VIEW_TYPE_CUBE;case TEX_ARRAY_CUBE:return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;default:throw std::runtime_error("Unsupported shader texture type");}}
struct Texture {Image image;uint64_t hash=0,guest=0,va=0,size=0,seen=0,checked=~0ull;};
std::map<std::vector<uint32_t>,std::unique_ptr<Texture>> textures;
template<class T> struct Retired {T object;uint64_t after;};
std::vector<Retired<Image>> retired_images;
std::vector<Retired<VkImageView>> retired_views;
static Image *retire_image(Image image){retired_images.push_back({image,device.generation});return &retired_images.back().object;}
static void retire_view(VkImageView view){retired_views.push_back({view,device.generation});}
static void retire_completed(){
    buffer_cache.collect();
    for(auto it=retired_views.begin();it!=retired_views.end();)if(it->after<=device.completed_generation){vkDestroyImageView(device.handle,it->object,nullptr);it=retired_views.erase(it);}else ++it;
    for(auto it=retired_images.begin();it!=retired_images.end();)if(it->after<=device.completed_generation){device.destroy(it->object);it=retired_images.erase(it);}else ++it;
}
struct Sampled {Image *image;VkImageView view;};
std::map<std::pair<uint32_t,bool>,Image> null_textures;
static Sampled null_texture(uint32_t type,bool shadow){auto &image=null_textures[{type,shadow}];if(!image.handle){auto kind=texture_type(type);uint32_t layers=type==TEX_CUBE||type==TEX_ARRAY_CUBE?6:1;image=device.image(1,1,1,layers,1,shadow?VK_FORMAT_D32_SFLOAT:VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,kind,shadow?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT);device.transition(image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);VkImageSubresourceRange range={image.aspect,0,1,0,layers};if(shadow){VkClearDepthStencilValue clear={0,0};vkCmdClearDepthStencilImage(device.command,image.handle,image.layout,&clear,1,&range);}else{VkClearColorValue clear={};vkCmdClearColorImage(device.command,image.handle,image.layout,&clear,1,&range);}device.transition(image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);}return {&image,image.view};}
static Sampled texture(const uint32_t *tic,uint32_t type,bool shadow,const std::vector<Surface *> &outputs){
    CpuTimer timer(texture_cpu_ns);
    TextureDesc desc={};if(!tic_decode(tic,&desc)){if(!(tic[0]&0x7f))return null_texture(type,shadow);char error[256];snprintf(error,sizeof error,"Unsupported texture header: %08x %08x %08x %08x %08x %08x %08x %08x",tic[0],tic[1],tic[2],tic[3],tic[4],tic[5],tic[6],tic[7]);throw std::runtime_error(error);}uint64_t guest=gpu_va_to_guest(desc.address);if(!guest)return null_texture(type,shadow);auto format=texture_format(desc.dxgi);auto kind=texture_type(type);
    bool depth_tic=desc.format==0x29||desc.format==0x2a||desc.format==0x2b||desc.format==0x2f||desc.format==0x30||desc.format==0x3a;Surface *latest=nullptr;for(auto &candidate:surfaces)if(candidate->guest==guest&&(!candidate->depth||(depth_tic&&candidate->width==desc.width&&candidate->height==desc.height))&&(!latest||candidate->used>latest->used))latest=candidate.get();
    if(latest){auto &s=*latest;if(std::find(outputs.begin(),outputs.end(),&s)!=outputs.end()){
        // Snapshot feedback before beginning dynamic rendering; descriptors
        // must never sample an image concurrently bound for attachment writes.
        auto copy=device.image(s.image.width,s.image.height,1,1,1,s.image.format,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|(s.depth?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT),VK_IMAGE_VIEW_TYPE_2D,s.image.aspect);device.transition(s.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);device.transition(copy,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);VkImageCopy region={};VkImageAspectFlags aspect=s.depth?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT;region.srcSubresource={aspect,0,0,1};region.dstSubresource=region.srcSubresource;region.extent={copy.width,copy.height,1};vkCmdCopyImage(device.command,s.image.handle,s.image.layout,copy.handle,copy.layout,1,&region);device.transition(copy,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);Surface temporary;temporary.image=copy;temporary.depth=s.depth;auto copy_view=view(temporary,s.depth?copy.format:format,kind);for(auto entry:temporary.views)retire_view(entry.second);auto stored=retire_image(copy);return {stored,copy_view};
    }device.transition(s.image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);return {&s.image,view(s,format,kind)};}
    uint32_t layers=desc.type==TIC_CUBE?6:desc.type==TIC_CUBE_ARRAY?6*desc.depth:desc.type==TIC_1D_ARRAY||desc.type==TIC_2D_ARRAY?desc.depth:1;
    uint32_t levels=desc.type==TIC_3D?1:std::max(1u,desc.levels),max_levels=1;while((desc.width>>max_levels)||(desc.height>>max_levels))max_levels++;levels=std::min(levels,max_levels);
    uint64_t stored=desc.type==TIC_3D?texture_3d_size(&desc):texture_layer_size(&desc)*layers;if(stored>(256ull<<20))throw std::runtime_error("Texture exceeds experimental backend resource limit");
    std::vector<uint32_t> key(tic,tic+8);key.push_back(type);auto &entry=textures[key];bool same=entry&&entry->guest==guest&&entry->size==stored;
    if(same&&entry->checked==texture_epoch){texture_hits++;return {&entry->image,entry->image.view};}
    uint64_t content=0,seen=same?entry->seen:0;bool unchanged=false;
    if(memory_watch.contains(guest,stored)){uint64_t start=rt_clock_ns();bool dirty=memory_watch.changed(guest,stored,seen);gfx_stat_watch_us+=(rt_clock_ns()-start)/1000;unchanged=same&&!dirty;}
    else{content=hash_bytes(P(uint8_t,guest),(size_t)stored);texture_hash_bytes+=stored;unchanged=same&&entry->hash==content;}
    if(unchanged){entry->seen=seen;entry->checked=texture_epoch;texture_hits++;return {&entry->image,entry->image.view};}
    if(entry){retire_image(entry->image);entry->image={};}else entry=std::make_unique<Texture>();entry->guest=guest;entry->va=desc.address;entry->size=stored;entry->hash=content;entry->seen=seen;entry->checked=texture_epoch;
    entry->image=device.image(desc.width,desc.height,desc.type==TIC_3D?desc.depth:1,layers,levels,format,VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,kind);
    device.transition(entry->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    std::vector<VkBufferImageCopy> copies;std::vector<uint8_t> data;
    for(uint32_t layer=0;layer<layers;layer++)for(uint32_t level=0;level<levels;level++){uint32_t w=std::max(1u,desc.width>>level),h=std::max(1u,desc.height>>level),depth=desc.type==TIC_3D?desc.depth:1;size_t size=(size_t)((w+desc.bw-1)/desc.bw)*((h+desc.bh-1)/desc.bh)*desc.bytes*depth;size_t offset=(data.size()+15)&~size_t(15);data.resize(offset+size);if(desc.type==TIC_3D)texture_read_3d(&desc,guest,data.data()+offset);else texture_read(&desc,guest,layer,level,data.data()+offset);VkBufferImageCopy copy={};copy.bufferOffset=offset;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level,layer,1};copy.imageExtent={w,h,depth};copies.push_back(copy);}
    auto upload=device.upload(data.size(),16);memcpy(upload.data,data.data(),data.size());for(auto &copy:copies)copy.bufferOffset+=upload.offset;vkCmdCopyBufferToImage(device.command,upload.buffer,entry->image.handle,entry->image.layout,(uint32_t)copies.size(),copies.data());device.transition(entry->image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);gfx_stat_textures++;return {&entry->image,entry->image.view};
}
std::map<std::vector<uint32_t>,VkSampler> samplers;
static VkSampler sampler(const uint32_t *tsc){std::vector<uint32_t> key(tsc,tsc+8);auto &value=samplers[key];if(value)return value;auto address=[](uint32_t wrap){switch(wrap){case 0:return VK_SAMPLER_ADDRESS_MODE_REPEAT;case 1:return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;case 3:return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;default:return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;}};VkSamplerCreateInfo info={VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};info.magFilter=(tsc[1]&3)==2?VK_FILTER_LINEAR:VK_FILTER_NEAREST;info.minFilter=((tsc[1]>>4)&3)==2?VK_FILTER_LINEAR:VK_FILTER_NEAREST;uint32_t mip=(tsc[1]>>6)&3;info.mipmapMode=mip==3?VK_SAMPLER_MIPMAP_MODE_LINEAR:VK_SAMPLER_MIPMAP_MODE_NEAREST;info.addressModeU=address(tsc[0]&7);info.addressModeV=address((tsc[0]>>3)&7);info.addressModeW=address((tsc[0]>>6)&7);info.compareEnable=(tsc[0]>>9)&1;info.compareOp=(VkCompareOp)((tsc[0]>>10)&7);info.mipLodBias=std::clamp(((int32_t)(tsc[1]<<7)>>19)/256.f,-device.properties.limits.maxSamplerLodBias,device.properties.limits.maxSamplerLodBias);info.minLod=(tsc[2]&4095)/256.f;info.maxLod=mip==1?0.f:((tsc[2]>>12)&4095)/256.f;if(info.maxLod<info.minLod)info.minLod=info.maxLod;uint32_t anisotropy=(tsc[0]>>20)&7;info.anisotropyEnable=anisotropy&&device.features.samplerAnisotropy;info.maxAnisotropy=info.anisotropyEnable?std::min((float)(1u<<anisotropy),device.properties.limits.maxSamplerAnisotropy):1.f;float border[4];memcpy(border,tsc+4,16);info.borderColor=border[0]==1&&border[1]==1&&border[2]==1?VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE:border[3]==1?VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK:VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;check(vkCreateSampler(device.handle,&info,nullptr,&value),"Maxwell sampler");return value;}
static bool dense_resources=getenv("SSBU_VK_DENSE_RESOURCES")&&strcmp(getenv("SSBU_VK_DENSE_RESOURCES"),"0");
static bool resource_toggle=getenv("SSBU_VK_RESOURCE_TOGGLE")&&strcmp(getenv("SSBU_VK_RESOURCE_TOGGLE"),"0");
struct Resources:DescriptorResources {Resources():DescriptorResources(dense_resources){}};
static VkDescriptorBufferInfo buffer_info(Slice slice){return {slice.buffer,slice.offset,slice.size};}
static void bind_resources(Resources &res,Shader &shader,const uint32_t *regs,const CbBinding *cb,nativevk::DrawParams &params,const std::vector<Surface *> &outputs){
    int stage=shader.stage==SHADER_PIXEL?1:0;
    for(uint32_t bank=0;bank<18;bank++)if(shader.info.cbuf_mask>>bank&1){size_t size=std::min(0x10000u,std::min(cb[bank].size,shader.info.cbuf_extent[bank]));res.buffers[1+bank]=buffer_info(upload_gpu(cb[bank].address,size,UploadKind::Constant));}
    for(uint32_t i=0;i<shader.info.sb_count&&shader.stage!=SHADER_COMPUTE;i++){uint32_t desc[4]={};if(cb[0].address)read_gpu(cb[0].address+shader.info.sb_offset[i],16,desc);uint64_t va=(uint64_t)desc[1]<<32|desc[0];uint32_t size=desc[2]?std::min<uint64_t>(((va+desc[2]+4095)&~4095ull)-va,16u<<20):0;params.sb[stage][i][0]=desc[0];params.sb[stage][i][1]=desc[1];params.sb[stage][i][2]=size;res.buffers[97+i]=buffer_info(upload_gpu(va,size,UploadKind::Global));}
    uint32_t bank=regs[M_BINDLESS_TEXTURE]&31;uint64_t tic_pool=(uint64_t)regs[M_TEX_HEADER_POOL]<<32|regs[M_TEX_HEADER_POOL+1],tsc_pool=(uint64_t)regs[M_TEX_SAMPLER_POOL]<<32|regs[M_TEX_SAMPLER_POOL+1];
    for(uint32_t i=0;i<shader.info.ntex;i++){uint32_t handle=0,tic[8]={},tsc[8]={};if(bank<18&&cb[bank].address)read_gpu(cb[bank].address+shader.info.tex[i].handle_offset,4,&handle);uint32_t index=handle&0xfffff,smp=regs[M_SAMPLER_BINDING]&1?index:handle>>20;read_gpu(tic_pool+index*32ull,32,tic);read_gpu(tsc_pool+smp*32ull,32,tsc);auto sampled=texture(tic,shader.info.tex[i].type,shader.info.tex[i].shadow,outputs);res.images[65+i]={VK_NULL_HANDLE,sampled.view,sampled.image->layout};res.images[160+i]={sampler(tsc),VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED};params.swz[stage][i]=(tic[0]>>19)&4095;if(sampled.image->width){TextureDesc desc={};if(tic_decode(tic,&desc)&&desc.width){float factor=(float)sampled.image->width/desc.width;if(factor!=1)params.swz[stage][i]|=(uint32_t)(factor*4096+.5f)<<16;}}}
}
static void preflight_textures(Shader &code,const uint32_t *regs,const CbBinding *cb){
    if(!compute_batch.empty()){
        uint32_t bank=regs[M_BINDLESS_TEXTURE]&31;uint64_t tic_pool=(uint64_t)regs[M_TEX_HEADER_POOL]<<32|regs[M_TEX_HEADER_POOL+1],tsc_pool=(uint64_t)regs[M_TEX_SAMPLER_POOL]<<32|regs[M_TEX_SAMPLER_POOL+1];
        for(uint32_t i=0;i<code.info.ntex&&!compute_batch.empty();i++){
            uint32_t handle=0,tic[8]={};if(bank<18&&cb[bank].address){uint64_t word=cb[bank].address+code.info.tex[i].handle_offset;if(pending_overlap(word,4))flush_compute();read_gpu(word,4,&handle);}
            uint32_t index=handle&0xfffff,smp=regs[M_SAMPLER_BINDING]&1?index:handle>>20;
            if(pending_overlap(tic_pool+index*32ull,32)||pending_overlap(tsc_pool+smp*32ull,32)){flush_compute();break;}
            read_gpu(tic_pool+index*32ull,32,tic);TextureDesc desc={};if(tic_decode(tic,&desc)){
                uint32_t layers=desc.type==TIC_CUBE?6:desc.type==TIC_CUBE_ARRAY?6*desc.depth:desc.type==TIC_1D_ARRAY||desc.type==TIC_2D_ARRAY?desc.depth:1;
                uint64_t size=desc.type==TIC_3D?texture_3d_size(&desc):texture_layer_size(&desc)*layers;if(pending_overlap(desc.address,size))flush_compute();
            }
        }
    }
}
static void preflight_uniforms(Shader &code,const CbBinding *cb){
    for(uint32_t bank=0;bank<18;bank++)if(code.info.cbuf_mask>>bank&1){size_t bytes=std::max<size_t>(16,std::min(0x10000u,std::min(cb[bank].size,code.info.cbuf_extent[bank])));if(pending_overlap(cb[bank].address,bytes)&&!resident_slice(cb[bank].address,bytes).buffer&&!overlay_possible(cb[bank].address,bytes)){resident_fallbacks++;flush_compute();}}
}
static VkDescriptorSet descriptors(const std::vector<Binding> &bindings,VkDescriptorSetLayout layout,Resources &resources,Slice params){
    // Every declared slot receives a descriptor, including unused CB/attribute
    // declarations retained by the compiler. Missing raw buffers read zero.
    Slice private_zero={};resources.buffers[0]=buffer_info(params);VkDescriptorSet set=device.descriptor_set(layout);std::vector<VkWriteDescriptorSet> writes;if(dense_resources)writes.reserve(bindings.size());
    for(auto binding:bindings){VkWriteDescriptorSet write={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=set;write.dstBinding=binding.binding;write.descriptorCount=1;write.descriptorType=binding.type;if(binding.type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER||binding.type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER){if(!resources.buffers.count(binding.binding)){
            // Register b/t buffers are read-only; u registers start at 256.
            // Writable fallback slots retain a private snapshot per stage.
            bool readonly=binding.type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER||binding.binding<256;
            if(!readonly&&!private_zero.buffer){private_zero=device.upload(4096,device.properties.limits.minStorageBufferOffsetAlignment);memset(private_zero.data,0,4096);private_zero_snapshots++;}
            resources.buffers[binding.binding]=buffer_info(readonly?device.read_only_zero():private_zero);
        }write.pBufferInfo=&resources.buffers.at(binding.binding);}else{auto found=resources.images.find(binding.binding);if(!found)throw std::runtime_error("Missing Vulkan texture/sampler descriptor");write.pImageInfo=found;}if(write.pBufferInfo){auto &b=*write.pBufferInfo;device.trace("buffer-binding",(uint64_t)write.dstSet,write.dstBinding,(uint64_t)b.buffer,b.offset,b.range,write.descriptorType);}else if(write.pImageInfo){auto &i=*write.pImageInfo;device.trace("image-binding",(uint64_t)write.dstSet,write.dstBinding,(uint64_t)i.imageView,(uint64_t)i.sampler,i.imageLayout,write.descriptorType);}writes.push_back(write);}vkUpdateDescriptorSets(device.handle,(uint32_t)writes.size(),writes.data(),0,nullptr);if(!private_zero.buffer)zero_snapshots_avoided++;return set;
}
static uint64_t surface_span(const Surface &s){
    uint32_t bytes=4,f=s.format;
    if(s.depth)bytes=f==0x13?2:f==0x19?8:4;
    else if(f>=0xc0&&f<=0xc5)bytes=16;
    else if(f>=0xc6&&f<=0xce)bytes=8;
    else if((f>=0xe8&&f<=0xf2)||f==0xf8)bytes=2;
    else if(f>=0xf3&&f<=0xf6)bytes=1;
    uint64_t pitch=((uint64_t)s.width*bytes+63)&~63ull;
    return pitch*((s.height+31ull)&~31ull);
}
static void invalidate_memory(uint64_t address,uint64_t bytes,bool gpu){
    program_codes.clear();
    buffer_cache.invalidate(address,bytes,gpu);
    index_bounds_cache.clear();
    if(!address||!bytes)return;uint64_t end=address+bytes;if(end<address)end=~0ull;
    auto overlaps=[&](uint64_t start,uint64_t size){return start&&start<end&&address<start+size;};
    for(auto it=surfaces.begin();it!=surfaces.end();){auto &s=**it;if(overlaps(gpu?s.va:s.guest,surface_span(s))){for(auto view:s.views)retire_view(view.second);retire_image(s.image);it=surfaces.erase(it);gfx_stat_unmap_dropped++;}else ++it;}
    for(auto it=textures.begin();it!=textures.end();){auto &t=*it->second;if(overlaps(gpu?t.va:t.guest,t.size)){retire_image(t.image);it=textures.erase(it);gfx_stat_unmap_dropped++;}else ++it;}
    draw_snapshots.clear();
}
} // namespace

static uint64_t instanced_draws=0,instanced_saved_draws=0;
static void draw_instances(const u32 *regs,const CbBinding cb[STAGES][18],int indexed,uint32_t instances){guarded("draw",[&]{
    draw_snapshots.clear();
    uint32_t count=indexed?regs[M_DRAW_INDEX_COUNT]:regs[M_VERTEX_ARRAY_COUNT];if(!count)return;
    uint32_t index_format=regs[M_INDEX_BUFFER+4]&3,index_bytes=index_format==2?4:index_format==1?2:1;
    bool direct_indices=indexed&&native_indices(index_format,(regs[M_PRIMITIVE_RESTART]&1)!=0,regs[M_PRIMITIVE_RESTART_INDEX]);
    if(!compute_batch.empty()){
        uint64_t code_region=(uint64_t)regs[M_PROGRAM_REGION]<<32|regs[M_PROGRAM_REGION+1];
        if(pending_overlap(code_region+regs[M_PIPELINE(1)+1],0x40000)||((regs[M_PIPELINE(5)]&1)&&pending_overlap(code_region+regs[M_PIPELINE(5)+1],0x40000)))flush_compute();
        if(indexed){uint64_t va=((uint64_t)regs[M_INDEX_BUFFER]<<32|regs[M_INDEX_BUFFER+1])+(uint64_t)regs[M_INDEX_BUFFER+5]*index_bytes;uint64_t bytes=(uint64_t)count*index_bytes;if(direct_indices)bytes=std::max<uint64_t>(16,bytes);if(pending_overlap(va,bytes)&&(!direct_indices||(!resident_slice(va,bytes).buffer&&!overlay_possible(va,bytes))))flush_compute();}
    }
    uint32_t begin=regs[M_BEGIN];if(begin&(1u<<26))instance_index++;else if(!(begin&(1u<<27)))instance_index=0;
    uint64_t region=(uint64_t)regs[M_PROGRAM_REGION]<<32|regs[M_PROGRAM_REGION+1];auto vs=shader(gpu_va_to_guest(region+regs[M_PIPELINE(1)+1]),SHADER_VERTEX,0,cb[STAGE_VERTEX]);bool has_ps=regs[M_PIPELINE(5)]&1;bool alpha=(regs[M_ALPHA_TEST]&1)&&comparison(regs[M_ALPHA_FUNC])!=VK_COMPARE_OP_ALWAYS;auto ps=has_ps?shader(gpu_va_to_guest(region+regs[M_PIPELINE(5)+1]),SHADER_PIXEL,vs->info.outputs,cb[STAGE_FRAGMENT],nullptr,0,alpha):nullptr;
    preflight_textures(*vs,regs,cb[STAGE_VERTEX]);if(ps)preflight_textures(*ps,regs,cb[STAGE_FRAGMENT]);preflight_uniforms(*vs,cb[STAGE_VERTEX]);if(ps)preflight_uniforms(*ps,cb[STAGE_FRAGMENT]);
    bool vertices_known=!indexed;uint64_t last_vertex=(uint64_t)regs[M_VERTEX_ARRAY_START]+count-1;
    if(indexed&&vertex_bounds_enabled){uint64_t va=((uint64_t)regs[M_INDEX_BUFFER]<<32|regs[M_INDEX_BUFFER+1])+(uint64_t)regs[M_INDEX_BUFFER+5]*index_bytes,bytes=(uint64_t)count*index_bytes,guest=bytes<=(16ull<<20)?linear_guest(va,bytes):0;uint32_t topology=regs[M_BEGIN]&0xffff;bool restart=(regs[M_PRIMITIVE_RESTART]&1)&&(topology==3||topology==5||topology==6);
        // Never read pending GPU-produced indices back just to find bounds.
        if(guest&&!pending_overlap(va,bytes)){auto bounds=index_bounds_cache.get(va,guest,count,index_bytes,restart,regs[M_PRIMITIVE_RESTART_INDEX],read_gpu);if(bounds.known){int64_t base=(int32_t)regs[M_GLOBAL_BASE_VERTEX],lo=(int64_t)bounds.lo+base,hi=(int64_t)bounds.hi+base;if(!bounds.any){vertices_known=true;last_vertex=0;}else if(lo>=0&&hi<=0xffffffffll){vertices_known=true;last_vertex=hi;}}}
    }
    auto attribute_span=[&](uint32_t attr,uint64_t available){if(!vertex_bounds_enabled)return available;uint32_t format=regs[M_VERTEX_ATTRIBUTE(attr)],stream=format&31,base=M_VERTEX_STREAM(stream);bool instanced=(regs[M_VERTEX_STREAM_INSTANCED(stream)]&1)!=0;
        // The software base includes the first Maxwell instance. Native IID
        // supplies the rest; divisors >1 retain the separate-draw fallback.
        uint64_t last=instanced?(uint64_t)regs[M_GLOBAL_BASE_INSTANCE]+instance_index+instances-1:last_vertex;
        return vertex_read_span(available,format,regs[base]&4095,instanced||vertices_known,last);};
    auto preflight_raw=[&](uint64_t va,size_t bytes){bytes=std::max<size_t>(16,bytes);if(pending_overlap(va,bytes)&&!resident_slice(va,bytes).buffer&&!overlay_possible(va,bytes)){resident_fallbacks++;flush_compute();}};
    for(uint32_t attr=0;attr<32;attr++)if(vs->info.inputs>>attr&1){uint32_t fmt=regs[M_VERTEX_ATTRIBUTE(attr)],base=M_VERTEX_STREAM(fmt&31);uint64_t va=(uint64_t)regs[base+1]<<32|regs[base+2],limit=(uint64_t)regs[M_VERTEX_STREAM_LIMIT(fmt&31)]<<32|regs[M_VERTEX_STREAM_LIMIT(fmt&31)+1];if((regs[base]>>12&1)&&va)preflight_raw(va,attribute_span(attr,limit>=va?std::min<uint64_t>(limit-va+1,64u<<20):16));}
    auto preflight_globals=[&](Shader *code,const CbBinding *bindings){if(!code)return;for(uint32_t i=0;i<code->info.sb_count;i++){uint32_t desc[4]={};if(bindings[0].address){uint64_t address=bindings[0].address+code->info.sb_offset[i];if(pending_overlap(address,16))flush_compute();read_gpu(address,16,desc);}uint64_t va=(uint64_t)desc[1]<<32|desc[0];uint32_t size=desc[2]?std::min<uint64_t>(((va+desc[2]+4095)&~4095ull)-va,16u<<20):0;preflight_raw(va,size);}};
    preflight_globals(vs,cb[STAGE_VERTEX]);preflight_globals(ps,cb[STAGE_FRAGMENT]);
    std::vector<Surface *> outputs;Surface *colors[8]={},*depth=nullptr;uint32_t width=0,height=0,attachments=std::min(8u,regs[M_CT_SELECT]&15);
    for(uint32_t i=0;i<attachments;i++){uint32_t target=(regs[M_CT_SELECT]>>(4+3*i))&7;uint32_t base=M_CT(target);uint64_t va=(uint64_t)regs[base]<<32|regs[base+1];if(!va||!regs[base+4])continue;auto s=surface_gpu(va,regs[base+2],regs[base+3],regs[base+4],false);colors[i]=s;outputs.push_back(s);if(!width){width=s->width;height=s->height;}}
    if(regs[M_ZT_SELECT]&1){uint64_t va=(uint64_t)regs[M_ZT_A]<<32|regs[M_ZT_A+1];depth=surface_gpu(va,regs[M_ZT_SIZE],regs[M_ZT_SIZE+1],regs[M_ZT_A+2],true);outputs.push_back(depth);if(!width){width=depth->width;height=depth->height;}}
    if(!width){gfx_stat_skipped++;gfx_skip_target++;return;}
    auto state=draw_state(regs,width,height,outputs[0]->image.width/(float)width,outputs[0]->image.height/(float)height,instance_index);state.pipeline.color_count=attachments;
    for(uint32_t i=0;i<attachments;i++)if(colors[i]){uint32_t target=(regs[M_CT_SELECT]>>(4+3*i))&7;state.pipeline.colors[i]=color_format(regs[M_CT(target)+4]);}
    if(depth){state.pipeline.depth=depth->image.format;if(depth->image.aspect&VK_IMAGE_ASPECT_STENCIL_BIT)state.pipeline.stencil=depth->image.format;else state.pipeline.stencil_test=false;}else{state.pipeline.depth_test=state.pipeline.depth_write=state.pipeline.stencil_test=false;}
    if(regs[M_WINDOW_ORIGIN]&(1<<4)){std::swap(state.pipeline.stencil_front,state.pipeline.stencil_back);std::swap(state.stencil_front,state.stencil_back);}
    uint32_t hardware_mask=0;
    if(hardware_vertices)for(uint32_t attr=0;attr<32;attr++)if(vs->info.inputs>>attr&1){
        uint32_t fmt=regs[M_VERTEX_ATTRIBUTE(attr)],stream=fmt&31,base=M_VERTEX_STREAM(stream),config=regs[base];
        uint64_t start=(uint64_t)regs[base+1]<<32|regs[base+2],limit=(uint64_t)regs[M_VERTEX_STREAM_LIMIT(stream)]<<32|regs[M_VERTEX_STREAM_LIMIT(stream)+1];
        auto input=float_vertex_input(fmt,config&4095,((config>>12)&1)&&start,(regs[M_VERTEX_STREAM_INSTANCED(stream)]&1)!=0,vertices_known,last_vertex,limit>=start?std::min<uint64_t>(limit-start+1,64u<<20):0);
        auto &limits=device.properties.limits;
        if(input.format&&attr<limits.maxVertexInputAttributes&&attr<limits.maxVertexInputBindings&&input.stride<=limits.maxVertexInputBindingStride&&input.offset<=limits.maxVertexInputAttributeOffset&&vertex_format_supported(input.format)){
            state.pipeline.vertices[attr]=input;hardware_mask|=1u<<attr;
        }
    }
    vs=hardware_shader(vs,hardware_mask);
    VkBuffer vertex_buffers[32]={};VkDeviceSize vertex_offsets[32]={};
    Resources resources[2];for(uint32_t attr=0;attr<32;attr++)if(vs->info.inputs>>attr&1){uint32_t fmt=regs[M_VERTEX_ATTRIBUTE(attr)],stream=fmt&31,base=M_VERTEX_STREAM(stream),config=regs[base];uint64_t start=(uint64_t)regs[base+1]<<32|regs[base+2],limit=(uint64_t)regs[M_VERTEX_STREAM_LIMIT(stream)]<<32|regs[M_VERTEX_STREAM_LIMIT(stream)+1];state.params.attr[attr][0]=fmt;state.params.attr[attr][1]=config&4095;state.params.attr[attr][2]=(regs[M_VERTEX_STREAM_INSTANCED(stream)]&1)?std::max(1u,regs[base+3]):0;state.params.attr[attr][3]=((config>>12)&1)&&start;if(state.params.attr[attr][3]){size_t available=limit>=start?std::min<uint64_t>(limit-start+1,64u<<20):16;size_t bytes=attribute_span(attr,available);vertex_span_before+=available;vertex_span_after+=bytes;if(bytes<available)vertex_views_trimmed++;auto uploaded=upload_gpu(start,bytes,UploadKind::Vertex);if(hardware_mask>>attr&1){vertex_buffers[attr]=uploaded.buffer;vertex_offsets[attr]=uploaded.offset;hardware_vertex_attributes++;}else resources[0].buffers[19+attr]=buffer_info(uploaded);}}
    bind_resources(resources[0],*vs,regs,cb[STAGE_VERTEX],state.params,outputs);if(ps)bind_resources(resources[1],*ps,regs,cb[STAGE_FRAGMENT],state.params,outputs);
    auto params=device.upload(sizeof state.params,device.properties.limits.minUniformBufferOffsetAlignment);memcpy(params.data,&state.params,sizeof state.params);auto &cached=program_for(vs,ps);VkDescriptorSet sets[2];sets[0]=descriptors(cached.program.bindings[0],cached.program.sets[0],resources[0],params);if(ps)sets[1]=descriptors(cached.program.bindings[1],cached.program.sets[1],resources[1],params);
    VkBuffer index_buffer=VK_NULL_HANDLE;VkDeviceSize index_offset=0;VkIndexType index_type=VK_INDEX_TYPE_UINT16;
    if(indexed){
        uint64_t va=((uint64_t)regs[M_INDEX_BUFFER]<<32|regs[M_INDEX_BUFFER+1])+(uint64_t)regs[M_INDEX_BUFFER+5]*index_bytes;
        if(direct_indices){auto raw=upload_gpu(va,(uint64_t)count*index_bytes,UploadKind::Index);index_buffer=raw.buffer;index_offset=raw.offset;index_type=index_bytes==4?VK_INDEX_TYPE_UINT32:VK_INDEX_TYPE_UINT16;native_index_draws++;}
        else{CpuTimer timer(index_conversion_ns);uint32_t host_bytes=index_bytes==4?4:2;auto indices=device.upload((uint64_t)count*host_bytes,16);std::vector<uint8_t> source((uint64_t)count*index_bytes);read_gpu(va,source.size(),source.data());for(uint32_t i=0;i<count;i++){uint32_t value=0;memcpy(&value,source.data()+(uint64_t)i*index_bytes,index_bytes);if(state.pipeline.primitive_restart&&value==(regs[M_PRIMITIVE_RESTART_INDEX]&(index_bytes==1?255:index_bytes==2?65535:~0u)))value=~0u;memcpy(indices.data+(uint64_t)i*host_bytes,&value,host_bytes);}index_buffer=indices.buffer;index_offset=indices.offset;index_type=host_bytes==4?VK_INDEX_TYPE_UINT32:VK_INDEX_TYPE_UINT16;converted_index_draws++;}
    }
    // All uploads, copies, and sampling transitions precede the rendering scope.
    const Image *render_images[9]={};for(size_t i=0;i<outputs.size();i++)render_images[i]=&outputs[i]->image;
    auto extent=rendering_extent(render_images,outputs.size());
    state.scissor=rendering_scissor(state.scissor,extent);
    bool mixed=false;for(auto output:outputs)if(output->image.width!=extent.width||output->image.height!=extent.height)mixed=true;
    if(mixed&&++mixed_attachment_draws<=8){fprintf(stderr,"[native attachment bounds] %s %s mask %#x, shared %ux%u;",vs->name.c_str(),ps?ps->name.c_str():"no pixel shader",ps?ps->info.color_mask:0,extent.width,extent.height);for(size_t i=0;i<outputs.size();i++)fprintf(stderr," %s%zu=%ux%u",outputs[i]->depth?"depth":"color",i,outputs[i]->image.width,outputs[i]->image.height);fprintf(stderr,"\n");}
    std::vector<VkRenderingAttachmentInfo> targets(attachments);VkRenderingAttachmentInfo z={VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    for(uint32_t i=0;i<attachments;i++){auto &a=targets[i];a.sType=VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;if(colors[i]){device.transition(colors[i]->image,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);a.imageView=view(*colors[i],state.pipeline.colors[i],VK_IMAGE_VIEW_TYPE_2D);a.imageLayout=colors[i]->image.layout;a.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;a.storeOp=VK_ATTACHMENT_STORE_OP_STORE;}}
    if(depth){device.transition(depth->image,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);z.imageView=depth->image.view;z.imageLayout=depth->image.layout;z.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;z.storeOp=VK_ATTACHMENT_STORE_OP_STORE;}
    VkRenderingInfo info={VK_STRUCTURE_TYPE_RENDERING_INFO};info.renderArea={{0,0},extent};info.layerCount=1;info.colorAttachmentCount=attachments;info.pColorAttachments=targets.data();info.pDepthAttachment=depth?&z:nullptr;info.pStencilAttachment=depth&&(depth->image.aspect&VK_IMAGE_ASPECT_STENCIL_BIT)?&z:nullptr;
    auto pipeline=graphics_pipeline(cached,state.pipeline);device.render(info);vkCmdBindPipeline(device.command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);vkCmdBindDescriptorSets(device.command,VK_PIPELINE_BIND_POINT_GRAPHICS,cached.program.layout,0,ps?2:1,sets,0,nullptr);vkCmdSetViewport(device.command,0,1,&state.viewport);vkCmdSetScissor(device.command,0,1,&state.scissor);vkCmdSetBlendConstants(device.command,state.blend);vkCmdSetStencilReference(device.command,VK_STENCIL_FACE_FRONT_BIT,state.stencil_front);vkCmdSetStencilReference(device.command,VK_STENCIL_FACE_BACK_BIT,state.stencil_back);
    for(uint32_t attr=0;attr<32;attr++)if(hardware_mask>>attr&1)vkCmdBindVertexBuffers(device.command,attr,1,&vertex_buffers[attr],&vertex_offsets[attr]);
    if(hardware_mask)hardware_vertex_draws++;
    if(indexed)vkCmdBindIndexBuffer(device.command,index_buffer,index_offset,index_type);
    device.trace("draw",vs->key,ps?ps->key:0,count,instances,indexed,(uint64_t)index_buffer);
    uint32_t draw_timer=device.gpu_draw_period&&gfx_stat_dispatched>=draw_profile_after?device.draw_timer_begin():~0u;
    if(indexed)vkCmdDrawIndexed(device.command,count,instances,0,(int32_t)regs[M_GLOBAL_BASE_VERTEX],0);else vkCmdDraw(device.command,count,instances,regs[M_VERTEX_ARRAY_START],0);
    if(draw_timer!=~0u)device.draw_timer_end(draw_timer,{timing_key(*vs),ps?timing_key(*ps):0},count,(uint64_t)extent.width*extent.height);gfx_stat_drawn++;
    if(logging)fprintf(stderr,"[native draw] %s %s %ux%u count %u indexed %d\n",vs->name.c_str(),ps?ps->name.c_str():"depth-only",width,height,count,indexed);
    // Start recorded work while the CPU builds subsequent draws. Guest result
    // publication still waits in flush_compute; held readbacks cannot recycle
    // merely because an earlier partial submission completed.
    static uint64_t split_generation=0;static uint32_t split_count=0;
    if(split_generation!=device.generation){split_generation=device.generation;split_count=0;}
    if(early_submit_draws&&++split_count>=early_submit_draws){device.barrier();device.submit(VK_NULL_HANDLE,VK_NULL_HANDLE,false);early_submissions++;}

});if((indexed?regs[M_DRAW_INDEX_COUNT]:regs[M_VERTEX_ARRAY_COUNT])!=0)instance_index+=instances-1;}

extern "C" void gfx_draw(const u32 *regs,const CbBinding cb[STAGES][18],int indexed){draw_instances(regs,cb,indexed,1);}
extern "C" void gfx_native_draw_instances(const u32 *regs,const CbBinding cb[STAGES][18],int indexed,u32 instances){
    // Preserve the existing one-draw semantics for divisors >1. Their current
    // software fetch base differs from Vulkan's iid/divisor for a combined draw.
    bool safe=instances>0&&instances<=4096;
    uint32_t first=(regs[M_BEGIN]&(1u<<26))?instance_index+1:((regs[M_BEGIN]&(1u<<27))?instance_index:0);
    if((uint64_t)regs[M_GLOBAL_BASE_INSTANCE]+first+instances>0xffffffffull)safe=false;
    for(uint32_t stream=0;stream<32;stream++)if((regs[M_VERTEX_STREAM_INSTANCED(stream)]&1)&&regs[M_VERTEX_STREAM(stream)+3]>1)safe=false;
    if(!safe){u32 copy[0x1000];memcpy(copy,regs,sizeof copy);for(u32 i=0;i<instances;i++){if(i)copy[M_BEGIN]=(copy[M_BEGIN]&~(1u<<27))|(1u<<26);draw_instances(copy,cb,indexed,1);}return;}
    if(instances>1){instanced_draws++;instanced_saved_draws+=instances-1;}
    draw_instances(regs,cb,indexed,instances);
}

extern "C" void gfx_dispatch(const u32 *regs,const u32 *q){guarded("dispatch",[&]{
    CpuTimer dispatch_timer(compute_total_ns);
    draw_snapshots.clear();
    uint32_t local[]={q[18]>>16,q[19]&65535,q[19]>>16},grid[]={q[12]&0x7fffffff,q[13]&65535,q[13]>>16};if(!grid[0]||!grid[1]||!grid[2])return;CbBinding cb[18]={};for(uint32_t i=0;i<8;i++)if(q[20]>>i&1){cb[i].address=(uint64_t)(q[30+2*i]&255)<<32|q[29+2*i];cb[i].size=q[30+2*i]>>15;}uint64_t region=(uint64_t)regs[M_PROGRAM_REGION]<<32|regs[M_PROGRAM_REGION+1];if(!compute_batch.empty()){if(pending_overlap(region+q[8],0x40000))flush_compute();}
    auto cs=shader(gpu_va_to_guest(region+q[8]),SHADER_COMPUTE,0,cb,local,q[17]&0x3ffff);nativevk::DrawParams params;Resources resources;
    struct Range {uint64_t lo,hi;Buffer buffer;VkDeviceSize offset=0;bool resident=false;};std::vector<Range> ranges;uint64_t addresses[16]={};uint32_t sizes[16]={};
    for(uint32_t i=0;i<cs->info.sb_count;i++){uint32_t desc[4]={};if(cb[0].address){uint64_t address=cb[0].address+cs->info.sb_offset[i];if(pending_overlap(address,16))flush_compute();read_gpu(address,16,desc);}uint64_t va=(uint64_t)desc[1]<<32|desc[0];uint32_t size=std::min(desc[2],64u<<20);addresses[i]=va;sizes[i]=size;if(va&&size)ranges.push_back({va,va+((size+3ull)&~3ull),{}});}
    std::sort(ranges.begin(),ranges.end(),[](const Range &a,const Range &b){return a.lo<b.lo;});for(size_t i=1;i<ranges.size();)if(ranges[i].lo<=ranges[i-1].hi){ranges[i-1].hi=std::max(ranges[i-1].hi,ranges[i].hi);ranges.erase(ranges.begin()+i);}else i++;
    uint64_t incoming_bytes=0;for(auto &r:ranges)incoming_bytes+=r.hi-r.lo;
    if(incoming_bytes+compute_batch.pending_bytes>(64ull<<20))flush_compute();
    bool defer=batch_compute;
    for(auto &r:ranges){uint64_t size=r.hi-r.lo,guest=gpu_va_to_guest(r.lo);if(pending_overlap(r.lo,size)&&!resident_slice(r.lo,size).buffer){resident_fallbacks++;flush_compute();}if(!guest||gpu_va_to_guest(r.hi-1)!=guest+size-1)defer=false;}
    for(size_t i=0;i<ranges.size();i++)for(size_t j=0;j<i;j++){uint64_t a=gpu_va_to_guest(ranges[i].lo),b=gpu_va_to_guest(ranges[j].lo);if(a&&b&&a<b+ranges[j].hi-ranges[j].lo&&b<a+ranges[i].hi-ranges[i].lo)defer=false;}
    if(!defer)flush_compute();
    // Texture and descriptor CPU reads must be coherent before any frame slices
    // are prepared: a dependency flush submits and switches frame contexts.
    preflight_textures(*cs,regs,cb);preflight_uniforms(*cs,cb);
    {CpuTimer input_timer(compute_input_ns);for(auto &r:ranges){auto kept=defer?resident_slice(r.lo,r.hi-r.lo):Slice{};if(kept.buffer){r.buffer.handle=kept.buffer;r.buffer.mapped=kept.data;r.buffer.size=kept.size;r.offset=kept.offset;r.resident=true;resident_compute_reuses++;}else{r.buffer=compute_readbacks.acquire(r.hi-r.lo);read_gpu(r.lo,r.hi-r.lo,r.buffer.mapped);}}}
    for(uint32_t i=0;i<cs->info.sb_count;i++){params.sb[0][i][0]=(uint32_t)addresses[i];params.sb[0][i][1]=addresses[i]>>32;params.sb[0][i][2]=sizes[i];for(auto &r:ranges)if(addresses[i]>=r.lo&&addresses[i]+sizes[i]<=r.hi){params.sb[0][i][0]=(uint32_t)r.lo;params.sb[0][i][1]=r.lo>>32;params.sb[0][i][2]=(uint32_t)(r.hi-r.lo);resources.buffers[256+i]={r.buffer.handle,r.offset,r.hi-r.lo};break;}}
    bind_resources(resources,*cs,regs,cb,params,{});auto uniform=device.upload(sizeof params,device.properties.limits.minUniformBufferOffsetAlignment);memcpy(uniform.data,&params,sizeof params);auto &program=program_for(cs,nullptr);auto &pipeline=program.pipelines[{}];if(!pipeline)pipeline=program.program.dispatch();auto set=descriptors(program.program.bindings[0],program.program.sets[0],resources,uniform);device.barrier();vkCmdBindPipeline(device.command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);vkCmdBindDescriptorSets(device.command,VK_PIPELINE_BIND_POINT_COMPUTE,program.program.layout,0,1,&set,0,nullptr);auto gpu_timer=device.compute_timer_begin();device.trace("dispatch",cs->key,grid[0],grid[1],grid[2],ranges.size());vkCmdDispatch(device.command,grid[0],grid[1],grid[2]);device.compute_timer_end(gpu_timer);device.barrier();
    if(defer&&cs->info.stores){for(auto &r:ranges)if(!r.resident){compute_batch.add(r.lo,gpu_va_to_guest(r.lo),r.hi-r.lo,r.buffer);compute_readbacks.hold(r.buffer);}deferred_dispatches++;}
    else{flush_compute();{CpuTimer wait_timer(compute_wait_ns);device.submit();}gfx_stat_gpu_waits++;retire_completed();{CpuTimer output_timer(compute_output_ns);if(cs->info.stores)for(auto &r:ranges)write_gpu(r.lo,r.hi-r.lo,r.buffer.mapped);}}
    gfx_stat_dispatched++;
});}

namespace {
Program clear_program;
std::map<std::pair<VkFormat,uint32_t>,VkPipeline> clear_pipelines;
static void clear_color(Surface &surface,const float *values,uint32_t mask){
    auto &image=surface.image;
    VkFormat target_format=color_format(surface.format);
    if(mask==15&&target_format!=VK_FORMAT_R8G8B8A8_SRGB&&target_format!=VK_FORMAT_B8G8R8A8_SRGB){device.transition(image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);VkClearColorValue color;memcpy(color.float32,values,16);VkImageSubresourceRange range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};vkCmdClearColorImage(device.command,image.handle,image.layout,&color,1,&range);return;}
    if(!clear_program.device){auto vs=compile("float4 main(uint id:SV_VertexID):SV_Position{return float4(id==1?3:-1,id==2?3:-1,0,1);}",0,"clear VS");auto ps=compile("cbuffer Color:register(b0){float4 color;}float4 main():SV_Target0{return color;}",1,"masked clear PS");clear_program.init_graphics(device,vs,&ps);}
    auto &pipeline=clear_pipelines[{target_format,mask}];if(!pipeline){GraphicsState state;state.color_count=1;state.colors[0]=target_format;state.blends[0].colorWriteMask=mask;pipeline=clear_program.graphics(state);}
    auto color=device.upload(16,device.properties.limits.minUniformBufferOffsetAlignment);memcpy(color.data,values,16);Resources resources;auto set=descriptors(clear_program.bindings[1],clear_program.sets[1],resources,color);device.transition(image,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);VkRenderingAttachmentInfo attachment={VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};attachment.imageView=view(surface,target_format,VK_IMAGE_VIEW_TYPE_2D);attachment.imageLayout=image.layout;attachment.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;VkRenderingInfo render={VK_STRUCTURE_TYPE_RENDERING_INFO};render.renderArea={{0,0},{image.width,image.height}};render.layerCount=1;render.colorAttachmentCount=1;render.pColorAttachments=&attachment;
    device.render(render);vkCmdBindPipeline(device.command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);vkCmdBindDescriptorSets(device.command,VK_PIPELINE_BIND_POINT_GRAPHICS,clear_program.layout,1,1,&set,0,nullptr);VkViewport viewport={0,0,(float)image.width,(float)image.height,0,1};VkRect2D scissor={{0,0},{image.width,image.height}};float blend[4]={};vkCmdSetViewport(device.command,0,1,&viewport);vkCmdSetScissor(device.command,0,1,&scissor);vkCmdSetBlendConstants(device.command,blend);vkCmdSetStencilReference(device.command,VK_STENCIL_FACE_FRONT_AND_BACK,0);vkCmdDraw(device.command,3,1,0,0);
}
static void screenshot(Image &source){
    auto converted=device.image(source.width,source.height,1,1,1,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT);auto buffer=device.buffer((uint64_t)source.width*source.height*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT);device.transition(source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);device.transition(converted,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);VkImageBlit blit={};blit.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};blit.dstSubresource=blit.srcSubresource;blit.srcOffsets[1]={(int)source.width,(int)source.height,1};blit.dstOffsets[1]=blit.srcOffsets[1];vkCmdBlitImage(device.command,source.handle,source.layout,converted.handle,converted.layout,1,&blit,VK_FILTER_NEAREST);device.transition(converted,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);VkBufferImageCopy copy={};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={source.width,source.height,1};vkCmdCopyImageToBuffer(device.command,converted.handle,converted.layout,buffer.handle,1,&copy);device.barrier();device.submit();retire_completed();
    CreateDirectoryA("screenshots",nullptr);SYSTEMTIME now;GetLocalTime(&now);char path[256];snprintf(path,sizeof path,"screenshots/native_%04u%02u%02u_%02u%02u%02u_%03u.bmp",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,now.wMilliseconds);FILE *file=fopen(path,"wb");if(!file)throw std::runtime_error("Cannot save native Vulkan screenshot");BITMAPFILEHEADER header={};BITMAPINFOHEADER info={};header.bfType=0x4d42;header.bfOffBits=sizeof header+sizeof info;header.bfSize=header.bfOffBits+source.width*source.height*4;info.biSize=sizeof info;info.biWidth=source.width;info.biHeight=-(LONG)source.height;info.biPlanes=1;info.biBitCount=32;info.biSizeImage=source.width*source.height*4;for(uint64_t i=0;i<(uint64_t)source.width*source.height;i++)std::swap(buffer.mapped[i*4],buffer.mapped[i*4+2]);fwrite(&header,sizeof header,1,file);fwrite(&info,sizeof info,1,file);fwrite(buffer.mapped,info.biSizeImage,1,file);fclose(file);fprintf(stderr,"[native Vulkan] screenshot %s\n",path);device.destroy(buffer);device.destroy(converted);
}
}
extern "C" void gfx_clear(const u32 *regs,u32 arg){guarded("clear",[&]{
    // Image clears do not read compute buffers on the CPU. Preserve ordering
    // on the GPU; publication remains at real dependencies and CPU boundaries.
    uint32_t target=(arg>>6)&15;if((arg&0x3c)&&target<8){uint32_t base=M_CT(target);auto s=surface_gpu((uint64_t)regs[base]<<32|regs[base+1],regs[base+2],regs[base+3],regs[base+4],false);float values[4];for(int i=0;i<4;i++)values[i]=floating(regs[M_COLOR_CLEAR(i)]);clear_color(*s,values,(arg>>2)&15);}if(arg&3){auto s=surface_gpu((uint64_t)regs[M_ZT_A]<<32|regs[M_ZT_A+1],regs[M_ZT_SIZE],regs[M_ZT_SIZE+1],regs[M_ZT_A+2],true);auto &image=s->image;device.transition(image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);VkImageAspectFlags aspects=(arg&1?VK_IMAGE_ASPECT_DEPTH_BIT:0)|(arg&2?VK_IMAGE_ASPECT_STENCIL_BIT:0);aspects&=image.aspect;if(aspects){VkClearDepthStencilValue value={floating(regs[M_Z_CLEAR]),regs[M_STENCIL_CLEAR]&255};VkImageSubresourceRange range={aspects,0,1,0,1};vkCmdClearDepthStencilImage(device.command,image.handle,image.layout,&value,1,&range);}}gfx_stat_clears++;});}
extern "C" void gfx_blit2d(const u32 *regs){guarded("2D blit",[&]{flush_compute();uint64_t src=gpu_va_to_guest((uint64_t)regs[0x94]<<32|regs[0x95]),dst=gpu_va_to_guest((uint64_t)regs[0x88]<<32|regs[0x89]);Surface *source=nullptr;for(auto it=surfaces.rbegin();it!=surfaces.rend();++it)if((*it)->guest==src&&!(*it)->depth){source=it->get();break;}if(!source){fprintf(stderr,"[native Vulkan] CPU-only 2D blit not supported by the existing renderer path\n");gfx_stat_skipped++;return;}auto target=surface_gpu((uint64_t)regs[0x88]<<32|regs[0x89],regs[0x86],regs[0x87],regs[0x80],false);if(target==source)throw std::runtime_error("Overlapping 2D blit needs a source snapshot");double du=regs[0x231]+regs[0x230]/4294967296.,dv=regs[0x233]+regs[0x232]/4294967296.,sx=regs[0x235]+regs[0x234]/4294967296.,sy=(int32_t)regs[0x237]+regs[0x236]/4294967296.;uint32_t x=regs[0x22c],y=regs[0x22d],w=regs[0x22e],h=regs[0x22f];float scale_src_x=source->image.width/(float)source->width,scale_src_y=source->image.height/(float)source->height,scale_dst_x=target->image.width/(float)target->width,scale_dst_y=target->image.height/(float)target->height;VkImageBlit region={};region.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.dstSubresource=region.srcSubresource;region.srcOffsets[0]={(int)(sx*scale_src_x),(int)(sy*scale_src_y),0};region.srcOffsets[1]={(int)((sx+w*du)*scale_src_x),(int)((sy+h*dv)*scale_src_y),1};region.dstOffsets[0]={(int)(x*scale_dst_x),(int)(y*scale_dst_y),0};region.dstOffsets[1]={(int)((x+w)*scale_dst_x),(int)((y+h)*scale_dst_y),1};device.transition(source->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);device.transition(target->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);vkCmdBlitImage(device.command,source->image.handle,source->image.layout,target->image.handle,target->image.layout,1,&region,regs[0x223]&(1<<4)?VK_FILTER_LINEAR:VK_FILTER_NEAREST);gfx_stat_blits++;});}
extern "C" void gfx_present(u64 guest,u32 width,u32 height){guarded("present",[&]{
    flush_compute();
    texture_epoch++;
    watchdog_present();hitch_present();Surface *source=nullptr;
    for(auto &candidate:surfaces)if(candidate->guest==guest&&!candidate->depth&&(!source||candidate->used>source->used))source=candidate.get();
    if(!source){device.submit();retire_completed();return;}
    LONG pending=InterlockedExchange(&keys,0);logging=(pending&8)!=0;
    if(pending&8)snapshot_diagnostics();
    if((pending&16)&&early_submit_toggle){early_submit_draws=early_submit_draws?0:256;fprintf(stderr,"[native early submissions] threshold %u at frame %llu, dispatch %llu\n",early_submit_draws,gfx_stat_presented,gfx_stat_dispatched);}
    else if((pending&16)&&getenv("SSBU_VK_SERIAL_PRESENT_TOGGLE")&&strcmp(getenv("SSBU_VK_SERIAL_PRESENT_TOGGLE"),"0")){presentation.wait_for_gpu=!presentation.wait_for_gpu;fprintf(stderr,"[native serial presentation] toggled %s at frame %llu, dispatch %llu\n",presentation.wait_for_gpu?"ON":"OFF",gfx_stat_presented,gfx_stat_dispatched);}
    else if((pending&16)&&getenv("SSBU_VK_OVERLAY_INIT_TOGGLE")&&strcmp(getenv("SSBU_VK_OVERLAY_INIT_TOGGLE"),"0")){skip_overlay_init=!skip_overlay_init;fprintf(stderr,"[native overlay init] toggled %s at frame %llu\n",skip_overlay_init?"SKIP":"BASELINE",gfx_stat_presented);}
    else if((pending&16)&&getenv("SSBU_VK_INSTANCE_TOGGLE")&&strcmp(getenv("SSBU_VK_INSTANCE_TOGGLE"),"0")){fprintf(stderr,"[native instancing] toggled %s at frame %llu\n",gfx_native_toggle_instance_batch()?"ON":"OFF",gfx_stat_presented);}
    else if((pending&16)&&hardware_vertex_toggle){hardware_vertices=!hardware_vertices;fprintf(stderr,"[native hardware vertex] toggled %s at frame %llu, dispatch %llu\n",hardware_vertices?"ON":"OFF",gfx_stat_presented,gfx_stat_dispatched);}
    else if((pending&16)&&snapshot_toggle){draw_snapshots.set_flat(!draw_snapshots.flat());fprintf(stderr,"[native snapshot table] %s (CPU lookup only)\n",draw_snapshots.flat()?"FLAT":"MAP");}
    else if((pending&16)&&resource_toggle){dense_resources=!dense_resources;fprintf(stderr,"[native resource tables] %s (CPU descriptor metadata only)\n",dense_resources?"DENSE":"MAP");}
    if(pending&32)profile_start(10);
    static uint64_t profile_dispatch=getenv("SSBU_VK_PROFILE_DISPATCH")?strtoull(getenv("SSBU_VK_PROFILE_DISPATCH"),nullptr,10):0;
    if(profile_dispatch&&gfx_stat_dispatched>=profile_dispatch){profile_dispatch=0;profile_start(10);}

    if(pending&4)screenshot(source->image);
    bool shown=presentation.present(source->image);
    if(!shown)device.submit();
    retire_completed();
    if(shown){
        gfx_stat_presented++;
        compare_sparse_phase();compare_attachment_phase();
        static uint64_t capture=getenv("SSBU_VK_CAPTURE_FRAME")?strtoull(getenv("SSBU_VK_CAPTURE_FRAME"),nullptr,10):0;
        static uint64_t capture_start=rt_clock_ns();
        static uint64_t capture_seconds=getenv("SSBU_VK_CAPTURE_SECONDS")?strtoull(getenv("SSBU_VK_CAPTURE_SECONDS"),nullptr,10):0;
        static uint64_t capture_dispatch=getenv("SSBU_VK_CAPTURE_DISPATCH")?strtoull(getenv("SSBU_VK_CAPTURE_DISPATCH"),nullptr,10):0;
        static uint64_t exit_frame=getenv("SSBU_VK_EXIT_FRAME")?strtoull(getenv("SSBU_VK_EXIT_FRAME"),nullptr,10):0;
        if(capture_seconds&&rt_clock_ns()-capture_start>=capture_seconds*1000000000ull){capture_seconds=0;fprintf(stderr,"[native Vulkan] timed scene capture at frame %llu\n",gfx_stat_presented);screenshot(source->image);}
        if(capture_dispatch&&gfx_stat_dispatched>=capture_dispatch){capture_dispatch=0;fprintf(stderr,"[native Vulkan] dispatch-triggered scene capture at frame %llu, dispatch %llu\n",gfx_stat_presented,gfx_stat_dispatched);screenshot(source->image);}
    if(capture&&gfx_stat_presented==capture){screenshot(source->image);if(getenv("SSBU_VK_CAPTURE_TARGETS"))for(auto &target:surfaces)if(!target->depth){fprintf(stderr,"[native Vulkan] target guest %#llx %ux%u format %#x used %llu\n",target->guest,target->width,target->height,target->format,target->used);screenshot(target->image);}}
        if(exit_frame&&gfx_stat_presented>=exit_frame){if(device.submission_trace)device.dump_submission_trace(device.last_submitted,"bounded self-check");fprintf(stderr,"[native Vulkan] bounded run completed: %llu frames\n",gfx_stat_presented);device.save_cache();fflush(stderr);ExitProcess(0);}
    }
    // A large driver-cache snapshot can stall for hundreds of milliseconds.
    // Recipes/SPIR-V are recorded immediately; checkpoint the driver cache by
    // elapsed time, not every few hundred frames. Normal close still forces it.
    uint64_t now=rt_clock_ns();if(!cache_checkpoint_ns)cache_checkpoint_ns=now;
    if(now-cache_checkpoint_ns>=30000000000ull){device.save_cache();cache_checkpoint_ns=rt_clock_ns();}
    if(InterlockedExchange(&draw_profile_pending,0))draw_timing_diagnostics();
});}
// In batching mode, independent results are published at dependencies, graphics,
// stream ends, or explicit CPU/fence visibility points in the native command processor.
extern "C" int gfx_copy_resident(u64,u64,u64){if(ready&&!compute_batch.empty())guarded("copy compute publication",[&]{flush_compute();});return 0;}
extern "C" void gfx_resident_flush(u64 va,u64 bytes){if(ready&&batch_compute&&!compute_batch.empty()&&pending_overlap(va,bytes))guarded("compute publication",[&]{flush_compute();});}
extern "C" void gfx_native_cpu_access(u64 va,u64 bytes,const char *site,int line){if(ready&&batch_compute&&!compute_batch.empty()&&pending_overlap(va,bytes))guarded("command CPU visibility",[&]{publish_compute(site,line);});}
extern "C" void gfx_native_fence_notify(u64 guest){notify_fence(P(void,guest));}
extern "C" void gfx_native_fence_pause(u64 guest,u32 target,u64 remaining_us){pause_fence(P(void,guest),target,remaining_us);}
extern "C" void gfx_resident_written(u64,u64){}
extern "C" void gfx_stream_wrote(u64,u64){}
extern "C" void gfx_guest_wrote(u64 address,u64 bytes){
#ifdef SSBU_OPENSMASH_PORT
if(ready==2){opensmash_port_memory_written(address,bytes);return;}
#endif
if(ready)guarded("texture write notification",[&]{texture_epoch++;});}
extern "C" void gfx_stream_end(void){if(ready&&!compute_batch.empty())guarded("compute stream publication",[&]{flush_compute();});}
extern "C" void gfx_gpu_unmapped(u64 address,u64 bytes){
#ifdef SSBU_OPENSMASH_PORT
if(ready==2)return; // NVN buffer addresses are guest addresses, not Maxwell VA mappings.
#endif
if(ready)guarded("GPU memory invalidation",[&]{gfx_stat_unmapped++;flush_compute();invalidate_memory(address,bytes,true);});}
extern "C" void gfx_guest_unmapped(u64 address,u64 bytes){
#ifdef SSBU_OPENSMASH_PORT
if(ready==2){opensmash_port_memory_retire(address,bytes);return;}
#endif
if(ready)guarded("guest memory invalidation",[&]{gfx_stat_unmapped++;flush_compute();invalidate_memory(address,bytes,false);});}
extern "C" int gfx_draw_logging(void){return logging;}
extern "C" void *gfx_window_handle(void){return window;}
extern "C" void *gfx_display_output(void){return nullptr;} // shared display falls back to clock pacing
extern "C" int gfx_window_focused(void){return window&&GetForegroundWindow()==window;}
extern "C" DWORD gfx_lock_owner(void){return ready?(DWORD)(uintptr_t)lock.OwningThread:0;}
extern "C" DWORD gfx_last_enter_thread(void){return last_thread;}
extern "C" void gfx_hotkey(int key){InterlockedOr(&keys,1<<key);}
extern "C" void gfx_status_lines(void){
#ifdef SSBU_OPENSMASH_PORT
if(opensmash_port_selected()){if(ready==2)opensmash_port_report();return;}
#endif
fprintf(stderr,"[native cache versions] %s, %llu fresh versions, %llu slab rollovers\n",buffer_cache.append_only()?"APPEND":"REUSE",buffer_cache.version_allocations,buffer_cache.rollovers);fprintf(stderr,"[native overlay init] %s, %llu copies avoided, %llu MiB initialization avoided\n",skip_overlay_init?"SKIP":"BASELINE",overlay_init_skips,overlay_init_bytes>>20);fprintf(stderr,"[native instancing] %llu batched draws, %llu host draws avoided\n",instanced_draws,instanced_saved_draws);fprintf(stderr,"[native early submissions] threshold %u, %llu partial graphics submissions\n",early_submit_draws,early_submissions);fprintf(stderr,"[native hardware vertex] %s, %llu draws, %llu attributes; bounded aligned float32 only\n",hardware_vertices?"ON":"OFF",hardware_vertex_draws,hardware_vertex_attributes);fprintf(stderr,"[native vertex fast path] %llu vertex programs use aligned-float direct loads (other formats retain reference decoding)\n",work_stats.vertex_fast_programs);fprintf(stderr,"[native render barriers] %llu full, %llu attachment-stage\n",device.full_render_barriers,device.attachment_render_barriers);draw_timing_diagnostics();constant_diagnostics();fprintf(stderr,"[native constant dedup] %llu unchanged dirty snapshots reused, %llu MiB compared, %llu MiB shadow copies\n",buffer_cache.verified_reuses,buffer_cache.verified_bytes>>20,buffer_cache.shadow_copied_bytes>>20);fprintf(stderr,"[native CPU buffer copies] vertex %llu MiB, constants %llu MiB, globals %llu MiB, indices %llu MiB, other %llu MiB\n",cpu_buffer_bytes[0]>>20,cpu_buffer_bytes[1]>>20,cpu_buffer_bytes[2]>>20,cpu_buffer_bytes[3]>>20,cpu_buffer_bytes[4]>>20);fprintf(stderr,"[native vertex spans] %llu attributes narrowed, %llu -> %llu MiB requested; index bounds %llu hits / %llu scans (%llu MiB read)\n",vertex_views_trimmed,vertex_span_before>>20,vertex_span_after>>20,index_bounds_cache.hits,index_bounds_cache.scans,index_bounds_cache.bytes_read>>20);fprintf(stderr,"[native shader bindings] %llu active / %llu declared, %.1f ms interface scan\n",work_stats.active_bindings,work_stats.declared_bindings,work_stats.interface_ns/1e6);fprintf(stderr,"[native zero buffers] %llu stage snapshots avoided (%llu MiB), %llu private writable snapshots\n",zero_snapshots_avoided,zero_snapshots_avoided>>8,private_zero_snapshots);fprintf(stderr,"[native indices] %llu direct bindings, %llu converted draws, %.1f ms conversion\n",native_index_draws,converted_index_draws,index_conversion_ns/1e6);fprintf(stderr,"[native overlays] %llu GPU buffer views, %llu MiB copied on GPU\n",resident_overlay_reads,resident_overlay_bytes>>20);fprintf(stderr,"[native cache saves] %llu writes, %.1f ms cumulative\n",device.cache_saves,device.cache_save_ns/1e6);for(auto &entry:publication_sites){auto &s=entry.second;fprintf(stderr,"[native publication] %s:%d count %llu wait %.1f ms copy %.1f ms\n",s.function,entry.first.second,s.count,s.wait_ns/1e6,s.copy_ns/1e6);}if(batch_compute)fprintf(stderr,"[native residency] %llu compute-buffer reuses, %llu direct shader-buffer reads, %llu incompatible-range fallbacks\n",resident_compute_reuses,resident_draw_reads,resident_fallbacks);if(batch_compute)fprintf(stderr,"[native compute batching] %llu deferred dispatches, %llu result ranges, %llu publication waits, %llu maximum pending ranges\n",deferred_dispatches,compute_batch.queued,compute_batch.flushes,compute_batch.max_ranges);fprintf(stderr,"[native shader lookup] %llu matching-key candidates checked across %zu variants\n",shader_variant_probes,shaders.size());if(device.gpu_timings)fprintf(stderr,"[native GPU time] compute %.1f ms/%llu dispatches; whole submissions %.1f ms/%llu batches (GPU timestamps, not CPU waits)\n",device.gpu_compute_ns/1e6,device.gpu_timed_dispatches,device.gpu_batch_ns/1e6,device.gpu_timed_batches);fprintf(stderr,"[native shader code] %llu unchanged reuses, %llu MiB checksummed\n",shader_code_hits,shader_hash_bytes>>20);fprintf(stderr,"[native compute pool] %llu allocations, %llu reuses, %llu MiB retained\n",compute_readbacks.allocations,compute_readbacks.hits,compute_readbacks.allocated>>20);fprintf(stderr,"[native work totals] translate %.1f ms, rewrite %.1f ms, DXC %.1f ms (%llu cache hits, %llu misses); graphics pipelines %.1f ms/%llu, compute pipelines %.1f ms/%llu\n",translation_ns/1e6,work_stats.rewrite_ns/1e6,work_stats.dxc_ns/1e6,work_stats.cache_hits,work_stats.cache_misses,work_stats.graphics_ns/1e6,work_stats.graphics_count,work_stats.compute_ns/1e6,work_stats.compute_count);fprintf(stderr,"[native compute totals] inclusive %.1f ms; input/allocation %.1f ms, submit/wait %.1f ms, guest writeback %.1f ms across %llu dispatches\n",compute_total_ns/1e6,compute_input_ns/1e6,compute_wait_ns/1e6,compute_output_ns/1e6,gfx_stat_dispatched);fprintf(stderr,"[native Vulkan] %llu draws, %llu dispatches, %llu presents; experimental synchronous resource bridge\n",gfx_stat_drawn,gfx_stat_dispatched,gfx_stat_presented);fprintf(stderr,"[native perf] cumulative texture %.1f ms, shader lookup %.1f ms; %llu texture hits, %llu MiB hashed, %llu program decodes\n",texture_cpu_ns/1e6,shader_cpu_ns/1e6,texture_hits,texture_hash_bytes>>20,shader_decodes);fprintf(stderr,"[native perf] %llu rendering passes for %llu draws; %llu buffer reuses, %llu MiB buffer data copied\n",device.render_passes,gfx_stat_drawn,buffer_cache.hits,(buffer_cache.copied+gfx_stat_upload_bytes)>>20);fprintf(stderr,"[native perf] cumulative uploads %.1f ms, fence %.1f ms, submit %.1f ms, acquire %.1f ms, present %.1f ms across %llu submissions\n",buffer_cpu_ns/1e6,device.fence_wait_ns/1e6,device.queue_submit_ns/1e6,presentation.acquire_ns/1e6,presentation.present_ns/1e6,device.submissions);}
extern "C" void gfx_closing(void){
#ifdef SSBU_OPENSMASH_PORT
if(opensmash_port_selected()){opensmash_port_close();return;}
#endif
if(ready&&TryEnterCriticalSection(&lock)){device.save_cache();LeaveCriticalSection(&lock);}}
extern "C" void gfx_report_compiles(void (*line)(const char *)){line("Native Vulkan: synchronous SPIR-V/pipeline compilation (experimental)");}
extern "C" int gfx_warm_shaders_main(const char *,const char *){fprintf(stderr,"[native Vulkan] Offline D3D shader warming is unavailable for this backend\n");return 1;}
extern "C" int gfx_adapter_names(char names[][64],int max){if(max<=0)return 0;if(ready){snprintf(names[0],64,"%s",device.properties.deviceName);return 1;}return 0;}
// NVN uses a native Online settings window, independent of GPU overlay rendering.
extern "C" int menu_is_open(void){
#ifdef SSBU_OPENSMASH_PORT
return nvn_online_menu_open();
#else
return 0;
#endif
}
extern "C" int menu_blocks_input(void){return menu_is_open();}
extern "C" int menu_visible(void){return menu_is_open();}
extern "C" void menu_key(int key){
#ifdef SSBU_OPENSMASH_PORT
if(key==1 && window && opensmash_port_selected())PostMessageW(window,WM_APP+77,0,0);
#else
(void)key;
#endif
}
extern "C" void menu_char(unsigned){}
extern "C" int menu_frame(int *,int){return 0;}
extern "C" void menu_draw(float,float){}
extern "C" void menu_toast(const char *text){fprintf(stderr,"[native Vulkan] %s\n",text);}

#ifdef SSBU_OPENSMASH_PORT
extern "C" void gfx_nvn_ensure_window(void){InitOnceExecuteOnce(&once,initialize,nullptr,nullptr);}
#endif
