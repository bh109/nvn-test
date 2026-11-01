#pragma once
#include <windows.h>
#include <cstdint>
#include <array>
#include <map>
namespace nativevk {
// One shared page clock preserves notifications when overlapping resources
// query/reset Windows write-watch bits in a different order.
class WriteWatch {
    static constexpr uint64_t page_size=4096,chunk_pages=512;
    struct Chunk {uint64_t newest=0;std::array<uint64_t,chunk_pages> page={};};
    uint64_t base,size,clock=0;
    std::map<uint64_t,Chunk> chunks;
public:
    WriteWatch(uint64_t base,uint64_t size):base(base),size(size){}
    bool contains(uint64_t address,uint64_t bytes)const;
    // Query before reading pixels. On an OS query failure, conservatively
    // report a change rather than treating possibly stale pixels as current.
    bool changed(uint64_t address,uint64_t bytes,uint64_t &seen);
};
}
