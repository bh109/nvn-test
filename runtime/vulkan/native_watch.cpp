#include "native_watch.h"
#include <vector>
namespace nativevk {
bool WriteWatch::contains(uint64_t address,uint64_t bytes)const{return bytes&&address>=base&&address-base<size&&bytes<=size-(address-base);}
bool WriteWatch::changed(uint64_t address,uint64_t bytes,uint64_t &seen){
    if(!contains(address,bytes))return true;
    uint64_t lo=address&~4095ull,hi=(address+bytes+4095)&~4095ull;
    void *first=nullptr;ULONG_PTR count=1;ULONG granularity=0;
    if(GetWriteWatch(0,(void *)lo,(SIZE_T)(hi-lo),&first,&count,&granularity)!=0)return true;
    if(count){
        std::array<void *,4096> pages;
        uint64_t at=(uint64_t)first;
        while(at<hi){
            count=pages.size();
            if(GetWriteWatch(WRITE_WATCH_FLAG_RESET,(void *)at,(SIZE_T)(hi-at),pages.data(),&count,&granularity)!=0||granularity!=page_size)return true;
            if(!count)break;
            uint64_t stamp=++clock;
            for(ULONG_PTR i=0;i<count;i++){uint64_t page=((uint64_t)pages[i]-base)/page_size;auto &chunk=chunks[page/chunk_pages];chunk.newest=chunk.page[page%chunk_pages]=stamp;}
            if(count<pages.size())break;
            at=(uint64_t)pages[count-1]+page_size;
        }
    }
    bool written=false;
    for(uint64_t page=(lo-base)/page_size,end=(hi-base)/page_size;page<end;){uint64_t chunk_index=page/chunk_pages,chunk_end=(chunk_index+1)*chunk_pages;auto found=chunks.find(chunk_index);if(found!=chunks.end()&&found->second.newest>seen){for(;page<chunk_end&&page<end;page++)if(found->second.page[page%chunk_pages]>seen){written=true;break;}}if(written)break;page=chunk_end;}
    seen=clock;return written;
}
}
