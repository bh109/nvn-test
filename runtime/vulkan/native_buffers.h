#pragma once
#include "native_device.h"
#include "native_watch.h"
#include <map>
#include <tuple>
namespace nativevk {
// Empty GPU mappings read as zero in the bridge. Check every page, including
// interior pages and unaligned endpoints; never infer this from endpoints only.
bool all_unmapped(uint64_t va,uint64_t bytes,uint64_t (*translate)(uint64_t));
struct IndexBounds {uint32_t lo=~0u,hi=0;bool known=false,any=false;};
class IndexBoundsCache {
    struct Entry {uint64_t guest=0,seen=0;IndexBounds bounds;};
    WriteWatch &watch;
    using Key=std::tuple<uint64_t,uint32_t,uint32_t,bool,uint32_t>;
    std::map<Key,Entry> entries;
public:
    uint64_t hits=0,scans=0,bytes_read=0;
    explicit IndexBoundsCache(WriteWatch &watch):watch(watch){}
    IndexBounds get(uint64_t va,uint64_t guest,uint32_t count,uint32_t width,bool restart,uint32_t restart_index,void (*read)(uint64_t,size_t,void *));
    void clear(){entries.clear();}
};
struct PendingResult {uint64_t va,guest,bytes;Buffer buffer;};
class ComputeBatch {
    Device &device;
    std::vector<PendingResult> results;
public:
    uint64_t flushes=0,queued=0,max_ranges=0,pending_bytes=0;
    explicit ComputeBatch(Device &device):device(device){}
    bool empty()const{return results.empty();}
    bool overlaps(uint64_t address,uint64_t bytes,bool guest=false)const;
    Slice find(uint64_t va,uint64_t guest,uint64_t bytes)const;
    bool can_overlay(uint64_t guest,uint64_t bytes)const;
    bool covers(uint64_t guest,uint64_t bytes)const;
    // Unless covers() is true, destination contains the unchanged snapshot. Overlay only
    // intersecting retained results on the GPU, without CPU publication.
    void overlay(Slice destination,uint64_t guest,uint64_t bytes);
    void add(uint64_t va,uint64_t guest,uint64_t bytes,Buffer buffer);
    // Synchronize once, then publish all independent results to guest memory.
    void flush(void (*write)(uint64_t,size_t,const void *),uint64_t &wait_ns,uint64_t &copy_ns);
};
// Returned buffers are borrowed: the pool owns their memory. A slot is never
// reused until the submission that last acquired it has completed.
class ReadbackPool {
    struct Slot {Buffer buffer;uint64_t used;bool held=false;};
    Device &device;
    std::vector<Slot> slots;
public:
    uint64_t allocations=0,hits=0,allocated=0;
    explicit ReadbackPool(Device &device):device(device){}
    Buffer acquire(uint64_t bytes);
    // Pending guest results can be read by later submissions. Completion of
    // their original submission alone does not make these slots reusable.
    void hold(Buffer buffer);
    void release_held(); // caller has published after waiting for ALL submissions
    void close(); // caller has completed all GPU work
};
class BufferCache {
    struct SparsePage {uint64_t guest=0,bytes=0;};
    struct Entry {uint64_t guest=0,seen=0,used=~0ull,current_generation=~0ull;Slice persistent={},current={};bool persistent_current=false,shadow_current=false,sparse=false;std::vector<uint8_t> shadow;std::vector<SparsePage> pages;};
    Device &device;WriteWatch &watch;
    std::map<std::pair<uint64_t,uint64_t>,Entry> entries;
    std::vector<Buffer> slabs;
    uint64_t allocated=0;
    uint64_t offset=0,last_generation=~0ull;
    size_t slab_index=0;
    bool append_versions=false;
    bool full=false;
    uint64_t shadow_bytes=0;
    Slice allocate(uint64_t bytes);
public:
    uint64_t hits=0,copied=0;
    uint64_t verified_bytes=0,verified_reuses=0,shadow_copied_bytes=0;
    // Aggregate diagnostics: distinguish eligibility failures from real changes.
    uint64_t rejected_small=0,rejected_unwatched=0,rejected_budget=0,rejected_full=0;
    uint64_t created=0,rollovers=0,invalidated=0,dirty_checks=0,shadow_changes=0;
    uint64_t sparse_reuses=0,sparse_read_bytes=0,sparse_checks=0,sparse_changed=0;
    uint64_t version_allocations=0;
    using Reader=void (*)(uint64_t,size_t,void *);
    BufferCache(Device &device,WriteWatch &watch,bool append_versions=false):device(device),watch(watch),append_versions(append_versions){}
    bool append_only()const{return append_versions;}
    // compare_source is a caller-proven linear CPU image of the range. Exact
    // comparisons permit caching unwatched constants without trusting them.
    // Small ranges, unsupported ranges and a full cache return an empty slice.
    Slice get(uint64_t va,uint64_t guest,uint64_t bytes,Reader read,const void *compare_source=nullptr);
    Slice get_sparse(uint64_t va,uint64_t bytes,Reader read,uint64_t (*translate)(uint64_t));
    void invalidate(uint64_t address,uint64_t bytes,bool gpu);
    void collect(); // only after submission/fence completion
    void close();   // caller has completed all GPU work
};
}
