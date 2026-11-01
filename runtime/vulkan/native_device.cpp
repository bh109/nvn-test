#include "native_device.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace nativevk {
uint64_t elapsed_ns(LARGE_INTEGER start){static double ns_per_tick=[](){LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);return 1e9/frequency.QuadPart;}();LARGE_INTEGER end;QueryPerformanceCounter(&end);return (uint64_t)((end.QuadPart-start.QuadPart)*ns_per_tick);}
#define VK_FN(name) PFN_vk##name vk##name;
#include "vk_api.inc"
#undef VK_FN

void check(VkResult result,const char *operation) {
    if(result!=VK_SUCCESS){char text[256];snprintf(text,sizeof text,"%s failed: VkResult %d",operation,(int)result);throw std::runtime_error(text);}
}
VkExtent2D rendering_extent(const Image *const *images,size_t count){
    VkExtent2D extent={};bool found=false;
    for(size_t i=0;i<count;i++)if(images[i]&&images[i]->handle){
        if(!found){extent={images[i]->width,images[i]->height};found=true;}
        else{extent.width=std::min(extent.width,images[i]->width);extent.height=std::min(extent.height,images[i]->height);}
    }
    return extent;
}
VkRect2D rendering_scissor(VkRect2D scissor,VkExtent2D extent){
    int64_t left=std::clamp<int64_t>(scissor.offset.x,0,extent.width),top=std::clamp<int64_t>(scissor.offset.y,0,extent.height);
    int64_t right=std::clamp<int64_t>((int64_t)scissor.offset.x+scissor.extent.width,left,extent.width);
    int64_t bottom=std::clamp<int64_t>((int64_t)scissor.offset.y+scissor.extent.height,top,extent.height);
    return {{(int32_t)left,(int32_t)top},{(uint32_t)(right-left),(uint32_t)(bottom-top)}};
}
static std::vector<uint8_t> read_file(const char *path) {
    FILE *f=fopen(path,"rb");if(!f)return {};
    fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
    std::vector<uint8_t> data(n>0?n:0);if(n>0&&fread(data.data(),1,n,f)!=(size_t)n)data.clear();fclose(f);return data;
}
void Device::init(const char *filter,const char *cache) {
    HMODULE loader=LoadLibraryW(L"vulkan-1.dll");if(!loader)throw std::runtime_error("Vulkan loader unavailable");
#define VK_FN(name) vk##name=(PFN_vk##name)GetProcAddress(loader,"vk" #name);
#include "vk_api.inc"
#undef VK_FN
    if(!vkCreateInstance||!vkGetInstanceProcAddr)throw std::runtime_error("Vulkan loader entry points unavailable");
    VkApplicationInfo app={VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="WindowSmash native Vulkan";app.applicationVersion=1;app.pEngineName="WindowSmash";app.apiVersion=VK_API_VERSION_1_3;
    const char *extensions[]={VK_KHR_SURFACE_EXTENSION_NAME,VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    VkInstanceCreateInfo ci={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;ci.enabledExtensionCount=2;ci.ppEnabledExtensionNames=extensions;
    check(vkCreateInstance(&ci,nullptr,&instance),"vkCreateInstance");
#define VK_FN(name) if(!vk##name)vk##name=(PFN_vk##name)vkGetInstanceProcAddr(instance,"vk" #name);
#include "vk_api.inc"
#undef VK_FN
    uint32_t count=0;check(vkEnumeratePhysicalDevices(instance,&count,nullptr),"enumerate GPUs");std::vector<VkPhysicalDevice> devices(count);check(vkEnumeratePhysicalDevices(instance,&count,devices.data()),"enumerate GPUs");
    for(auto candidate:devices){VkPhysicalDeviceProperties props;vkGetPhysicalDeviceProperties(candidate,&props);if(filter&&*filter&&strcmp(filter,"auto")&& !strstr(props.deviceName,filter))continue;
        uint32_t n=0;vkGetPhysicalDeviceQueueFamilyProperties(candidate,&n,nullptr);std::vector<VkQueueFamilyProperties> families(n);vkGetPhysicalDeviceQueueFamilyProperties(candidate,&n,families.data());
        for(uint32_t i=0;i<n;i++)if((families[i].queueFlags&(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))==(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT)){physical=candidate;family=i;properties=props;timestamp_bits=families[i].timestampValidBits;break;}if(physical)break;}
    if(!physical)throw std::runtime_error("No matching graphics/compute Vulkan GPU");
    VkPhysicalDeviceVulkan13Features f13={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};VkPhysicalDeviceFeatures2 available={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};available.pNext=&f13;vkGetPhysicalDeviceFeatures2(physical,&available);
    if(!f13.dynamicRendering)throw std::runtime_error("Direct Vulkan renderer requires dynamicRendering");
    features.robustBufferAccess=available.features.robustBufferAccess;features.independentBlend=available.features.independentBlend;features.dualSrcBlend=available.features.dualSrcBlend;
    features.samplerAnisotropy=available.features.samplerAnisotropy;features.fillModeNonSolid=available.features.fillModeNonSolid;features.textureCompressionBC=available.features.textureCompressionBC;
    features.imageCubeArray=available.features.imageCubeArray;
    // Translated game shaders use texture gathers with extended offsets.
    // Physical-device support alone does not enable this shader capability.
    features.shaderImageGatherExtended=available.features.shaderImageGatherExtended;
    features.shaderClipDistance=available.features.shaderClipDistance;features.shaderCullDistance=available.features.shaderCullDistance;
    features.fragmentStoresAndAtomics=available.features.fragmentStoresAndAtomics;features.vertexPipelineStoresAndAtomics=available.features.vertexPipelineStoresAndAtomics;
    VkPhysicalDeviceVulkan13Features enable13={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};enable13.dynamicRendering=VK_TRUE;
    float priority=1;VkDeviceQueueCreateInfo qc={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qc.queueFamilyIndex=family;qc.queueCount=1;qc.pQueuePriorities=&priority;
    const char *device_extensions[]={VK_KHR_SWAPCHAIN_EXTENSION_NAME};VkDeviceCreateInfo dc={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dc.pNext=&enable13;dc.queueCreateInfoCount=1;dc.pQueueCreateInfos=&qc;dc.pEnabledFeatures=&features;dc.enabledExtensionCount=1;dc.ppEnabledExtensionNames=device_extensions;
    check(vkCreateDevice(physical,&dc,nullptr,&handle),"vkCreateDevice");vkGetDeviceQueue(handle,family,0,&queue);vkGetPhysicalDeviceMemoryProperties(physical,&memory);
    attachment_barriers=getenv("SSBU_VK_ATTACHMENT_BARRIERS")&&strcmp(getenv("SSBU_VK_ATTACHMENT_BARRIERS"),"0");
    submission_trace=getenv("SSBU_VK_SUBMISSION_TRACE")&&strcmp(getenv("SSBU_VK_SUBMISSION_TRACE"),"0");
    if(submission_trace)fprintf(stderr,"[native Vulkan] CPU submission history enabled (last 256 operations per command buffer)\n");
    gpu_draw_period=getenv("SSBU_VK_DRAW_TIMINGS")?(uint32_t)strtoul(getenv("SSBU_VK_DRAW_TIMINGS"),nullptr,10):0;if(gpu_draw_period>4096)gpu_draw_period=0;
    gpu_timings=timestamp_bits&&(gpu_draw_period||(getenv("SSBU_VK_GPU_TIMINGS")&&strcmp(getenv("SSBU_VK_GPU_TIMINGS"),"0")));
    if(!gpu_timings)gpu_draw_period=0;
    if(gpu_timings)fprintf(stderr,"[native Vulkan] diagnostic GPU timestamps enabled, %u valid bits, %.3f ns/tick\n",timestamp_bits,properties.limits.timestampPeriod);
    for(auto &frame:frames)create_frame(frame);
    activate_frame(0);
    cache_path=cache?cache:"";auto initial=read_file(cache_path.c_str());VkPipelineCacheCreateInfo pc={VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};pc.initialDataSize=initial.size();pc.pInitialData=initial.data();
    VkResult result=vkCreatePipelineCache(handle,&pc,nullptr,&pipelines);if(result!=VK_SUCCESS){pc.initialDataSize=0;pc.pInitialData=nullptr;check(vkCreatePipelineCache(handle,&pc,nullptr,&pipelines),"empty pipeline cache");}
    fprintf(stderr,"[native Vulkan] %s, API %u.%u.%u; direct SPIR-V, dynamic rendering\n",properties.deviceName,VK_VERSION_MAJOR(properties.apiVersion),VK_VERSION_MINOR(properties.apiVersion),VK_VERSION_PATCH(properties.apiVersion));
    begin();
}
void Device::create_frame(FrameState &frame){
    if(gpu_timings){VkQueryPoolCreateInfo qi={VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};qi.queryType=VK_QUERY_TYPE_TIMESTAMP;qi.queryCount=256;check(vkCreateQueryPool(handle,&qi,nullptr,&frame.timestamps),"timestamp pool");}
    VkCommandPoolCreateInfo cp={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cp.queueFamilyIndex=family;cp.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;check(vkCreateCommandPool(handle,&cp,nullptr,&frame.commands),"command pool");
    VkCommandBufferAllocateInfo ca={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=frame.commands;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;check(vkAllocateCommandBuffers(handle,&ca,&frame.command),"command buffer");
    VkFenceCreateInfo fc={VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};check(vkCreateFence(handle,&fc,nullptr,&frame.fence),"fence");
    VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,8192},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,65536},{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,32768},{VK_DESCRIPTOR_TYPE_SAMPLER,32768}};
    VkDescriptorPoolCreateInfo pool={VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=8192;pool.poolSizeCount=4;pool.pPoolSizes=sizes;check(vkCreateDescriptorPool(handle,&pool,nullptr,&frame.descriptors),"descriptor pool");
    VkSemaphoreCreateInfo semaphore={VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};check(vkCreateSemaphore(handle,&semaphore,nullptr,&frame.acquired),"frame acquire semaphore");
}
static uint32_t memory_type(Device &d,uint32_t bits,VkMemoryPropertyFlags flags){for(uint32_t i=0;i<d.memory.memoryTypeCount;i++)if((bits&(1u<<i))&&(d.memory.memoryTypes[i].propertyFlags&flags)==flags)return i;throw std::runtime_error("No compatible Vulkan memory type");}
Buffer Device::buffer(VkDeviceSize size,VkBufferUsageFlags usage,bool host,bool cpu_readback){Buffer b;b.size=std::max<VkDeviceSize>(size,16);VkBufferCreateInfo ci={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};ci.size=b.size;ci.usage=usage;ci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;check(vkCreateBuffer(handle,&ci,nullptr,&b.handle),"buffer");VkMemoryRequirements req;vkGetBufferMemoryRequirements(handle,b.handle,&req);VkMemoryAllocateInfo ai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=memory_type(*this,req.memoryTypeBits,host?VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT:VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);// CPU reads from write-combined upload memory are extremely slow on Iris Xe.
    // Prefer coherent HOST_CACHED memory only for buffers read back by the CPU;
    // uploads keep their existing memory type. Fall back when unavailable.
    if(host&&cpu_readback){auto required=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT;for(uint32_t i=0;i<memory.memoryTypeCount;i++)if((req.memoryTypeBits&(1u<<i))&&(memory.memoryTypes[i].propertyFlags&required)==required){ai.memoryTypeIndex=i;break;}}
    b.memory_flags=memory.memoryTypes[ai.memoryTypeIndex].propertyFlags;
    check(vkAllocateMemory(handle,&ai,nullptr,&b.memory),"buffer memory");check(vkBindBufferMemory(handle,b.handle,b.memory,0),"bind buffer");if(host)check(vkMapMemory(handle,b.memory,0,VK_WHOLE_SIZE,0,(void **)&b.mapped),"map buffer");return b;}
void Device::destroy(Buffer &b){if(b.mapped)vkUnmapMemory(handle,b.memory);if(b.handle)vkDestroyBuffer(handle,b.handle,nullptr);if(b.memory)vkFreeMemory(handle,b.memory,nullptr);b={};}
Image Device::image(uint32_t w,uint32_t h,uint32_t depth,uint32_t layers,uint32_t levels,VkFormat format,VkImageUsageFlags usage,VkImageViewType type,VkImageAspectFlags aspect){Image image;image.width=w;image.height=h;image.depth=depth;image.layers=layers;image.levels=levels;image.format=format;image.type=type;image.aspect=aspect;
    VkImageCreateInfo ci={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=type==VK_IMAGE_VIEW_TYPE_3D?VK_IMAGE_TYPE_3D:(type==VK_IMAGE_VIEW_TYPE_1D||type==VK_IMAGE_VIEW_TYPE_1D_ARRAY)?VK_IMAGE_TYPE_1D:VK_IMAGE_TYPE_2D;ci.extent={w,h,depth};ci.mipLevels=levels;ci.arrayLayers=layers;ci.format=format;ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=usage;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;if(type==VK_IMAGE_VIEW_TYPE_CUBE||type==VK_IMAGE_VIEW_TYPE_CUBE_ARRAY)ci.flags|=VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    if(usage&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)ci.flags|=VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    check(vkCreateImage(handle,&ci,nullptr,&image.handle),"image");VkMemoryRequirements req;vkGetImageMemoryRequirements(handle,image.handle,&req);VkMemoryAllocateInfo ai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=memory_type(*this,req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);check(vkAllocateMemory(handle,&ai,nullptr,&image.memory),"image memory");check(vkBindImageMemory(handle,image.handle,image.memory,0),"bind image");VkImageViewCreateInfo vi={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=image.handle;vi.viewType=type;vi.format=format;vi.subresourceRange={aspect,0,levels,0,layers};check(vkCreateImageView(handle,&vi,nullptr,&image.view),"image view");return image;}
void Device::destroy(Image &i){if(i.view)vkDestroyImageView(handle,i.view,nullptr);if(i.handle)vkDestroyImage(handle,i.handle,nullptr);if(i.memory)vkFreeMemory(handle,i.memory,nullptr);i={};}
void Device::begin(){if(recording)return;VkCommandBufferBeginInfo begin={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;check(vkBeginCommandBuffer(command,&begin),"begin commands");recording=true;}
Slice Device::upload(VkDeviceSize size,VkDeviceSize alignment){auto &frame=frames[active_frame];auto &uploads=frame.uploads;auto &upload_chunk=frame.upload_chunk;auto &upload_at=frame.upload_at;begin();alignment=std::max<VkDeviceSize>(alignment,16);for(;;){if(upload_chunk==uploads.size())uploads.push_back(buffer(std::max<VkDeviceSize>(32ull<<20,size+alignment),VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));auto &b=uploads[upload_chunk];VkDeviceSize at=(upload_at+alignment-1)/alignment*alignment;if(at+size<=b.size){upload_at=at+size;trace("upload-slice",(uint64_t)b.handle,at,size,generation,completed_generation,active_frame);return {b.handle,at,size,b.mapped+at};}upload_chunk++;upload_at=0;}}
void Device::end_rendering(){if(rendering){vkCmdEndRendering(command);rendering=false;render_key.clear();}}
Slice Device::read_only_zero(uint64_t bytes){if(!bytes||bytes>65536)throw std::runtime_error("Read-only zero buffer range exceeds capacity");if(!read_only_zero_buffer.handle){read_only_zero_buffer=buffer(65536,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);memset(read_only_zero_buffer.mapped,0,65536);}return {read_only_zero_buffer.handle,0,bytes,read_only_zero_buffer.mapped};}
void Device::render(const VkRenderingInfo &info){
    std::vector<uint64_t> key={(uint64_t)info.renderArea.offset.x,(uint64_t)info.renderArea.offset.y,info.renderArea.extent.width,info.renderArea.extent.height,info.layerCount,info.viewMask,info.colorAttachmentCount,info.flags};
    bool loads_only=true;
    auto attachment=[&](const VkRenderingAttachmentInfo *a){if(!a){key.insert(key.end(),{0,0,0,0,0,0});return;}key.insert(key.end(),{(uint64_t)a->imageView,(uint64_t)a->imageLayout,(uint64_t)a->resolveMode,(uint64_t)a->resolveImageView,(uint64_t)a->resolveImageLayout,(uint64_t)a->storeOp});if(a->imageView&&a->loadOp!=VK_ATTACHMENT_LOAD_OP_LOAD)loads_only=false;};
    for(uint32_t i=0;i<info.colorAttachmentCount;i++)attachment(&info.pColorAttachments[i]);attachment(info.pDepthAttachment);attachment(info.pStencilAttachment);
    if(rendering&&loads_only&&render_key==key)return;
    // Separate dynamic rendering instances have no implicit ordering. Keep
    // the dependency at actual pass boundaries rather than every UI draw.
    VkPipelineStageFlags stages=0;VkAccessFlags access=0;
    for(uint32_t i=0;i<info.colorAttachmentCount;i++)if(info.pColorAttachments[i].imageView){stages|=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;access|=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;}
    if((info.pDepthAttachment&&info.pDepthAttachment->imageView)||(info.pStencilAttachment&&info.pStencilAttachment->imageView)){stages|=VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;access|=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;}
    if(attachment_barriers&&stages&&!info.flags&&!info.pNext){
        // The bridge has readonly graphics shader buffers. Compute publication,
        // buffer copies and image layout/sample transitions retain their explicit
        // full dependencies. Here only attachment LOAD/blend/depth/store hazards
        // need ordering; allow unrelated vertex/fragment work to overlap.
        end_rendering();begin();VkMemoryBarrier b={VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        b.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;b.dstAccessMask=access;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,stages,0,1,&b,0,nullptr,0,nullptr);attachment_render_barriers++;
    }else{barrier();full_render_barriers++;}
    vkCmdBeginRendering(command,&info);rendering=true;render_key=std::move(key);render_passes++;
}
void Device::barrier(){end_rendering();begin();VkMemoryBarrier b={VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&b,0,nullptr,0,nullptr);}
void Device::transition(Image &image,VkImageLayout layout){if(image.layout==layout&&(layout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL||layout==VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL||(rendering&&(layout==VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL||layout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL))))return;end_rendering();begin();VkImageMemoryBarrier b={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.srcAccessMask=image.layout==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT;b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;b.oldLayout=image.layout;b.newLayout=layout;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=image.handle;b.subresourceRange={image.aspect,0,image.levels,0,image.layers};vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);image.layout=layout;}
void Device::wait_frame(FrameState &frame){
    if(!frame.submitted)return;LARGE_INTEGER start;QueryPerformanceCounter(&start);
    auto result=vkWaitForFences(handle,1,&frame.fence,VK_TRUE,30000000000ull);
    if(result!=VK_SUCCESS)dump_submission_trace(frame.serial);
    check(result,"frame GPU completion (30s)");fence_wait_ns+=elapsed_ns(start);completed_generation=std::max(completed_generation,frame.serial);
    if(frame.timestamps&&!frame.timing_collected){std::vector<uint64_t> values(frame.query_count);check(vkGetQueryPoolResults(handle,frame.timestamps,0,frame.query_count,values.size()*sizeof(uint64_t),values.data(),sizeof(uint64_t),VK_QUERY_RESULT_64_BIT),"completed GPU timestamps");
        uint64_t mask=timestamp_bits==64?~0ull:(1ull<<timestamp_bits)-1;
        auto duration=[&](uint32_t first){return (uint64_t)(((values[first+1]-values[first])&mask)*properties.limits.timestampPeriod);};
        gpu_batch_ns+=duration(0);gpu_timed_batches++;
        for(auto first:frame.compute_queries){gpu_compute_ns+=duration(first);gpu_timed_dispatches++;}
        for(auto &query:frame.draw_queries){auto key=query.key;if(!gpu_draw_times.count(key)&&gpu_draw_times.size()>=16384)key={0,0};auto &cost=gpu_draw_times[key];cost.samples++;cost.ns+=duration(query.first);cost.vertices+=query.vertices;cost.pixels+=query.pixels;}
        frame.timing_collected=true;
    }
}
void Device::activate_frame(uint32_t index){
    active_frame=index;auto &frame=frames[index];wait_frame(frame);frame.trace_count=0;
    check(vkResetFences(handle,1,&frame.fence),"reset frame fence");frame.submitted=false;
    check(vkResetCommandPool(handle,frame.commands,0),"reset frame commands");check(vkResetDescriptorPool(handle,frame.descriptors,0),"reset frame descriptors");frame.upload_chunk=0;frame.upload_at=0;
    commands=frame.commands;command=frame.command;fence=frame.fence;descriptors=frame.descriptors;recording=false;rendering=false;render_key.clear();begin();
    trace("begin",index,generation,completed_generation,last_submitted);
    frame.query_count=2;frame.compute_queries.clear();frame.draw_queries.clear();frame.timing_collected=false;
    if(frame.timestamps){vkCmdResetQueryPool(command,frame.timestamps,0,256);vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,frame.timestamps,0);}
}
uint32_t Device::compute_timer_begin(){auto &frame=frames[active_frame];if(!frame.timestamps||frame.query_count+2>256)return ~0u;uint32_t first=frame.query_count;frame.query_count+=2;vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,frame.timestamps,first);return first;}
void Device::compute_timer_end(uint32_t first){if(first==~0u)return;auto &frame=frames[active_frame];vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,frame.timestamps,first+1);frame.compute_queries.push_back(first);}
uint32_t Device::draw_timer_begin(){
    if(!gpu_draw_period)return ~0u;if((gpu_draw_candidates++%gpu_draw_period)!=0)return ~0u;
    auto &frame=frames[active_frame];if(!frame.timestamps||frame.query_count+2>256){gpu_draw_skipped++;return ~0u;}
    uint32_t first=frame.query_count;frame.query_count+=2;vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,frame.timestamps,first);return first;
}
void Device::draw_timer_end(uint32_t first,DrawTimingKey key,uint32_t vertices,uint64_t pixels){if(first==~0u)return;auto &frame=frames[active_frame];vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,frame.timestamps,first+1);frame.draw_queries.push_back({first,key,vertices,pixels});}
void Device::wait_idle(){check(vkDeviceWaitIdle(handle),"GPU idle");completed_generation=std::max(completed_generation,last_submitted);}
void Device::trace(const char *kind,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){
    if(!submission_trace)return;auto &frame=frames[active_frame];
    frame.trace_events[frame.trace_count++%frame.trace_events.size()]={kind,{a,b,c,d,e,f}};
}
void Device::dump_submission_trace(uint64_t waiting_serial,const char *reason){
    if(!submission_trace)return;
    fprintf(stderr,"[native submission trace] reason=%s waiting=%llu completed=%llu lastSubmitted=%llu generation=%llu activeSlot=%u; CPU recording history, NOT GPU fault location\n",reason,waiting_serial,completed_generation,last_submitted,generation,active_frame);
    for(uint32_t i=0;i<2;i++){
        auto &frame=frames[i];uint64_t first=frame.trace_count>frame.trace_events.size()?frame.trace_count-frame.trace_events.size():0;
        fprintf(stderr,"[native submission trace] slot=%u serial=%llu submitted=%u events=%llu omitted=%llu\n",i,frame.serial,frame.submitted,frame.trace_count,first);
        for(uint64_t n=first;n<frame.trace_count;n++){auto &event=frame.trace_events[n%frame.trace_events.size()];fprintf(stderr,"[native submission trace] %llu %s",n,event.kind);for(auto value:event.values)fprintf(stderr," %#llx",value);fprintf(stderr,"\n");}
    }
    fflush(stderr);
}
void Device::submit(VkSemaphore wait,VkSemaphore signal,bool wait_for_gpu){
    if(!recording)return;end_rendering();
    // Fence completion alone does not publish device writes to the host memory
    // domain. Every submission may own mapped compute/transfer results read
    // after its fence (including asynchronous submissions waited on later).
    // HOST_COHERENT removes invalidation, not this device-to-host dependency.
    VkMemoryBarrier host_read={VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    host_read.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;
    host_read.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host_read,0,nullptr,0,nullptr);
    trace("host-readback",generation);
    if(frames[active_frame].timestamps)vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,frames[active_frame].timestamps,1);check(vkEndCommandBuffer(command),"end commands");recording=false;
    auto &frame=frames[active_frame];VkPipelineStageFlags stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;VkSubmitInfo info={VK_STRUCTURE_TYPE_SUBMIT_INFO};info.commandBufferCount=1;info.pCommandBuffers=&command;
    trace("submit",generation,wait_for_gpu,(uint64_t)wait,(uint64_t)signal,completed_generation);
    if(wait){info.waitSemaphoreCount=1;info.pWaitSemaphores=&wait;info.pWaitDstStageMask=&stage;}if(signal){info.signalSemaphoreCount=1;info.pSignalSemaphores=&signal;}
    LARGE_INTEGER start;QueryPerformanceCounter(&start);check(vkQueueSubmit(queue,1,&info,frame.fence),"submit commands");queue_submit_ns+=elapsed_ns(start);submissions++;
    frame.submitted=true;frame.serial=generation;last_submitted=generation;if(wait_for_gpu)wait_frame(frame);
    generation++;activate_frame((active_frame+1)%2);
}
VkDescriptorSet Device::descriptor_set(VkDescriptorSetLayout layout){VkDescriptorSet set;VkDescriptorSetAllocateInfo ai={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=descriptors;ai.descriptorSetCount=1;ai.pSetLayouts=&layout;check(vkAllocateDescriptorSets(handle,&ai,&set),"descriptor set");return set;}
void Device::save_cache(){if(cache_path.empty()||!pipelines||saved_pipeline_revision==pipeline_revision)return;LARGE_INTEGER start;QueryPerformanceCounter(&start);size_t size=0;if(vkGetPipelineCacheData(handle,pipelines,&size,nullptr)!=VK_SUCCESS)return;std::vector<uint8_t> bytes(size);if(vkGetPipelineCacheData(handle,pipelines,&size,bytes.data())!=VK_SUCCESS)return;auto temporary=cache_path+"."+std::to_string(GetCurrentProcessId())+".tmp";FILE *f=fopen(temporary.c_str(),"wb");if(f){bool ok=fwrite(bytes.data(),1,size,f)==size;if(fclose(f))ok=false;if(ok)ok=MoveFileExA(temporary.c_str(),cache_path.c_str(),MOVEFILE_REPLACE_EXISTING)!=0;if(!ok)DeleteFileA(temporary.c_str());else{saved_pipeline_revision=pipeline_revision;cache_saves++;cache_save_ns+=elapsed_ns(start);}}}
void Device::close(){
    if(!handle)return;vkDeviceWaitIdle(handle);end_rendering();save_cache();
    for(auto &frame:frames){if(frame.timestamps)vkDestroyQueryPool(handle,frame.timestamps,nullptr);for(auto &buffer:frame.uploads)destroy(buffer);frame.uploads.clear();if(frame.acquired)vkDestroySemaphore(handle,frame.acquired,nullptr);if(frame.descriptors)vkDestroyDescriptorPool(handle,frame.descriptors,nullptr);if(frame.fence)vkDestroyFence(handle,frame.fence,nullptr);if(frame.commands)vkDestroyCommandPool(handle,frame.commands,nullptr);frame={};}
    if(read_only_zero_buffer.handle)destroy(read_only_zero_buffer);
    if(pipelines)vkDestroyPipelineCache(handle,pipelines,nullptr);vkDestroyDevice(handle,nullptr);handle=VK_NULL_HANDLE;if(instance)vkDestroyInstance(instance,nullptr);instance=VK_NULL_HANDLE;
}
}
