#include "nvn/opensmash_context.h"
#include <cstddef>
#include <cstdio>
extern "C" size_t opensmash_reference_layout(unsigned);
extern "C" void opensmash_reference_callback(opensmash::Context *);
int main(){
    using opensmash::Context;
    const size_t layout[]={sizeof(Context),offsetof(Context,x),offsetof(Context,v),offsetof(Context,N),offsetof(Context,tpidr),offsetof(Context,ex_addr),offsetof(Context,in_import),offsetof(Context,ex_val2)};
    for(unsigned i=0;i<sizeof layout/sizeof layout[0];i++)if(layout[i]!=opensmash_reference_layout(i)){fprintf(stderr,"FAIL: reference Ctx field %u layout mismatch\n",i);return 1;}
    Cpu c={};c.x[1]=35;c.sp=0x7700010000;c.v[0].f[0]=1.5;c.tpidr=100;
    c.fpcr=0x1234;c.fpsr=0x5678;c.tpidrro=0xabc;c.thread=(void *)0xdef;
    opensmash::invoke(opensmash_reference_callback,c);
    if(c.x[0]!=42||c.sp!=0x770000fff0||c.v[0].f[0]!=5.0||c.tpidr!=108||c.fpcr!=0x1234||c.fpsr!=0x5678||c.tpidrro!=0xabc||c.thread!=(void *)0xdef){fputs("FAIL: cross-runtime callback changed or lost CPU fields\n",stderr);return 1;}
    puts("PASS: actual OpenSmash Ctx ABI and callback marshaling preserve WindowSmash kernel/TLS/FP state");return 0;
}
