#include "native_buffers.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace nativevk {
bool all_unmapped(uint64_t va,uint64_t bytes,uint64_t (*translate)(uint64_t)){
    if(!bytes||va>~0ull-(bytes-1))return false;
    while(bytes){if(translate(va))return false;uint64_t step=std::min<uint64_t>(bytes,4096-(va&4095));va+=step;bytes-=step;}
    return true;
}
IndexBounds IndexBoundsCache::get(uint64_t va,uint64_t guest,uint32_t count,uint32_t width,bool restart,uint32_t restart_index,void (*read)(uint64_t,size_t,void *)){
    uint64_t bytes=(uint64_t)count*width;if(!count||(width!=1&&width!=2&&width!=4)||bytes>(16ull<<20)||!watch.contains(guest,bytes))return {};
    uint32_t mask=width==1?255:width==2?65535:~0u;restart_index&=mask;if(!restart)restart_index=0;
    if(entries.size()>=16384)entries.clear();auto &entry=entries[{va,count,width,restart,restart_index}];if(entry.guest!=guest){entry={};entry.guest=guest;}
    bool dirty=watch.changed(guest,bytes,entry.seen);if(entry.bounds.known&&!dirty){hits++;return entry.bounds;}
    std::vector<uint8_t> data((size_t)bytes);read(va,(size_t)bytes,data.data());IndexBounds result;result.known=true;
    for(uint32_t i=0;i<count;i++){uint32_t v=0;memcpy(&v,data.data()+(uint64_t)i*width,width);if(restart&&v==restart_index)continue;result.lo=std::min(result.lo,v);result.hi=std::max(result.hi,v);result.any=true;}
    entry.bounds=result;scans++;bytes_read+=bytes;return result;
}
bool ComputeBatch::overlaps(uint64_t address,uint64_t bytes,bool guest)const{
    if(!bytes)return !empty();uint64_t end=address+bytes;if(end<address)return !empty();
    for(auto &r:results){uint64_t start=guest?r.guest:r.va;if(address<start+r.bytes&&start<end)return true;}return false;
}
Slice ComputeBatch::find(uint64_t va,uint64_t guest,uint64_t bytes)const{
    if(!bytes)return {};for(auto &r:results){
        uint64_t offset=~0ull;
        if(va>=r.va&&va-r.va<=r.bytes&&bytes<=r.bytes-(va-r.va))offset=va-r.va;
        else if(guest&&guest>=r.guest&&guest-r.guest<=r.bytes&&bytes<=r.bytes-(guest-r.guest))offset=guest-r.guest;
        if(offset!=~0ull)return {r.buffer.handle,offset,bytes,r.buffer.mapped+offset};
    }return {};
}
void ComputeBatch::add(uint64_t va,uint64_t guest,uint64_t bytes,Buffer buffer){
    if(overlaps(va,bytes)||overlaps(guest,bytes,true))throw std::runtime_error("Dependent compute result added without synchronization");
    results.push_back({va,guest,bytes,buffer});queued++;pending_bytes+=bytes;max_ranges=std::max<uint64_t>(max_ranges,results.size());
}
bool ComputeBatch::can_overlay(uint64_t guest,uint64_t bytes)const{
    if(!guest||!bytes||(bytes&3)||guest+bytes<guest)return false;bool found=false;
    for(auto &r:results){uint64_t lo=std::max(guest,r.guest),hi=std::min(guest+bytes,r.guest+r.bytes);if(lo>=hi)continue;
        if(((lo-r.guest)|(lo-guest)|(hi-lo))&3)return false;found=true;}
    return found;
}
bool ComputeBatch::covers(uint64_t guest,uint64_t bytes)const{
    if(!can_overlay(guest,bytes))return false;
    for(auto &r:results)if(guest>=r.guest&&guest-r.guest<=r.bytes&&bytes<=r.bytes-(guest-r.guest))return true;
    return false;
}
void ComputeBatch::overlay(Slice destination,uint64_t guest,uint64_t bytes){
    if(!destination.buffer||destination.size<bytes||(destination.offset&3)||!can_overlay(guest,bytes))throw std::runtime_error("Invalid GPU result overlay");
    for(auto &r:results)if(r.buffer.handle==destination.buffer)throw std::runtime_error("GPU result overlay aliases its source buffer");
    device.barrier();
    for(auto &r:results){uint64_t lo=std::max(guest,r.guest),hi=std::min(guest+bytes,r.guest+r.bytes);if(lo>=hi)continue;
        VkBufferCopy copy={lo-r.guest,destination.offset+lo-guest,hi-lo};device.trace("overlay-copy",(uint64_t)r.buffer.handle,(uint64_t)destination.buffer,copy.srcOffset,copy.dstOffset,copy.size);vkCmdCopyBuffer(device.command,r.buffer.handle,destination.buffer,1,&copy);}
    device.barrier();
}
void ComputeBatch::flush(void (*write)(uint64_t,size_t,const void *),uint64_t &wait_ns,uint64_t &copy_ns){
    if(empty())return;device.trace("publish-compute",results.size(),pending_bytes);LARGE_INTEGER start;QueryPerformanceCounter(&start);device.barrier();device.submit();wait_ns+=elapsed_ns(start);
    QueryPerformanceCounter(&start);for(auto &r:results)write(r.va,r.bytes,r.buffer.mapped);copy_ns+=elapsed_ns(start);results.clear();pending_bytes=0;flushes++;
}
Buffer ReadbackPool::acquire(uint64_t bytes){
    size_t best=slots.size();
    for(size_t i=0;i<slots.size();i++)if(!slots[i].held&&slots[i].used<=device.completed_generation&&slots[i].buffer.size>=bytes&&(best==slots.size()||slots[i].buffer.size<slots[best].buffer.size))best=i;
    if(best!=slots.size()){device.trace("readback-reuse",(uint64_t)slots[best].buffer.handle,bytes,slots[best].used,device.completed_generation,device.generation);slots[best].used=device.generation;hits++;return slots[best].buffer;}
    uint64_t capacity=65536;while(capacity<bytes&&capacity<(1ull<<63))capacity*=2;
    if(capacity<bytes)throw std::runtime_error("Readback allocation size overflow");
    // Keep completed idle buffers within a 256 MiB soft budget. A single
    // dispatch may legitimately need more; live buffers are never reclaimed.
    for(auto it=slots.begin();it!=slots.end()&&allocated+capacity>(256ull<<20);){
        if(!it->held&&it->used<=device.completed_generation){allocated-=it->buffer.size;device.destroy(it->buffer);it=slots.erase(it);}else ++it;
    }
    auto buffer=device.buffer(capacity,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,true,true);
    slots.push_back({buffer,device.generation});allocations++;allocated+=buffer.size;return buffer;
}
void ReadbackPool::hold(Buffer buffer){for(auto &slot:slots)if(slot.buffer.handle==buffer.handle){slot.held=true;return;}throw std::runtime_error("Cannot retain an unknown compute buffer");}
void ReadbackPool::release_held(){
    if(device.completed_generation<device.last_submitted)throw std::runtime_error("Cannot release retained compute buffers before GPU completion");
    for(auto &slot:slots)if(slot.held){slot.held=false;slot.used=device.last_submitted;}
}
void ReadbackPool::close(){for(auto &slot:slots)device.destroy(slot.buffer);slots.clear();allocated=0;}
Slice BufferCache::allocate(uint64_t bytes){
    constexpr uint64_t slab_bytes=32ull<<20;
    uint64_t alignment=std::max<uint64_t>(16,device.properties.limits.minStorageBufferOffsetAlignment);
    for(;;){
        if(slab_index==slabs.size()){
            if(allocated+slab_bytes>(512ull<<20)){full=true;return {};}
            slabs.push_back(device.buffer(slab_bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));allocated+=slab_bytes;
        }
        auto &slab=slabs[slab_index];uint64_t at=(offset+alignment-1)/alignment*alignment;
        if(at+bytes<=slab.size){offset=at+bytes;return {slab.handle,at,bytes,slab.mapped+at};}
        slab_index++;offset=0;
    }
}
Slice BufferCache::get(uint64_t va,uint64_t guest,uint64_t bytes,Reader read,const void *compare_source){
    bool tracked=watch.contains(guest,bytes);
    if(bytes<4096){rejected_small++;return {};}
    if(bytes>(32ull<<20)||(!tracked&&(!compare_source||bytes>65536))){rejected_unwatched++;return {};}
    if(last_generation!=device.generation){last_generation=device.generation;if(full){device.wait_idle();entries.clear();shadow_bytes=0;slab_index=0;offset=0;full=false;rollovers++;}}
    auto key=std::make_pair(va,bytes);auto found=entries.find(key);
    // Unwatched memory is cached only with a CPU shadow for an exact check on
    // every access. Never infer "unchanged" from missing write-watch support.
    if(!tracked&&(found==entries.end()||found->second.shadow.empty())&&shadow_bytes+bytes>(128ull<<20)){rejected_budget++;return {};}
    if(found==entries.end()){
        auto slot=allocate(bytes);if(!slot.buffer){rejected_full++;return {};}
        Entry entry;entry.guest=guest;entry.persistent=slot;found=entries.emplace(key,std::move(entry)).first;
        created++;
    }
    auto &entry=found->second;
    if(entry.sparse){entry.sparse=false;entry.pages.clear();entry.seen=0;entry.persistent_current=false;entry.current_generation=~0ull;entry.shadow_current=false;}
    if(entry.guest!=guest){entry.guest=guest;entry.seen=0;entry.persistent_current=false;entry.current_generation=~0ull;entry.shadow_current=false;}
    bool dirty=tracked?watch.changed(guest,bytes,entry.seen):true;
    if(dirty)dirty_checks++;
    bool verified=false;
    if(dirty&&compare_source&&entry.shadow_current&&entry.shadow.size()==bytes){verified_bytes+=bytes;if(!memcmp(compare_source,entry.shadow.data(),(size_t)bytes)){dirty=false;verified=true;}else shadow_changes++;}
    if(!dirty&&entry.persistent_current){entry.used=device.generation;hits++;if(verified)verified_reuses++;return entry.persistent;}
    if(!dirty&&entry.current_generation==device.generation){hits++;if(verified)verified_reuses++;return entry.current;}
    if(compare_source&&bytes<=65536&&entry.shadow.empty()&&shadow_bytes+bytes<=(128ull<<20)){entry.shadow.resize((size_t)bytes);shadow_bytes+=bytes;}
    auto copy=[&](void *destination){if(compare_source)memcpy(destination,compare_source,(size_t)bytes);else read(va,bytes,destination);};
    if(append_versions&&entry.used!=~0ull){
        // Keep every written version immutable until the slab's fenced recycle,
        // rather than overwriting it based on its last-use completion stamp.
        auto fresh=allocate(bytes);
        if(!fresh.buffer){entry.persistent_current=false;entry.current_generation=~0ull;rejected_full++;return {};}
        entry.persistent=fresh;entry.used=~0ull;version_allocations++;
    }
    // Rewriting a mapped buffer already referenced by this submission would
    // change earlier draws as well. Use a fresh arena slice in that case.
    if(entry.used==~0ull||entry.used<=device.completed_generation){
        device.trace("cache-write",(uint64_t)entry.persistent.buffer,entry.persistent.offset,bytes,entry.used,device.completed_generation,device.generation);
        copy(entry.persistent.data);entry.current=entry.persistent;entry.persistent_current=true;entry.used=device.generation;
    }else{
        entry.current=device.upload(bytes,device.properties.limits.minStorageBufferOffsetAlignment);copy(entry.current.data);entry.persistent_current=false;
    }
    entry.shadow_current=compare_source&&entry.shadow.size()==bytes;if(entry.shadow_current){memcpy(entry.shadow.data(),compare_source,(size_t)bytes);shadow_copied_bytes+=bytes;}
    entry.current_generation=device.generation;copied+=bytes;return entry.current;
}
Slice BufferCache::get_sparse(uint64_t va,uint64_t bytes,Reader read,uint64_t (*translate)(uint64_t)){
    if(bytes<4096||bytes>65536||va>~0ull-(bytes-1))return {};
    if(last_generation!=device.generation){last_generation=device.generation;if(full){device.wait_idle();entries.clear();shadow_bytes=0;slab_index=0;offset=0;full=false;rollovers++;}}
    auto key=std::make_pair(va,bytes);auto found=entries.find(key);
    if((found==entries.end()||found->second.shadow.empty())&&shadow_bytes+bytes>(128ull<<20)){rejected_budget++;return {};}
    if(found==entries.end()){auto slot=allocate(bytes);if(!slot.buffer){rejected_full++;return {};}Entry entry;entry.persistent=slot;found=entries.emplace(key,std::move(entry)).first;created++;}
    auto &entry=found->second;
    if(!entry.sparse){entry.sparse=true;entry.guest=0;entry.shadow_current=false;entry.persistent_current=false;entry.current_generation=~0ull;entry.pages.clear();}
    if(entry.shadow.empty()){entry.shadow.resize((size_t)bytes);shadow_bytes+=bytes;}
    const bool valid=entry.shadow_current;bool changed=!valid;size_t page=0;
    uint8_t scratch[4096];
    for(uint64_t offset=0;offset<bytes;page++){
        uint64_t count=std::min<uint64_t>(bytes-offset,4096-((va+offset)&4095)),guest=translate(va+offset);
        if(page==entry.pages.size())entry.pages.push_back({});auto &previous=entry.pages[page];bool remapped=previous.guest!=guest;
        bool dirty=!valid||remapped;
        if(guest){
            // Compare only mapped bytes, never the zero-filled tail. Exact
            // checks avoid an OS write-watch query per tiny fragment and
            // remain correct for both watched and unwatched guest memory.
            read(va+offset,(size_t)count,scratch);sparse_read_bytes+=count;
            if(!valid||memcmp(scratch,entry.shadow.data()+offset,(size_t)count)){memcpy(entry.shadow.data()+offset,scratch,(size_t)count);shadow_copied_bytes+=count;changed=true;}
        }else if(dirty){for(uint64_t i=0;i<count;i++)if(entry.shadow[offset+i]){changed=true;break;}memset(entry.shadow.data()+offset,0,(size_t)count);}
        previous.guest=guest;previous.bytes=count;offset+=count;
    }
    entry.shadow_current=true;sparse_checks++;if(changed)sparse_changed++;
    if(!changed&&entry.persistent_current){entry.used=device.generation;hits++;sparse_reuses++;return entry.persistent;}
    if(!changed&&entry.current_generation==device.generation){hits++;sparse_reuses++;return entry.current;}
    if(append_versions&&entry.used!=~0ull){
        auto fresh=allocate(bytes);
        if(!fresh.buffer){entry.persistent_current=false;entry.current_generation=~0ull;rejected_full++;return {};}
        entry.persistent=fresh;entry.used=~0ull;version_allocations++;
    }
    if(entry.used==~0ull||entry.used<=device.completed_generation){memcpy(entry.persistent.data,entry.shadow.data(),(size_t)bytes);entry.current=entry.persistent;entry.persistent_current=true;entry.used=device.generation;}
    else{entry.current=device.upload(bytes,device.properties.limits.minStorageBufferOffsetAlignment);memcpy(entry.current.data,entry.shadow.data(),(size_t)bytes);entry.persistent_current=false;}
    entry.current_generation=device.generation;copied+=bytes;return entry.current;
}
void BufferCache::invalidate(uint64_t address,uint64_t bytes,bool gpu){
    if(!bytes)return;uint64_t end=address+bytes;if(end<address)end=~0ull;
    for(auto it=entries.begin();it!=entries.end();){uint64_t start=gpu?it->first.first:it->second.guest,size=it->first.second;bool overlap=start<end&&address<start+size;
        if(!gpu&&it->second.sparse){overlap=false;for(auto &page:it->second.pages)if(page.guest&&page.guest<end&&address<page.guest+page.bytes){overlap=true;break;}}
        if(overlap){shadow_bytes-=it->second.shadow.size();it=entries.erase(it);invalidated++;}else ++it;}
}
void BufferCache::collect(){} // slab slots stay reserved until a safe-generation rollover
void BufferCache::close(){for(auto &slab:slabs)device.destroy(slab);slabs.clear();entries.clear();allocated=offset=slab_index=shadow_bytes=0;full=false;}
}
