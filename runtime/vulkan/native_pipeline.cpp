#include "native_pipeline.h"
#include "native_recipes.h"
#include <map>
#include <stdexcept>
#include <cstdio>
#include <cstdlib>
namespace nativevk {
static VkShaderModule module(Device &device,const Spirv &code){
    if(code.words.empty())throw std::runtime_error("Cannot create an empty Vulkan shader");
    VkShaderModuleCreateInfo info={VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};info.codeSize=code.words.size()*4;info.pCode=code.words.data();VkShaderModule result;
    check(vkCreateShaderModule(device.handle,&info,nullptr,&result),"SPIR-V shader module");return result;
}
void Program::create_layout(uint32_t count,const VkShaderStageFlags *stages){
    for(uint32_t set=0;set<count;set++){
        std::map<uint32_t,VkDescriptorType> unique;
        for(auto &binding:bindings[set]){auto found=unique.find(binding.binding);if(found!=unique.end()&&found->second!=binding.type)throw std::runtime_error("Conflicting Vulkan descriptor binding types");unique[binding.binding]=binding.type;}
        std::vector<VkDescriptorSetLayoutBinding> entries;
        for(auto &entry:unique)entries.push_back({entry.first,entry.second,1,stages[set],nullptr});
        VkDescriptorSetLayoutCreateInfo info={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};info.bindingCount=(uint32_t)entries.size();info.pBindings=entries.data();
        check(vkCreateDescriptorSetLayout(device->handle,&info,nullptr,&sets[set]),"shader descriptor layout");
    }
    VkPipelineLayoutCreateInfo info={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};info.setLayoutCount=count;info.pSetLayouts=sets;
    check(vkCreatePipelineLayout(device->handle,&info,nullptr,&layout),"shader pipeline layout");
}
void Program::init_graphics(Device &d,const Spirv &vs,const Spirv *ps){
    if(device)throw std::runtime_error("Vulkan Program is already initialized");device=&d;
    try{vertex=module(d,vs);bindings[0]=vs.bindings;if(ps){pixel=module(d,*ps);bindings[1]=ps->bindings;}VkShaderStageFlags stages[]={VK_SHADER_STAGE_VERTEX_BIT,VK_SHADER_STAGE_FRAGMENT_BIT};create_layout(2,stages);recipe_id=record_program(d.recipe_path,false,vs,ps);}catch(...){close();throw;}
}
void Program::init_compute(Device &d,const Spirv &cs){
    if(device)throw std::runtime_error("Vulkan Program is already initialized");device=&d;
    try{compute=module(d,cs);bindings[0]=cs.bindings;VkShaderStageFlags stages[]={VK_SHADER_STAGE_COMPUTE_BIT};create_layout(1,stages);recipe_id=record_program(d.recipe_path,true,cs,nullptr);}catch(...){close();throw;}
}
VkPipeline Program::dispatch(){
    if(!compute)throw std::runtime_error("No compute shader in Vulkan program");
    VkComputePipelineCreateInfo info={VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};info.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};info.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;info.stage.module=compute;info.stage.pName="main";info.layout=layout;
    if(getenv("SSBU_LOG_STATS")){fprintf(stderr,"[native work] compute pipeline begin\n");fflush(stderr);}
    LARGE_INTEGER start;QueryPerformanceCounter(&start);
    VkPipeline result;check(vkCreateComputePipelines(device->handle,device->pipelines,1,&info,nullptr,&result),"compute pipeline");
    uint64_t duration=elapsed_ns(start);work_stats.compute_ns+=duration;work_stats.compute_count++;
    device->pipeline_revision++;
    record_pipeline(device->recipe_path,recipe_id,nullptr);
    if(getenv("SSBU_LOG_STATS"))fprintf(stderr,"[native work] compute pipeline end %.1f ms\n",duration/1e6);return result;
}
VkPipeline Program::graphics(const GraphicsState &state){
    if(!vertex||compute)throw std::runtime_error("No vertex shader in Vulkan graphics program");
    if(state.color_count>8)throw std::runtime_error("Too many Vulkan color attachments");
    VkPipelineShaderStageCreateInfo stages[2]={};for(int i=0;i<2;i++){stages[i].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stages[i].stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT;stages[i].module=i?pixel:vertex;stages[i].pName="main";}
    VkPipelineVertexInputStateCreateInfo vertex_input={VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    std::vector<VkVertexInputBindingDescription> vertex_bindings;
    std::vector<VkVertexInputAttributeDescription> vertex_attributes;
    for(uint32_t i=0;i<state.vertices.size();i++)if(state.vertices[i].format){auto v=state.vertices[i];
        if(i>=device->properties.limits.maxVertexInputAttributes||i>=device->properties.limits.maxVertexInputBindings||v.stride>device->properties.limits.maxVertexInputBindingStride||v.offset>device->properties.limits.maxVertexInputAttributeOffset)throw std::runtime_error("Unsupported native vertex layout");
        vertex_bindings.push_back({i,v.stride,VK_VERTEX_INPUT_RATE_VERTEX});vertex_attributes.push_back({i,i,v.format,v.offset});
    }
    vertex_input.vertexBindingDescriptionCount=(uint32_t)vertex_bindings.size();vertex_input.pVertexBindingDescriptions=vertex_bindings.data();
    vertex_input.vertexAttributeDescriptionCount=(uint32_t)vertex_attributes.size();vertex_input.pVertexAttributeDescriptions=vertex_attributes.data();
    VkPipelineInputAssemblyStateCreateInfo assembly={VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};assembly.topology=state.topology;assembly.primitiveRestartEnable=state.primitive_restart;
    VkPipelineViewportStateCreateInfo viewport={VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};viewport.viewportCount=viewport.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo raster={VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.polygonMode=state.fill;raster.cullMode=state.cull;raster.frontFace=state.front;raster.lineWidth=1;
    VkPipelineDepthStencilStateCreateInfo depth={VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};depth.depthTestEnable=state.depth_test;depth.depthWriteEnable=state.depth_write;depth.depthCompareOp=state.depth_compare;depth.stencilTestEnable=state.stencil_test;depth.front=state.stencil_front;depth.back=state.stencil_back;depth.maxDepthBounds=1;
    VkPipelineMultisampleStateCreateInfo samples={VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};samples.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendStateCreateInfo blend={VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blend.attachmentCount=state.color_count;blend.pAttachments=state.blends.data();
    VkDynamicState dynamic_states[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR,VK_DYNAMIC_STATE_BLEND_CONSTANTS,VK_DYNAMIC_STATE_STENCIL_REFERENCE};VkPipelineDynamicStateCreateInfo dynamic={VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};dynamic.dynamicStateCount=4;dynamic.pDynamicStates=dynamic_states;
    VkPipelineRenderingCreateInfo rendering={VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};rendering.colorAttachmentCount=state.color_count;rendering.pColorAttachmentFormats=state.colors.data();rendering.depthAttachmentFormat=state.depth;rendering.stencilAttachmentFormat=state.stencil;
    VkGraphicsPipelineCreateInfo info={VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};info.pNext=&rendering;info.stageCount=pixel?2:1;info.pStages=stages;info.pVertexInputState=&vertex_input;info.pInputAssemblyState=&assembly;info.pViewportState=&viewport;info.pRasterizationState=&raster;info.pDepthStencilState=&depth;info.pMultisampleState=&samples;info.pColorBlendState=&blend;info.pDynamicState=&dynamic;info.layout=layout;
    if(getenv("SSBU_LOG_STATS")){fprintf(stderr,"[native work] graphics pipeline begin\n");fflush(stderr);}
    LARGE_INTEGER start;QueryPerformanceCounter(&start);
    VkPipeline result;check(vkCreateGraphicsPipelines(device->handle,device->pipelines,1,&info,nullptr,&result),"graphics pipeline");
    uint64_t duration=elapsed_ns(start);work_stats.graphics_ns+=duration;work_stats.graphics_count++;
    device->pipeline_revision++;
    record_pipeline(device->recipe_path,recipe_id,&state);
    if(getenv("SSBU_LOG_STATS"))fprintf(stderr,"[native work] graphics pipeline end %.1f ms\n",duration/1e6);return result;
}
void Program::close(){
    if(!device)return;auto handle=device->handle;
    if(layout)vkDestroyPipelineLayout(handle,layout,nullptr);layout=VK_NULL_HANDLE;
    for(auto &set:sets){if(set)vkDestroyDescriptorSetLayout(handle,set,nullptr);set=VK_NULL_HANDLE;}
    for(auto m:{vertex,pixel,compute})if(m)vkDestroyShaderModule(handle,m,nullptr);
    vertex=pixel=compute=VK_NULL_HANDLE;bindings[0].clear();bindings[1].clear();recipe_id=0;device=nullptr;
}
}
