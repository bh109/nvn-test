#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <utility>

namespace nativevk {
// Deduplicates snapshots ONLY within one draw/dispatch. clear() is mandatory
// at the same command boundaries as the reference map. Does not cache guest
// contents across commands or change the lifetime of the referenced GPU slice.
template<class T,size_t N=256> class SnapshotCache {
    static_assert(N && !(N & (N-1)), "Snapshot table size must be a power of two");
    using Key=std::pair<uint64_t,size_t>;
    struct Slot { uint64_t epoch=0; Key key; T value; };
    std::array<Slot,N> slots_{};
    std::map<Key,T> fallback_;
    uint64_t epoch_=1;
    bool flat_=false;
    size_t used_=0;
    static size_t bucket(Key key){
        uint64_t x=key.first ^ (uint64_t(key.second)*0x9e3779b97f4a7c15ull);
        x=(x^(x>>30))*0xbf58476d1ce4e5b9ull;
        x=(x^(x>>27))*0x94d049bb133111ebull;
        return size_t(x^(x>>31))&(N-1);
    }
public:
    uint64_t requests=0,hits=0,inserts=0,overflows=0,max_entries=0;
    explicit SnapshotCache(bool flat=false):flat_(flat){}
    SnapshotCache(const SnapshotCache &)=delete;
    SnapshotCache &operator=(const SnapshotCache &)=delete;
    void clear(){
        fallback_.clear();used_=0;
        if(++epoch_==0){for(auto &slot:slots_)slot.epoch=0;epoch_=1;}
    }
    void set_flat(bool flat){clear();flat_=flat;}
    bool flat()const{return flat_;}
    T *find(Key key){
        requests++;
        if(flat_){
            size_t at=bucket(key);
            for(size_t probe=0;probe<N;probe++,at=(at+1)&(N-1)){
                auto &slot=slots_[at];
                if(slot.epoch!=epoch_)return nullptr;
                if(slot.key==key){hits++;return &slot.value;}
            }
        }
        auto it=fallback_.find(key);if(it==fallback_.end())return nullptr;
        hits++;return &it->second;
    }
    void store(Key key,const T &value){
        if(flat_){
            size_t at=bucket(key);
            for(size_t probe=0;probe<N;probe++,at=(at+1)&(N-1)){
                auto &slot=slots_[at];
                if(slot.epoch!=epoch_){slot.key=key;slot.value=value;slot.epoch=epoch_;inserts++;used_++;if(used_>max_entries)max_entries=used_;return;}
                if(slot.key==key){slot.value=value;return;}
            }
        }
        auto result=fallback_.emplace(key,value);
        if(result.second){inserts++;used_++;if(flat_)overflows++;if(used_>max_entries)max_entries=used_;}
        else result.first->second=value;
    }
};
}
