#include "native_recipes.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
namespace nativevk {
namespace {
constexpr uint32_t program_magic=0x3150564e,pipeline_magic=0x3152564e,version=1;
uint64_t digest(const std::vector<uint32_t> &words){uint64_t h=1469598103934665603ull;for(auto w:words)for(unsigned i=0;i<4;i++)h=(h^((w>>(i*8))&255))*1099511628211ull;return h;}
std::string filename(const std::string &directory,uint64_t id,const char *suffix){char name[80];snprintf(name,sizeof name,"/%016llx.%s",(unsigned long long)id,suffix);return directory+name;}
uint64_t store(const std::string &directory,std::vector<uint32_t> body,const char *suffix){
    if(directory.empty())return 0;uint64_t id=digest(body);auto path=filename(directory,id,suffix);
    if(GetFileAttributesA(path.c_str())!=INVALID_FILE_ATTRIBUTES)return id;
    CreateDirectoryA(directory.c_str(),nullptr);body.push_back((uint32_t)id);body.push_back((uint32_t)(id>>32));
    auto temp=path+"."+std::to_string(GetCurrentProcessId())+".tmp";FILE *f=fopen(temp.c_str(),"wb");if(!f)return 0;
    bool ok=fwrite(body.data(),4,body.size(),f)==body.size();if(fclose(f))ok=false;
    if(ok)ok=MoveFileExA(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING)!=0;
    if(!ok){DeleteFileA(temp.c_str());fprintf(stderr,"[native recipes] could not save %s\n",path.c_str());return 0;}return id;
}
bool load(const std::string &path,std::vector<uint32_t> &body,uint64_t &id){
    FILE *f=fopen(path.c_str(),"rb");if(!f)return false;fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
    bool ok=size>=24&&size<=16*1024*1024&&!(size&3);if(ok){body.resize(size/4);ok=fread(body.data(),1,size,f)==(size_t)size;}fclose(f);if(!ok)return false;
    id=(uint64_t)body[body.size()-1]<<32|body[body.size()-2];body.resize(body.size()-2);return digest(body)==id;
}
void append(std::vector<uint32_t> &out,const Spirv &s){out.push_back((uint32_t)s.words.size());out.push_back((uint32_t)s.bindings.size());out.insert(out.end(),s.words.begin(),s.words.end());for(auto b:s.bindings){out.push_back(b.binding);out.push_back(b.type);}}
bool take(const std::vector<uint32_t> &v,size_t &at,Spirv &out){
    if(v.size()-at<2)return false;uint32_t n=v[at++],b=v[at++];if(n>1024*1024||b>1024||n+2ull*b>v.size()-at)return false;
    if(n&&(n<5||v[at]!=0x07230203))return false;out.words.assign(v.begin()+at,v.begin()+at+n);at+=n;
    for(uint32_t i=0;i<b;i++){uint32_t binding=v[at++],type=v[at++];if(binding>4096||(type!=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER&&type!=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER&&type!=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE&&type!=VK_DESCRIPTOR_TYPE_SAMPLER))return false;out.bindings.push_back({binding,(VkDescriptorType)type});}return true;
}
}
std::string recipe_program_path(const std::string &directory,uint64_t id){return filename(directory,id,"nvprog");}
uint64_t record_program(const std::string &directory,bool compute,const Spirv &first,const Spirv *second){if(directory.empty())return 0;std::vector<uint32_t> body={program_magic,version,compute?1u:0u};append(body,first);append(body,second?*second:Spirv{});return store(directory,std::move(body),"nvprog");}
void record_pipeline(const std::string &directory,uint64_t program,const GraphicsState *state){if(directory.empty()||!program)return;std::vector<uint32_t> body={pipeline_magic,version,state?0u:1u,(uint32_t)program,(uint32_t)(program>>32)};if(state){auto key=graphics_key(*state);bool hardware=false;for(auto v:state->vertices)hardware|=v.format!=VK_FORMAT_UNDEFINED;body.insert(body.end(),key.begin(),hardware?key.end():key.begin()+96);}store(directory,std::move(body),"nvpipe");}
bool read_program_recipe(const std::string &path,RecipeProgram &result){
    std::vector<uint32_t> body;uint64_t id;if(!load(path,body,id)||body.size()<7||body[0]!=program_magic||body[1]!=version||body[2]>1)return false;
    RecipeProgram r;r.id=id;r.compute=body[2]!=0;size_t at=3;if(!take(body,at,r.first)||!take(body,at,r.second)||at!=body.size()||r.first.words.empty()||(r.compute&&!r.second.words.empty()))return false;result=std::move(r);return true;
}
bool read_pipeline_recipe(const std::string &path,RecipePipeline &result){
    std::vector<uint32_t> body;uint64_t id;if(!load(path,body,id)||body.size()<5||body[0]!=pipeline_magic||body[1]!=version||body[2]>1)return false;
    RecipePipeline r;r.compute=body[2]!=0;r.program=(uint64_t)body[4]<<32|body[3];if(!r.program)return false;
    // Legacy recipes have 96 state words and imply no hardware inputs.
    if(r.compute?body.size()!=5:(body.size()!=101&&body.size()!=5+GraphicsKey{}.size()))return false;
    if(!r.compute){std::copy(body.begin()+5,body.end(),r.key.begin());if(r.key[11]>8)return false;auto s=graphics_state(r.key);if(graphics_key(s)!=r.key)return false;}
    result=r;return true;
}
GraphicsState graphics_state(const GraphicsKey &key){
    GraphicsState s;size_t at=0;auto get=[&](){return key[at++];};s.topology=(VkPrimitiveTopology)get();s.primitive_restart=get()!=0;s.fill=(VkPolygonMode)get();s.cull=get();s.front=(VkFrontFace)get();
    s.depth_test=get()!=0;s.depth_write=get()!=0;s.stencil_test=get()!=0;s.depth_compare=(VkCompareOp)get();s.depth=(VkFormat)get();s.stencil=(VkFormat)get();s.color_count=get();
    for(auto &f:s.colors)f=(VkFormat)get();for(auto &b:s.blends){b.blendEnable=get();b.srcColorBlendFactor=(VkBlendFactor)get();b.dstColorBlendFactor=(VkBlendFactor)get();b.colorBlendOp=(VkBlendOp)get();b.srcAlphaBlendFactor=(VkBlendFactor)get();b.dstAlphaBlendFactor=(VkBlendFactor)get();b.alphaBlendOp=(VkBlendOp)get();b.colorWriteMask=get();}
    for(auto f:{&s.stencil_front,&s.stencil_back}){f->failOp=(VkStencilOp)get();f->passOp=(VkStencilOp)get();f->depthFailOp=(VkStencilOp)get();f->compareOp=(VkCompareOp)get();f->compareMask=get();f->writeMask=get();}
    for(auto &v:s.vertices){v.format=(VkFormat)get();v.stride=get();v.offset=get();}return s;
}
}
