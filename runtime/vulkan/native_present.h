#pragma once
#include "native_device.h"
namespace nativevk {
// Presentation owns no window: the shared display layer remains responsible
// for input, settings, DPI and the Win32 message loop.
struct Presentation {
    Device *device=nullptr;
    HWND window=nullptr;
    VkSurfaceKHR surface=VK_NULL_HANDLE;
    VkSwapchainKHR swapchain=VK_NULL_HANDLE;
    std::vector<Image> images;
    std::vector<VkSemaphore> finished;
    VkExtent2D extent={};
    VkFormat format=VK_FORMAT_UNDEFINED;
    bool synchronized=true;
    bool wait_for_gpu=false; // Diagnostic: isolate overlap between rendered frames.
    uint64_t acquire_ns=0,present_ns=0;
    void init(Device &device,HWND window,bool synchronized=true);
    bool present(Image &source);
    void close();
private:
    bool resize();
};
}
