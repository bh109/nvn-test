#include "native_maxwell.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace nativevk {
static float floating(uint32_t bits){float value;memcpy(&value,&bits,4);return value;}
static uint32_t bits(float value){uint32_t result;memcpy(&result,&value,4);return result;}
VkFormat color_format(uint32_t format){switch(format){
case 0xc0:case 0xc3:return VK_FORMAT_R32G32B32A32_SFLOAT;
case 0xc1:case 0xc4:return VK_FORMAT_R32G32B32A32_SINT;
case 0xc2:case 0xc5:return VK_FORMAT_R32G32B32A32_UINT;
case 0xc6:return VK_FORMAT_R16G16B16A16_UNORM;case 0xc7:return VK_FORMAT_R16G16B16A16_SNORM;
case 0xc8:return VK_FORMAT_R16G16B16A16_SINT;case 0xc9:return VK_FORMAT_R16G16B16A16_UINT;
case 0xca:case 0xce:return VK_FORMAT_R16G16B16A16_SFLOAT;
case 0xcb:return VK_FORMAT_R32G32_SFLOAT;case 0xcc:return VK_FORMAT_R32G32_SINT;case 0xcd:return VK_FORMAT_R32G32_UINT;
case 0xcf:case 0xe6:return VK_FORMAT_B8G8R8A8_UNORM;case 0xd0:case 0xe7:return VK_FORMAT_B8G8R8A8_SRGB;
case 0xd1:return VK_FORMAT_A2B10G10R10_UNORM_PACK32;case 0xd2:return VK_FORMAT_A2B10G10R10_UINT_PACK32;
case 0xd5:case 0xf9:return VK_FORMAT_R8G8B8A8_UNORM;case 0xd6:case 0xfa:return VK_FORMAT_R8G8B8A8_SRGB;
case 0xd7:return VK_FORMAT_R8G8B8A8_SNORM;case 0xd8:return VK_FORMAT_R8G8B8A8_SINT;case 0xd9:return VK_FORMAT_R8G8B8A8_UINT;
case 0xda:return VK_FORMAT_R16G16_UNORM;case 0xdb:return VK_FORMAT_R16G16_SNORM;case 0xdc:return VK_FORMAT_R16G16_SINT;case 0xdd:return VK_FORMAT_R16G16_UINT;case 0xde:return VK_FORMAT_R16G16_SFLOAT;
case 0xe0:return VK_FORMAT_B10G11R11_UFLOAT_PACK32;case 0xe3:return VK_FORMAT_R32_SINT;case 0xe4:return VK_FORMAT_R32_UINT;case 0xe5:return VK_FORMAT_R32_SFLOAT;
case 0xe8:return VK_FORMAT_R5G6B5_UNORM_PACK16;case 0xe9:case 0xf8:return VK_FORMAT_A1R5G5B5_UNORM_PACK16;
case 0xea:return VK_FORMAT_R8G8_UNORM;case 0xeb:return VK_FORMAT_R8G8_SNORM;case 0xec:return VK_FORMAT_R8G8_SINT;case 0xed:return VK_FORMAT_R8G8_UINT;
case 0xee:return VK_FORMAT_R16_UNORM;case 0xef:return VK_FORMAT_R16_SNORM;case 0xf0:return VK_FORMAT_R16_SINT;case 0xf1:return VK_FORMAT_R16_UINT;case 0xf2:return VK_FORMAT_R16_SFLOAT;
case 0xf3:return VK_FORMAT_R8_UNORM;case 0xf4:return VK_FORMAT_R8_SNORM;case 0xf5:return VK_FORMAT_R8_SINT;case 0xf6:return VK_FORMAT_R8_UINT;
default:throw std::runtime_error("Unsupported Maxwell color format "+std::to_string(format));}}
VkFormat depth_format(uint32_t format){switch(format){case 0xa:return VK_FORMAT_D32_SFLOAT;case 0x13:return VK_FORMAT_D16_UNORM;case 0x14:case 0x15:case 0x16:return VK_FORMAT_D24_UNORM_S8_UINT;case 0x19:return VK_FORMAT_D32_SFLOAT_S8_UINT;default:throw std::runtime_error("Unsupported Maxwell depth format "+std::to_string(format));}}
VkCompareOp comparison(uint32_t f){if(f>=0x200&&f<=0x207)return (VkCompareOp)(f-0x200);if(f>=1&&f<=8)return (VkCompareOp)(f-1);return VK_COMPARE_OP_ALWAYS;}
static VkStencilOp stencil(uint32_t op){switch(op){case 2:case 0:return VK_STENCIL_OP_ZERO;case 3:case 0x1e01:return VK_STENCIL_OP_REPLACE;case 4:case 0x1e02:return VK_STENCIL_OP_INCREMENT_AND_CLAMP;case 5:case 0x1e03:return VK_STENCIL_OP_DECREMENT_AND_CLAMP;case 6:case 0x150a:return VK_STENCIL_OP_INVERT;case 7:case 0x8507:return VK_STENCIL_OP_INCREMENT_AND_WRAP;case 8:case 0x8508:return VK_STENCIL_OP_DECREMENT_AND_WRAP;default:return VK_STENCIL_OP_KEEP;}}
static VkBlendFactor factor(uint32_t f){switch(f){
case 1:case 0x4000:return VK_BLEND_FACTOR_ZERO;case 2:case 0x4001:return VK_BLEND_FACTOR_ONE;
case 3:case 0x4300:return VK_BLEND_FACTOR_SRC_COLOR;case 4:case 0x4301:return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
case 5:case 0x4302:return VK_BLEND_FACTOR_SRC_ALPHA;case 6:case 0x4303:return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
case 7:case 0x4304:return VK_BLEND_FACTOR_DST_ALPHA;case 8:case 0x4305:return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
case 9:case 0x4306:return VK_BLEND_FACTOR_DST_COLOR;case 10:case 0x4307:return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
case 11:case 0x4308:return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
case 14:case 0xc001:return VK_BLEND_FACTOR_CONSTANT_COLOR;case 15:case 0xc002:return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
case 0xc003:return VK_BLEND_FACTOR_CONSTANT_ALPHA;case 0xc004:return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
case 16:case 0xc900:return VK_BLEND_FACTOR_SRC1_COLOR;case 17:case 0xc901:return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
case 18:case 0xc902:return VK_BLEND_FACTOR_SRC1_ALPHA;case 19:case 0xc903:return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
default:return VK_BLEND_FACTOR_ONE;}}
static VkBlendOp operation(uint32_t op){switch(op){case 2:case 0x800a:return VK_BLEND_OP_SUBTRACT;case 3:case 0x800b:return VK_BLEND_OP_REVERSE_SUBTRACT;case 4:case 0x8007:return VK_BLEND_OP_MIN;case 5:case 0x8008:return VK_BLEND_OP_MAX;default:return VK_BLEND_OP_ADD;}}
DrawState draw_state(const uint32_t *r,uint32_t width,uint32_t height,float scale_x,float scale_y,uint32_t instance){
    if(!width||!height||!(scale_x>0)||!(scale_y>0))throw std::runtime_error("Invalid Maxwell render target dimensions");
    DrawState out;auto &p=out.pipeline;auto &params=out.params;
    switch(r[M_BEGIN]&0xffff){case 0:p.topology=VK_PRIMITIVE_TOPOLOGY_POINT_LIST;break;case 1:p.topology=VK_PRIMITIVE_TOPOLOGY_LINE_LIST;break;case 3:p.topology=VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;break;case 4:p.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;break;case 5:p.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;break;case 6:p.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;break;default:throw std::runtime_error("Unsupported Maxwell primitive topology");}
    p.primitive_restart=(r[M_PRIMITIVE_RESTART]&1)&&(p.topology==VK_PRIMITIVE_TOPOLOGY_LINE_STRIP||p.topology==VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP||p.topology==VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN);
    float sx=floating(r[M_VIEWPORT_SCALE(0)]),sy=floating(r[M_VIEWPORT_SCALE(0)+1]),sz=floating(r[M_VIEWPORT_SCALE(0)+2]);
    float ox=floating(r[M_VIEWPORT_SCALE(0)+3]),oy=floating(r[M_VIEWPORT_SCALE(0)+4]),oz=floating(r[M_VIEWPORT_SCALE(0)+5]);
    bool lower=r[M_WINDOW_ORIGIN]&1,minus_one=!(r[M_VIEWPORT_Z_CLIP]&1);if(lower){oy=height-oy;sy=-sy;}
    float x=(ox-fabsf(sx))*scale_x,y=(oy-fabsf(sy))*scale_y,w=2*fabsf(sx)*scale_x,h=2*fabsf(sy)*scale_y;
    if(w<1){x=0;w=width*scale_x;}if(h<1){y=0;h=height*scale_y;}
    float zmin=std::clamp(minus_one?oz-sz:oz,0.f,1.f),zmax=std::clamp(oz+sz,0.f,1.f);
    // Negative height retains the existing translator's D3D clip-space fixup.
    out.viewport={x,y+h,w,-h,zmin,zmax};
    params.fix[0]=sx<0?-1.f:1.f;params.fix[1]=sy<0?1.f:-1.f;params.fix[2]=minus_one?.5f:1.f;params.fix[3]=minus_one?.5f:0.f;
    params.frag[0]=lower?1.f:0.f;params.frag[1]=(float)height;params.base[0]=r[M_GLOBAL_BASE_VERTEX];params.base[1]=r[M_GLOBAL_BASE_INSTANCE]+instance;params.base[2]=bits(1.f/scale_x);params.base[3]=bits(1.f/scale_y);
    int left=0,top=0,right=(int)ceilf(width*scale_x),bottom=(int)ceilf(height*scale_y);
    if(r[M_SCISSOR(0)]&1){uint32_t horizontal=r[M_SCISSOR(0)+1],vertical=r[M_SCISSOR(0)+2];left=horizontal&0xffff;right=horizontal>>16;top=vertical&0xffff;bottom=vertical>>16;if(lower){int t=height-bottom;bottom=height-top;top=t;}left=(int)floorf(left*scale_x);top=(int)floorf(top*scale_y);right=(int)ceilf(right*scale_x);bottom=(int)ceilf(bottom*scale_y);}
    left=std::clamp(left,0,(int)ceilf(width*scale_x));right=std::clamp(right,left,(int)ceilf(width*scale_x));top=std::clamp(top,0,(int)ceilf(height*scale_y));bottom=std::clamp(bottom,top,(int)ceilf(height*scale_y));out.scissor={{left,top},{(uint32_t)(right-left),(uint32_t)(bottom-top)}};
    p.fill=r[M_FILL_MODE]==0x1b01?VK_POLYGON_MODE_LINE:VK_POLYGON_MODE_FILL;if(r[M_CULL_ENABLE]&1)p.cull=r[M_CULL_FACE]==0x404?VK_CULL_MODE_FRONT_BIT:r[M_CULL_FACE]==0x405?VK_CULL_MODE_BACK_BIT:VK_CULL_MODE_NONE;
    bool flipped=(!lower)!=((r[M_WINDOW_ORIGIN]>>4)&1);p.front=((r[M_FRONT_FACE]==0x901)!=flipped)?VK_FRONT_FACE_COUNTER_CLOCKWISE:VK_FRONT_FACE_CLOCKWISE;
    p.depth_test=r[M_DEPTH_TEST]&1;p.depth_write=r[M_DEPTH_WRITE]&1;p.depth_compare=comparison(r[M_DEPTH_FUNC]);p.stencil_test=r[M_STENCIL_TEST]&1;
    p.stencil_front={stencil(r[M_STENCIL_OP_FAIL]),stencil(r[M_STENCIL_OP_ZPASS]),stencil(r[M_STENCIL_OP_ZFAIL]),comparison(r[M_STENCIL_FUNC]),r[M_STENCIL_FUNC_MASK]&255,r[M_STENCIL_MASK]&255,0};p.stencil_back=p.stencil_front;
    out.stencil_front=r[M_STENCIL_REF]&255;out.stencil_back=out.stencil_front;
    if(r[M_TWO_SIDED_STENCIL]&1){p.stencil_back={stencil(r[M_BACK_STENCIL_OP_FAIL]),stencil(r[M_BACK_STENCIL_OP_ZPASS]),stencil(r[M_BACK_STENCIL_OP_ZFAIL]),comparison(r[M_BACK_STENCIL_FUNC]),r[M_BACK_STENCIL_FUNC_MASK]&255,r[M_BACK_STENCIL_MASK]&255,0};out.stencil_back=r[M_BACK_STENCIL_REF]&255;}
    for(uint32_t i=0;i<8;i++){auto &b=p.blends[i];uint32_t mask=r[M_CT_WRITE(i)];b.colorWriteMask=(mask&1?1:0)|(mask&0x10?2:0)|(mask&0x100?4:0)|(mask&0x1000?8:0);b.blendEnable=r[M_BLEND_ENABLE(i)]&1;uint32_t base=r[M_BLEND_STATE_PER_TARGET]&1?M_BLEND_PER_TARGET(i)+1:0;b.colorBlendOp=operation(r[base?base:M_BLEND_COLOR_OP]);b.srcColorBlendFactor=factor(r[base?base+1:M_BLEND_COLOR_SRC]);b.dstColorBlendFactor=factor(r[base?base+2:M_BLEND_COLOR_DST]);b.alphaBlendOp=operation(r[base?base+3:M_BLEND_ALPHA_OP]);b.srcAlphaBlendFactor=factor(r[base?base+4:M_BLEND_ALPHA_SRC]);b.dstAlphaBlendFactor=factor(r[base?base+5:M_BLEND_ALPHA_DST]);}
    for(int i=0;i<4;i++)out.blend[i]=floating(r[M_BLEND_CONST(i)]);
    if(r[M_ALPHA_TEST]&1){uint32_t function=(uint32_t)comparison(r[M_ALPHA_FUNC])+1;params.frag[2]=floating((function-1)|((function==6||function==8)?8:0));params.frag[3]=floating(r[M_ALPHA_REF]);}
    return out;
}
}
