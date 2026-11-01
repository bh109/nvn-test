// Test-only frontend notifications. No fake graphics handlers enter game builds.
#include "hostrt.h"
unsigned fixture_pool_made,fixture_retired,fixture_flushed;
ImportFn nvn_gl_pick(const char *name){(void)name;return 0;}
void nvn_label_set(uint64_t object,const char *text){(void)object;(void)text;}
void nvn_label_drop(uint64_t object){(void)object;}
void nvn_gl_memory_written(uint64_t address,uint64_t size){if(address&&size)fixture_flushed++;}
void nvn_front_pool_made(uint64_t address,uint64_t size){if(address&&size)fixture_pool_made++;}
void nvn_front_memory_retire(uint64_t address,uint64_t size){if(address&&size)fixture_retired++;}
