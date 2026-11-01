#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#include <windows.h>
#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <array>

namespace nativevk {
#define VK_FN(name) extern PFN_vk##name vk##name;
#include "vk_api.inc"
#undef VK_FN

void check(VkResult result,const char *operation);
uint64_t elapsed_ns(LARGE_INTEGER start);
struct WorkStats {
    uint64_t rewrite_ns=0,dxc_ns=0,cache_hits=0,cache_misses=0;
    uint64_t graphics_ns=0,compute_ns=0,graphics_count=0,compute_count=0;
    uint64_t declared_bindings=0,active_bindings=0,interface_ns=0;
    uint64_t vertex_fast_programs=0;
};
extern WorkStats work_stats;
struct Buffer {
    VkBuffer handle=VK_NULL_HANDLE;
    VkDeviceMemory memory=VK_NULL_HANDLE;
    uint8_t *mapped=nullptr;
    VkDeviceSize size=0;
    VkMemoryPropertyFlags memory_flags=0;
};
struct Image {
    VkImage handle=VK_NULL_HANDLE;
    VkDeviceMemory memory=VK_NULL_HANDLE;
    VkImageView view=VK_NULL_HANDLE;
    VkFormat format=VK_FORMAT_UNDEFINED;
    VkImageAspectFlags aspect=VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED;
    uint32_t width=0,height=0,depth=1,layers=1,levels=1;
    VkImageViewType type=VK_IMAGE_VIEW_TYPE_2D;
};
// Dynamic rendering may bind different image sizes. Its area must fit every
// bound color/depth/stencil image; viewport coordinates remain independent.
VkExtent2D rendering_extent(const Image *const *images,size_t count);
VkRect2D rendering_scissor(VkRect2D scissor,VkExtent2D extent);
struct Slice { VkBuffer buffer; VkDeviceSize offset,size; uint8_t *data; };
using DrawTimingKey=std::pair<uint64_t,uint64_t>;
struct DrawTiming {uint64_t samples=0,ns=0,vertices=0,pixels=0;};
struct DrawQuery {uint32_t first;DrawTimingKey key;uint32_t vertices;uint64_t pixels;};
struct FrameState {
    struct TraceEvent {const char *kind=nullptr;uint64_t values[6]={};};
    std::array<TraceEvent,256> trace_events={};
    uint64_t trace_count=0;
    VkCommandPool commands=VK_NULL_HANDLE;
    VkCommandBuffer command=VK_NULL_HANDLE;
    VkFence fence=VK_NULL_HANDLE;
    VkDescriptorPool descriptors=VK_NULL_HANDLE;
    VkSemaphore acquired=VK_NULL_HANDLE;
    std::vector<Buffer> uploads;
    size_t upload_chunk=0;
    VkDeviceSize upload_at=0;
    uint64_t serial=0;
    bool submitted=false;
    VkQueryPool timestamps=VK_NULL_HANDLE;
    uint32_t query_count=2;
    std::vector<uint32_t> compute_queries;
    std::vector<DrawQuery> draw_queries;
    bool timing_collected=false;
};
struct Device {
    VkInstance instance=VK_NULL_HANDLE;
    VkPhysicalDevice physical=VK_NULL_HANDLE;
    VkDevice handle=VK_NULL_HANDLE;
    VkQueue queue=VK_NULL_HANDLE;
    uint32_t family=0;
    VkPhysicalDeviceProperties properties={};
    VkPhysicalDeviceFeatures features={};
    VkPhysicalDeviceMemoryProperties memory={};
    VkCommandPool commands=VK_NULL_HANDLE;
    VkCommandBuffer command=VK_NULL_HANDLE;
    VkFence fence=VK_NULL_HANDLE;
    VkDescriptorPool descriptors=VK_NULL_HANDLE;
    VkPipelineCache pipelines=VK_NULL_HANDLE;
    FrameState frames[2];
    uint32_t active_frame=0;
    bool recording=false;
    bool rendering=false;
    uint64_t render_passes=0;
    bool attachment_barriers=false; // bridge graphics only reads shader buffers
    uint64_t full_render_barriers=0,attachment_render_barriers=0;
    uint64_t generation=1,completed_generation=0,last_submitted=0;
    uint64_t fence_wait_ns=0,queue_submit_ns=0,submissions=0;
    bool gpu_timings=false;
    bool submission_trace=false;
    uint32_t timestamp_bits=0;
    uint64_t gpu_batch_ns=0,gpu_compute_ns=0,gpu_timed_batches=0,gpu_timed_dispatches=0;
    uint32_t gpu_draw_period=0;
    uint64_t gpu_draw_candidates=0,gpu_draw_skipped=0;
    std::map<DrawTimingKey,DrawTiming> gpu_draw_times;
    std::vector<uint64_t> render_key;
    std::string cache_path;
    std::string recipe_path; // empty disables recording (tests/preparation)
    Buffer read_only_zero_buffer;
    uint64_t pipeline_revision=0,saved_pipeline_revision=~0ull;
    uint64_t cache_save_ns=0,cache_saves=0;

    void init(const char *gpu_filter,const char *pipeline_cache);
    void close();
    Buffer buffer(VkDeviceSize size,VkBufferUsageFlags usage,bool host=true,bool cpu_readback=false);
    void destroy(Buffer &buffer);
    Image image(uint32_t w,uint32_t h,uint32_t d,uint32_t layers,uint32_t levels,
                VkFormat format,VkImageUsageFlags usage,VkImageViewType type=VK_IMAGE_VIEW_TYPE_2D,
                VkImageAspectFlags aspect=VK_IMAGE_ASPECT_COLOR_BIT);
    void destroy(Image &image);
    Slice upload(VkDeviceSize size,VkDeviceSize alignment=16);
    Slice read_only_zero(uint64_t bytes=4096); // must never be bound to a writable shader slot
    void transition(Image &image,VkImageLayout layout);
    void barrier();
    void render(const VkRenderingInfo &info);
    void end_rendering();
    void begin();
    void submit(VkSemaphore wait=VK_NULL_HANDLE,VkSemaphore signal=VK_NULL_HANDLE,bool wait_for_gpu=true);
    void create_frame(FrameState &frame);
    void activate_frame(uint32_t index);
    void wait_frame(FrameState &frame);
    void wait_idle();
    void trace(const char *kind,uint64_t a=0,uint64_t b=0,uint64_t c=0,uint64_t d=0,uint64_t e=0,uint64_t f=0);
    void dump_submission_trace(uint64_t waiting_serial,const char *reason="fence wait failure");
    uint32_t compute_timer_begin();
    void compute_timer_end(uint32_t first);
    uint32_t draw_timer_begin();
    void draw_timer_end(uint32_t first,DrawTimingKey key,uint32_t vertices,uint64_t pixels);
    VkSemaphore acquire_semaphore(){return frames[active_frame].acquired;}
    VkDescriptorSet descriptor_set(VkDescriptorSetLayout layout);
    void save_cache();
};

struct Binding { uint32_t binding; VkDescriptorType type; };
struct Spirv { std::vector<uint32_t> words; std::vector<Binding> bindings; std::string source; };
void prune_unused_bindings(Spirv &code,int stage);
// stage: 0 VS, 1 PS, 2 CS. Generated HLSL registers are mapped explicitly,
// so no D3D runtime, DXBC bytecode, or DXVK translation is involved.
Spirv compile(const char *hlsl,int stage,const char *name);
bool fast_vertex_fetch(std::string &source);
Spirv prepare_fast_vertex(const Spirv &original);
}
