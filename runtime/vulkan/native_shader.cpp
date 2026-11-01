#include "native_device.h"
#include "native_vertex_input.h"
#include <dxcapi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <regex>
#include <map>
#include <set>
#include <algorithm>
#include <stdexcept>

namespace nativevk {
WorkStats work_stats;
void prune_unused_bindings(Spirv &code,int stage){
    LARGE_INTEGER start;QueryPerformanceCounter(&start);work_stats.declared_bindings+=code.bindings.size();
    auto keep=[&](){work_stats.active_bindings+=code.bindings.size();work_stats.interface_ns+=elapsed_ns(start);};
    const auto &v=code.words;if(v.size()<5||v[0]!=0x07230203){keep();return;}
    struct Resource {uint32_t binding=~0u,set=~0u;bool used=false;};std::map<uint32_t,Resource> resources;
    // Read direct Binding/DescriptorSet decorations. Group decorations are not
    // emitted by the pinned DXC; retain all slots if an unfamiliar form appears.
    for(size_t at=5;at<v.size();){uint32_t n=v[at]>>16,op=v[at]&65535;if(!n||n>v.size()-at||op==73||op==74||op==75){keep();return;}
        if(op==71&&n>=4){if(v[at+2]==33)resources[v[at+1]].binding=v[at+3];else if(v[at+2]==34)resources[v[at+1]].set=v[at+3];}at+=n;}
    bool function=false;
    for(size_t at=5;at<v.size();){uint32_t n=v[at]>>16,op=v[at]&65535;if(op==54)function=true;
        // All function bodies are scanned, not only main. Treat every operand
        // equal to a resource ID as a use, including possible literal matches:
        // that can keep an unnecessary slot, but cannot drop a referenced one.
        if(function)for(uint32_t i=1;i<n;i++){auto r=resources.find(v[at+i]);if(r!=resources.end())r->second.used=true;}
        if(op==56)function=false;at+=n;}
    uint32_t set=stage==1?1:0;std::set<uint32_t> used;
    for(auto &r:resources)if(r.second.used&&(r.second.set==set||r.second.set==~0u))used.insert(r.second.binding);
    code.bindings.erase(std::remove_if(code.bindings.begin(),code.bindings.end(),[&](Binding b){return !used.count(b.binding);}),code.bindings.end());keep();
}
static std::string stable_temporaries(const std::string &source){
    // HLSL's translator numbers tN temporaries across programs. Their names
    // depend on translation order, despite generating identical shader code.
    // Alpha-rename identifiers, preserving register slots, strings/comments.
    static const std::regex token(R"token(//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\bregister\s*\([^)]*\)|\bt[0-9]+\b)token");
    std::map<std::string,std::string> names;std::string stable;size_t last=0;
    for(std::sregex_iterator i(source.begin(),source.end(),token),end;i!=end;++i){auto &m=*i;std::string word=m.str();stable+=source.substr(last,m.position()-last);
        if(word[0]=='t'){auto found=names.find(word);if(found==names.end())found=names.emplace(word,"t"+std::to_string(names.size())).first;stable+=found->second;}else stable+=word;
        last=m.position()+m.length();
    }
    stable+=source.substr(last);return stable;
}
static std::string cache_file(const std::string &source,int stage){
    if(getenv("SSBU_VK_NO_SHADER_CACHE"))return {};
    char executable[32768];DWORD length=GetModuleFileNameA(nullptr,executable,sizeof executable);if(!length||length>=sizeof executable)return {};
    std::string directory(executable,length);auto slash=directory.find_last_of("/\\");if(slash==std::string::npos)return {};directory.resize(slash);directory+="\\shader-cache";CreateDirectoryA(directory.c_str(),nullptr);
    // This salt identifies the pinned DXC build, Vulkan target, compilation
    // flags and binding/layout convention. Changing any of them changes cache.
    const char *salt="native-spv-v1-dxc1.8.2505.1-vk1.2-dxlayout-strict-O3";uint64_t hash=1469598103934665603ull;
    for(auto p=salt;*p;p++)hash=(hash^(uint8_t)*p)*1099511628211ull;
    hash=(hash^(uint32_t)stage)*1099511628211ull;for(unsigned char c:source)hash=(hash^c)*1099511628211ull;
    char filename[64];snprintf(filename,sizeof filename,"\\%016llx.spv",(unsigned long long)hash);return directory+filename;
}
static bool read_cache(const std::string &path,std::vector<uint32_t> &words){
    if(path.empty())return false;FILE *file=fopen(path.c_str(),"rb");if(!file)return false;fseek(file,0,SEEK_END);long size=ftell(file);rewind(file);bool valid=size>=20&&size<=16*1024*1024&&!(size&3);if(valid){words.resize(size/4);valid=fread(words.data(),1,size,file)==(size_t)size&&words[0]==0x07230203;}fclose(file);if(!valid)words.clear();return valid;
}
static void write_cache(const std::string &path,const std::vector<uint32_t> &words){
    if(path.empty())return;std::string temporary=path+"."+std::to_string(GetCurrentProcessId())+".tmp";FILE *file=fopen(temporary.c_str(),"wb");if(!file)return;bool written=fwrite(words.data(),4,words.size(),file)==words.size();if(fclose(file))written=false;if(written)MoveFileExA(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING);else DeleteFileA(temporary.c_str());
}
static void capture_input(const std::string &cache,const char *hlsl,int stage){
    if(cache.empty()||!getenv("SSBU_VK_CAPTURE_SHADERS"))return;
    auto slash=cache.find_last_of("/\\");auto parent=cache.substr(0,slash).find_last_of("/\\");
    if(slash==std::string::npos||parent==std::string::npos)return;
    std::string directory=cache.substr(0,parent)+"\\shader-inputs";CreateDirectoryA(directory.c_str(),nullptr);
    std::string id=cache.substr(slash+1);id.resize(id.size()-4);
    std::string path=directory+"\\"+(stage==0?"vs_":stage==1?"ps_":"cs_")+id+".hlsl";
    HANDLE file=CreateFileA(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return;DWORD written=0;size_t bytes=strlen(hlsl);
    bool ok=WriteFile(file,hlsl,(DWORD)bytes,&written,nullptr)&&written==bytes;CloseHandle(file);if(!ok)DeleteFileA(path.c_str());
}
bool fast_vertex_fetch(std::string &source){
    const char *signature="uint4 decode_attr(ByteAddressBuffer b, uint addr, uint fmt) {\n";
    auto at=source.find(signature);if(at==std::string::npos||source.find("// native aligned float fetch")!=std::string::npos)return false;
    at+=strlen(signature);
    source.insert(at,
        "  // native aligned float fetch; other formats use the reference decoder\n"
        "  uint fast_size = (fmt >> 21) & 0x3Fu;\n"
        "  [branch] if (((fmt >> 27) & 7u) == 7u && (addr & 3u) == 0u && (fast_size == 1u || fast_size == 2u || fast_size == 4u || fast_size == 0x12u)) {\n"
        "    uint4 fast_value = uint4(0u,0u,0u,0x3F800000u);\n"
        "    [branch] if (fast_size == 1u) fast_value = b.Load4(addr);\n"
        "    else if (fast_size == 2u) fast_value.xyz = b.Load3(addr);\n"
        "    else if (fast_size == 4u) fast_value.xy = b.Load2(addr);\n"
        "    else fast_value.x = b.Load(addr);\n"
        "    return (fmt >> 31) != 0u ? fast_value.zyxw : fast_value;\n"
        "  }\n");return true;
}
bool hardware_vertex_fetch(std::string &source,uint32_t mask){
    if(!mask)return false;
    std::string candidate=std::regex_replace(source,std::regex(R"(\[\[vk::(?:binding|location)\([0-9,]+\)\]\] )"),""),fields;
    const std::string signature="VSOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID) {";
    if(candidate.find(signature)==std::string::npos)return false;
    for(uint32_t n=0;n<32;n++)if(mask>>n&1){
        auto id=std::to_string(n);
        std::string declaration="ByteAddressBuffer vb"+id+" : register(t"+std::to_string(18+n)+");\n";
        auto at=candidate.find(declaration);if(at==std::string::npos)return false;candidate.erase(at,declaration.size());
        at=candidate.find("uint4 fetch"+id+"(uint vid, uint iid) {\n");if(at==std::string::npos)return false;
        auto end=candidate.find("\n}\n",at);if(end==std::string::npos)return false;candidate.erase(at,end+3-at);
        std::string fetch="  uint4 va"+id+" = fetch"+id+"(vid, iid);";
        at=candidate.find(fetch);if(at==std::string::npos)return false;
        candidate.replace(at,fetch.size(),"  uint4 va"+id+" = asuint(vi.a"+id+");\n  if ((params.attr["+id+"].x >> 31) != 0u) va"+id+" = va"+id+".zyxw;");
        fields+="  [[vk::location("+id+")]] float4 a"+id+" : FETCH"+id+";\n";
    }
    auto at=candidate.find(signature);candidate.replace(at,signature.size(),"struct NativeVertexInput {\n"+fields+"};\nVSOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID, NativeVertexInput vi) {");
    source=std::move(candidate);return true;
}
Spirv prepare_fast_vertex(const Spirv &original){
    std::string source=original.source;if(!fast_vertex_fetch(source))return original;
    // Recipes store the compiler's explicitly annotated source. Remove only
    // annotations emitted by this module so rebinding is byte-for-byte equal
    // to compiling the original guest translation with FastVertex enabled.
    source=std::regex_replace(source,std::regex(R"(\[\[vk::(?:binding|location)\([0-9,]+\)\]\] )"),"");
    return compile(source.c_str(),0,"prepared aligned-float vertex variant");
}
Spirv compile(const char *hlsl,int stage,const char *name) {
    if(!hlsl||stage<0||stage>2)throw std::runtime_error("Invalid Vulkan shader input or stage");
    LARGE_INTEGER rewrite_start;QueryPerformanceCounter(&rewrite_start);
    Spirv result;
    uint32_t set=stage==1?1:0;
    std::string source(hlsl),rewritten;
    if(stage==0&&getenv("SSBU_VK_FAST_VERTEX")&&strcmp(getenv("SSBU_VK_FAST_VERTEX"),"0")&&fast_vertex_fetch(source))work_stats.vertex_fast_programs++;
    // Keep resource assignments independent of DXC's register-class shifts.
    std::regex resource(R"((cbuffer\s+\w+|(?:RW)?ByteAddressBuffer\s+\w+|(?:RW)?StructuredBuffer<[^>]+>\s+\w+|Texture\w*<[^>]+>\s+\w+|Sampler(?:Comparison)?State\s+\w+)\s*:\s*register\(([btus])(\d+)\))");
    size_t last=0;
    for(std::sregex_iterator i(source.begin(),source.end(),resource),end;i!=end;++i){auto &m=*i;char cls=m[2].str()[0];uint32_t slot=(uint32_t)std::stoul(m[3].str());uint32_t binding=cls=='b'?slot:cls=='t'?1+slot:cls=='s'?160+slot:256+slot;
        VkDescriptorType type=cls=='b'?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:cls=='s'?VK_DESCRIPTOR_TYPE_SAMPLER:cls=='u'?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:m[1].str().find("Texture")==0?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        result.bindings.push_back({binding,type});rewritten+=source.substr(last,m.position()-last);rewritten+="[[vk::binding("+std::to_string(binding)+","+std::to_string(set)+")]] "+m.str();last=m.position()+m.length();}
    rewritten+=source.substr(last);source=std::move(rewritten);
    // Explicit locations survive unused varyings and eliminate interstage
    // semantic-order differences. CLIPW is the translator's helper varying.
    source=std::regex_replace(source,std::regex(R"((float\s+clipw\s*:\s*CLIPW))"),"[[vk::location(0)]] $1");
    std::regex attr(R"((float4\s+a(\d+)\s*:\s*ATTR\d+))");rewritten.clear();last=0;
    for(std::sregex_iterator i(source.begin(),source.end(),attr),end;i!=end;++i){auto &m=*i;rewritten+=source.substr(last,m.position()-last);rewritten+="[[vk::location("+std::to_string(1+std::stoul(m[2].str()))+")]] "+m.str();last=m.position()+m.length();}rewritten+=source.substr(last);result.source=rewritten;
    auto legacy=cache_file(result.source,stage);result.source=stable_temporaries(result.source);
    work_stats.rewrite_ns+=elapsed_ns(rewrite_start);
    auto cache=cache_file(result.source,stage);capture_input(cache,hlsl,stage);
    bool hit=read_cache(cache,result.words);
    // Existing binaries remain valid: only temporary variable names changed.
    // Import a legacy entry before asking DXC to compile the canonical source.
    if(!hit&&cache!=legacy&&read_cache(legacy,result.words)){write_cache(cache,result.words);hit=true;}
    if(getenv("SSBU_LOG_STATS")){auto slash=cache.find_last_of("/\\");fprintf(stderr,"[native shader cache] %s %s %s\n",name,cache.empty()?"disabled":cache.c_str()+(slash==std::string::npos?0:slash+1),hit?"hit":"miss");}
    if(hit){work_stats.cache_hits++;prune_unused_bindings(result,stage);return result;}
    work_stats.cache_misses++;
    if(getenv("SSBU_LOG_STATS")){fprintf(stderr,"[native work] DXC begin %s\n",name);fflush(stderr);}
    LARGE_INTEGER dxc_start;QueryPerformanceCounter(&dxc_start);
    static HMODULE library=LoadLibraryW(L"dxcompiler.dll");
    if(!library)throw std::runtime_error("Native Vulkan requires dxcompiler.dll beside its executable");
    auto create=(DxcCreateInstanceProc)GetProcAddress(library,"DxcCreateInstance");if(!create)throw std::runtime_error("DxcCreateInstance unavailable");
    IDxcCompiler3 *compiler=nullptr;HRESULT hr=create(CLSID_DxcCompiler,IID_PPV_ARGS(&compiler));if(FAILED(hr))throw std::runtime_error("DXC compiler initialization failed");
    const wchar_t *target=stage==0?L"vs_6_0":stage==1?L"ps_6_0":L"cs_6_0";
    const wchar_t *args[]={L"-spirv",L"-T",target,L"-E",L"main",L"-fspv-target-env=vulkan1.2",L"-fvk-use-dx-layout",L"-fspv-preserve-interface",L"-Gis",L"-O3"};
    DxcBuffer input={result.source.data(),result.source.size(),DXC_CP_UTF8};IDxcResult *compiled=nullptr;
    hr=compiler->Compile(&input,args,sizeof args/sizeof args[0],nullptr,IID_PPV_ARGS(&compiled));compiler->Release();
    if(FAILED(hr)||!compiled)throw std::runtime_error("DXC invocation failed");
    HRESULT status;compiled->GetStatus(&status);IDxcBlobUtf8 *errors=nullptr;compiled->GetOutput(DXC_OUT_ERRORS,IID_PPV_ARGS(&errors),nullptr);
    std::string message=errors?errors->GetStringPointer():"";if(errors)errors->Release();
    if(FAILED(status)){compiled->Release();throw std::runtime_error(std::string(name)+": DXC SPIR-V compilation failed:\n"+message);}
    IDxcBlob *object=nullptr;compiled->GetOutput(DXC_OUT_OBJECT,IID_PPV_ARGS(&object),nullptr);compiled->Release();
    if(!object)throw std::runtime_error("DXC returned no SPIR-V object");
    size_t size=object->GetBufferSize();if(size%4){object->Release();throw std::runtime_error("DXC returned a truncated SPIR-V object");}result.words.resize(size/4);memcpy(result.words.data(),object->GetBufferPointer(),size);object->Release();
    if(result.words.empty()||result.words[0]!=0x07230203)throw std::runtime_error("DXC output is not SPIR-V");
    uint64_t duration=elapsed_ns(dxc_start);work_stats.dxc_ns+=duration;
    if(getenv("SSBU_LOG_STATS"))fprintf(stderr,"[native work] DXC end %s %.1f ms\n",name,duration/1e6);
    write_cache(cache,result.words);
    prune_unused_bindings(result,stage);
    return result;
}
}
