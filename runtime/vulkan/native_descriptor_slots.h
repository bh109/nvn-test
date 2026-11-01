#pragma once
#include "native_device.h"
#include <array>
#include <bitset>
#include <stdexcept>
namespace nativevk {
// CPU descriptor metadata only. Values/pointers live until vkUpdateDescriptorSets
// returns, exactly as with the old per-draw maps. Never changes GPU lifetimes.
template<class T,size_t N> class DescriptorSlots {
    std::array<T,N> values_; // intentionally not cleared in bulk per draw
    std::bitset<N> present_{};
    std::map<uint32_t,T> overflow_;
    bool dense_;
public:
    explicit DescriptorSlots(bool dense=false):dense_(dense){}
    DescriptorSlots(const DescriptorSlots &)=delete;
    DescriptorSlots &operator=(const DescriptorSlots &)=delete;
    T &operator[](uint32_t slot){
        if(!dense_||slot>=N)return overflow_[slot];
        if(!present_[slot]){values_[slot]={};present_.set(slot);}
        return values_[slot];
    }
    T *find(uint32_t slot){
        if(dense_&&slot<N)return present_[slot]?&values_[slot]:nullptr;
        auto it=overflow_.find(slot);return it==overflow_.end()?nullptr:&it->second;
    }
    size_t count(uint32_t slot){return find(slot)?1:0;}
    T &at(uint32_t slot){auto value=find(slot);if(!value)throw std::out_of_range("Missing descriptor slot");return *value;}
};
struct DescriptorResources {
    DescriptorSlots<VkDescriptorBufferInfo,288> buffers;
    DescriptorSlots<VkDescriptorImageInfo,192> images;
    explicit DescriptorResources(bool dense=false):buffers(dense),images(dense){}
};
}
