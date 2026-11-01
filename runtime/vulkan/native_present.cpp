#include "native_present.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
namespace nativevk {
void Presentation::init(Device &d,HWND hwnd,bool sync){
    if(device)throw std::runtime_error("Vulkan presentation is already initialized");device=&d;window=hwnd;synchronized=sync;
    wait_for_gpu=getenv("SSBU_VK_SERIAL_PRESENT")&&strcmp(getenv("SSBU_VK_SERIAL_PRESENT"),"0");
    if(wait_for_gpu)fprintf(stderr,"[native Vulkan] diagnostic serial presentation enabled\n");
    try{
        VkWin32SurfaceCreateInfoKHR info={VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};info.hinstance=GetModuleHandleW(nullptr);info.hwnd=hwnd;
        check(vkCreateWin32SurfaceKHR(d.instance,&info,nullptr,&surface),"Win32 Vulkan surface");
        VkBool32 supported=VK_FALSE;check(vkGetPhysicalDeviceSurfaceSupportKHR(d.physical,d.family,surface,&supported),"presentation queue support");
        if(!supported)throw std::runtime_error("Selected Vulkan queue cannot present to this window");
        resize();
    }catch(...){close();throw;}
}
bool Presentation::resize(){
    RECT rect;if(!GetClientRect(window,&rect))throw std::runtime_error("Cannot read Vulkan window size");
    if(rect.right<=0||rect.bottom<=0)return false;
    VkSurfaceCapabilitiesKHR caps;check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device->physical,surface,&caps),"surface capabilities");
    if(!(caps.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_DST_BIT))throw std::runtime_error("Vulkan surface does not support image transfers");
    uint32_t count=0;check(vkGetPhysicalDeviceSurfaceFormatsKHR(device->physical,surface,&count,nullptr),"surface format count");std::vector<VkSurfaceFormatKHR> formats(count);check(vkGetPhysicalDeviceSurfaceFormatsKHR(device->physical,surface,&count,formats.data()),"surface formats");
    if(formats.empty())throw std::runtime_error("No Vulkan surface formats");
    auto chosen=formats[0];for(auto candidate:formats)if(candidate.format==VK_FORMAT_B8G8R8A8_UNORM&&candidate.colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR){chosen=candidate;break;}
    if(chosen.format==VK_FORMAT_UNDEFINED)chosen.format=VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D size=caps.currentExtent;if(size.width==~0u){size.width=std::clamp((uint32_t)rect.right,caps.minImageExtent.width,caps.maxImageExtent.width);size.height=std::clamp((uint32_t)rect.bottom,caps.minImageExtent.height,caps.maxImageExtent.height);}
    if(!size.width||!size.height)return false;
    VkSwapchainCreateInfoKHR info={VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};info.surface=surface;info.minImageCount=caps.minImageCount+1;if(caps.maxImageCount)info.minImageCount=std::min(info.minImageCount,caps.maxImageCount);info.imageFormat=chosen.format;info.imageColorSpace=chosen.colorSpace;info.imageExtent=size;info.imageArrayLayers=1;info.imageUsage=VK_IMAGE_USAGE_TRANSFER_DST_BIT;info.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE;info.preTransform=caps.currentTransform;info.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if(!(caps.supportedCompositeAlpha&info.compositeAlpha)){for(uint32_t bit=1;bit;bit<<=1)if(caps.supportedCompositeAlpha&bit){info.compositeAlpha=(VkCompositeAlphaFlagBitsKHR)bit;break;}}
    info.presentMode=VK_PRESENT_MODE_FIFO_KHR;info.clipped=VK_TRUE;info.oldSwapchain=swapchain;
    if(!synchronized){uint32_t mode_count=0;check(vkGetPhysicalDeviceSurfacePresentModesKHR(device->physical,surface,&mode_count,nullptr),"present mode count");std::vector<VkPresentModeKHR> modes(mode_count);check(vkGetPhysicalDeviceSurfacePresentModesKHR(device->physical,surface,&mode_count,modes.data()),"present modes");if(std::find(modes.begin(),modes.end(),VK_PRESENT_MODE_MAILBOX_KHR)!=modes.end())info.presentMode=VK_PRESENT_MODE_MAILBOX_KHR;else if(std::find(modes.begin(),modes.end(),VK_PRESENT_MODE_IMMEDIATE_KHR)!=modes.end())info.presentMode=VK_PRESENT_MODE_IMMEDIATE_KHR;}
    fprintf(stderr,"[native Vulkan] presentation %s, %s pacing\n",info.presentMode==VK_PRESENT_MODE_MAILBOX_KHR?"MAILBOX":info.presentMode==VK_PRESENT_MODE_IMMEDIATE_KHR?"IMMEDIATE":"FIFO",synchronized?"display":"clock");
    // Reset and replacement occur only with all earlier queue work completed.
    device->wait_idle();VkSwapchainKHR replacement;check(vkCreateSwapchainKHR(device->handle,&info,nullptr,&replacement),"swapchain");
    for(auto semaphore:finished)vkDestroySemaphore(device->handle,semaphore,nullptr);finished.clear();images.clear();if(swapchain)vkDestroySwapchainKHR(device->handle,swapchain,nullptr);swapchain=replacement;extent=size;format=chosen.format;
    check(vkGetSwapchainImagesKHR(device->handle,swapchain,&count,nullptr),"swapchain image count");std::vector<VkImage> handles(count);check(vkGetSwapchainImagesKHR(device->handle,swapchain,&count,handles.data()),"swapchain images");
    for(auto handle:handles){Image image;image.handle=handle;image.width=size.width;image.height=size.height;image.format=format;images.push_back(image);VkSemaphoreCreateInfo sci={VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};VkSemaphore semaphore;check(vkCreateSemaphore(device->handle,&sci,nullptr,&semaphore),"present semaphore");finished.push_back(semaphore);}
    return true;
}
bool Presentation::present(Image &source){
    if(!source.handle||!source.width||!source.height||source.aspect!=VK_IMAGE_ASPECT_COLOR_BIT)throw std::runtime_error("Vulkan presentation requires a valid color image");
    RECT rect;if(!GetClientRect(window,&rect)||rect.right<=0||rect.bottom<=0)return false;
    if(!swapchain||extent.width!=(uint32_t)rect.right||extent.height!=(uint32_t)rect.bottom)if(!resize())return false;
    VkFormatProperties source_properties,target_properties;vkGetPhysicalDeviceFormatProperties(device->physical,source.format,&source_properties);vkGetPhysicalDeviceFormatProperties(device->physical,format,&target_properties);
    if(!(source_properties.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_SRC_BIT)||!(target_properties.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_DST_BIT))throw std::runtime_error("Vulkan presentation format does not support image blitting");
    VkSemaphore acquired=device->acquire_semaphore();uint32_t index;LARGE_INTEGER acquire_start;QueryPerformanceCounter(&acquire_start);VkResult result=vkAcquireNextImageKHR(device->handle,swapchain,1000000000ull,acquired,VK_NULL_HANDLE,&index);acquire_ns+=elapsed_ns(acquire_start);
    if(result==VK_ERROR_OUT_OF_DATE_KHR){resize();return false;}
    if(result==VK_TIMEOUT||result==VK_NOT_READY)return false;
    if(result!=VK_SUBOPTIMAL_KHR)check(result,"acquire swapchain image");
    auto &target=images.at(index);device->transition(source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);device->transition(target,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkClearColorValue black={{0,0,0,1}};VkImageSubresourceRange range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};vkCmdClearColorImage(device->command,target.handle,target.layout,&black,1,&range);
    device->barrier();uint32_t width=extent.width,height=extent.height;
    if((uint64_t)width*source.height>(uint64_t)height*source.width)width=(uint64_t)height*source.width/source.height;else height=(uint64_t)width*source.height/source.width;
    int x=(extent.width-width)/2,y=(extent.height-height)/2;VkImageBlit blit={};blit.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};blit.dstSubresource=blit.srcSubresource;blit.srcOffsets[1]={(int32_t)source.width,(int32_t)source.height,1};blit.dstOffsets[0]={x,y,0};blit.dstOffsets[1]={x+(int)width,y+(int)height,1};
    vkCmdBlitImage(device->command,source.handle,source.layout,target.handle,target.layout,1,&blit,VK_FILTER_NEAREST);device->transition(target,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);device->submit(acquired,finished[index],wait_for_gpu);
    VkPresentInfoKHR info={VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};info.waitSemaphoreCount=1;info.pWaitSemaphores=&finished[index];info.swapchainCount=1;info.pSwapchains=&swapchain;info.pImageIndices=&index;LARGE_INTEGER present_start;QueryPerformanceCounter(&present_start);result=vkQueuePresentKHR(device->queue,&info);present_ns+=elapsed_ns(present_start);
    if(result==VK_ERROR_OUT_OF_DATE_KHR||result==VK_SUBOPTIMAL_KHR){resize();return true;}check(result,"present swapchain");return true;
}
void Presentation::close(){if(!device)return;auto handle=device->handle;device->wait_idle();for(auto semaphore:finished)vkDestroySemaphore(handle,semaphore,nullptr);finished.clear();images.clear();if(swapchain)vkDestroySwapchainKHR(handle,swapchain,nullptr);if(surface)vkDestroySurfaceKHR(device->instance,surface,nullptr);swapchain=VK_NULL_HANDLE;surface=VK_NULL_HANDLE;device=nullptr;window=nullptr;}
}
