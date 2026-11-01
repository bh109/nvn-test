#pragma once
#include "native_pipeline.h"
#include "../maxwell3d.h"
namespace nativevk {
// Exact constant-buffer layout emitted by gpu_shader.c. Vulkan descriptors
// replace D3D registers; the translated shader's data layout stays unchanged.
struct DrawParams {
    uint32_t attr[32][4]={};
    float fix[4]={},frag[4]={};
    uint32_t base[4]={},swz[2][32]={},sb[2][16][4]={};
};
static_assert(sizeof(DrawParams)==1328,"Maxwell translator parameter layout");
struct DrawState {
    GraphicsState pipeline;
    DrawParams params;
    VkViewport viewport={};
    VkRect2D scissor={};
    float blend[4]={};
    uint32_t stencil_front=0,stencil_back=0;
};
VkFormat color_format(uint32_t format);
VkFormat depth_format(uint32_t format);
VkCompareOp comparison(uint32_t function);
inline bool native_indices(uint32_t format,bool restart,uint32_t restart_index){
    if(format!=1&&format!=2)return false; // byte indices still expand to uint16
    uint32_t mask=format==1?0xffffu:0xffffffffu;
    return !restart||(restart_index&mask)==mask;
}
inline uint64_t vertex_read_span(uint64_t available,uint32_t format,uint32_t stride,bool known,uint64_t last_vertex){
    // decode_attr reads Load4(aligned address) plus one extra word, even for
    // smaller formats. Keep that complete 20-byte read footprint in the view.
    if(format&0x40)return std::min<uint64_t>(available,16);
    if(!known||last_vertex>0xffffffffull)return available;
    uint64_t address=last_vertex*stride+((format>>7)&0x3fff);
    if(address>0xffffffffull-19)return available; // preserve uint address wrap
    return std::min<uint64_t>(available,(address&~3ull)+20);
}
DrawState draw_state(const uint32_t *regs,uint32_t width,uint32_t height,
                     float scale_x,float scale_y,uint32_t instance);
}
