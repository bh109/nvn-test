#pragma once
#include "native_pipeline.h"
namespace nativevk {
// First implementation deliberately excludes instancing, packed conversions,
// unaligned fields and unknown/OOB indices. Preserve the raw decoder there.
inline VertexInput float_vertex_input(uint32_t fmt,uint32_t stride,bool enabled,
    bool instanced,bool bounds_known,uint64_t last,uint64_t bytes){
    VertexInput v;uint32_t size=(fmt>>21)&63,offset=(fmt>>7)&0x3fff;
    uint32_t n=size==1?4:size==2?3:size==4?2:size==18?1:0;
    if(!enabled||(fmt&0x40)||instanced||!bounds_known||((fmt>>27)&7)!=7||!n||((stride|offset)&3))return v;
    uint64_t tail=(uint64_t)offset+n*4;
    if(tail>bytes||(stride&&last>(bytes-tail)/stride))return v;
    const VkFormat formats[]={VK_FORMAT_UNDEFINED,VK_FORMAT_R32_SFLOAT,VK_FORMAT_R32G32_SFLOAT,VK_FORMAT_R32G32B32_SFLOAT,VK_FORMAT_R32G32B32A32_SFLOAT};
    return {formats[n],stride,offset};
}
// Rewrite only the translator's known interface, atomically. Unconverted
// attributes retain their buffers/functions, and all built-in IDs stay intact.
bool hardware_vertex_fetch(std::string &source,uint32_t mask);
}
