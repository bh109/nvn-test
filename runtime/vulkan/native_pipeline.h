#pragma once
#include "native_device.h"
#include <array>

namespace nativevk {
// Hardware inputs may coexist with the Maxwell storage-buffer decoder. Each
// active location uses the same numbered binding; zero format means fallback.
struct VertexInput {VkFormat format=VK_FORMAT_UNDEFINED;uint32_t stride=0,offset=0;};
struct GraphicsState {
    std::array<VertexInput,32> vertices={};
    std::array<VkFormat,8> colors={};
    uint32_t color_count=0;
    VkFormat depth=VK_FORMAT_UNDEFINED,stencil=VK_FORMAT_UNDEFINED;
    VkPrimitiveTopology topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    bool primitive_restart=false;
    VkPolygonMode fill=VK_POLYGON_MODE_FILL;
    VkCullModeFlags cull=VK_CULL_MODE_NONE;
    VkFrontFace front=VK_FRONT_FACE_COUNTER_CLOCKWISE;
    bool depth_test=false,depth_write=false,stencil_test=false;
    VkCompareOp depth_compare=VK_COMPARE_OP_ALWAYS;
    VkStencilOpState stencil_front={},stencil_back={};
    std::array<VkPipelineColorBlendAttachmentState,8> blends={};
    GraphicsState(){for(auto &blend:blends)blend.colorWriteMask=15;}
};

// Fixed-size, padding-free cache identity. Disabled state must not create
// additional pipelines, while formats, write masks and enabled operations do.
using GraphicsKey=std::array<uint32_t,192>;
inline GraphicsKey graphics_key(const GraphicsState &s){
    GraphicsKey key={};size_t at=0;auto put=[&](uint32_t v){key[at++]=v;};
    put(s.topology);put(s.primitive_restart);put(s.fill);put(s.cull);put(s.front);
    put(s.depth_test);put(s.depth_test&&s.depth_write);put(s.stencil_test);
    put(s.depth_test?s.depth_compare:VK_COMPARE_OP_ALWAYS);put(s.depth);put(s.stencil);put(s.color_count);
    for(size_t i=0;i<8;i++)put(i<s.color_count?s.colors[i]:VK_FORMAT_UNDEFINED);
    for(size_t i=0;i<8;i++){
        auto b=i<s.color_count?s.blends[i]:VkPipelineColorBlendAttachmentState{};
        put(b.blendEnable);put(b.blendEnable?b.srcColorBlendFactor:0);put(b.blendEnable?b.dstColorBlendFactor:0);put(b.blendEnable?b.colorBlendOp:0);
        put(b.blendEnable?b.srcAlphaBlendFactor:0);put(b.blendEnable?b.dstAlphaBlendFactor:0);put(b.blendEnable?b.alphaBlendOp:0);put(b.colorWriteMask);
    }
    for(auto face:{s.stencil_front,s.stencil_back}){
        put(s.stencil_test?face.failOp:0);put(s.stencil_test?face.passOp:0);put(s.stencil_test?face.depthFailOp:0);
        put(s.stencil_test?face.compareOp:0);put(s.stencil_test?face.compareMask:0);put(s.stencil_test?face.writeMask:0);
    }
    for(auto v:s.vertices){put(v.format);put(v.format?v.stride:0);put(v.format?v.offset:0);}
    return key;
}

// Owns the shader modules and descriptor layouts. Sets follow the translator's
// stage convention: VS/CS = 0, PS = 1. Pipelines share these layouts.
struct Program {
    Device *device=nullptr;
    VkShaderModule vertex=VK_NULL_HANDLE,pixel=VK_NULL_HANDLE,compute=VK_NULL_HANDLE;
    VkDescriptorSetLayout sets[2]={};
    VkPipelineLayout layout=VK_NULL_HANDLE;
    std::vector<Binding> bindings[2];
    uint64_t recipe_id=0;
    Program()=default;
    Program(const Program &)=delete;
    Program &operator=(const Program &)=delete;
    void init_graphics(Device &device,const Spirv &vs,const Spirv *ps);
    void init_compute(Device &device,const Spirv &cs);
    VkPipeline graphics(const GraphicsState &state);
    VkPipeline dispatch();
    void close();
private:
    void create_layout(uint32_t count,const VkShaderStageFlags *stages);
};
}
