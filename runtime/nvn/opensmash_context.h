#pragma once
#include "recomp.h"

// Explicit marshaling boundary, not a cast between the two runtimes. Mirrors
// the reference runtime/runtime.h Ctx; checked against that header by the test.
namespace opensmash {
union Vector {uint8_t b[16];uint16_t h[8];uint32_t w[4];uint64_t d[2];float f[4];double df[2];};
struct Context {
    uint64_t x[32];Vector v[32];uint32_t N,Z,C,V;
    uint64_t tpidr,ex_addr,ex_val,ex_fail,fn;
    const char *in_import;uint64_t import_from,ex_val2;
};
using Function=void (*)(Context *);
inline Context from_cpu(const Cpu &cpu){
    Context result={};memcpy(result.x,cpu.x,sizeof cpu.x);result.x[31]=cpu.sp;
    memcpy(result.v,cpu.v,sizeof cpu.v);result.tpidr=cpu.tpidr;return result;
}
inline void to_cpu(const Context &source,Cpu &cpu){
    memcpy(cpu.x,source.x,sizeof cpu.x);cpu.sp=source.x[31];
    memcpy(cpu.v,source.v,sizeof cpu.v);cpu.tpidr=source.tpidr;
    // fpcr/fpsr, read-only TLS and kernel thread ownership belong to WindowSmash.
    // Context N/Z/C/V are integer condition flags, not floating-point controls.
}
inline void invoke(Function function,Cpu &cpu){Context context=from_cpu(cpu);function(&context);to_cpu(context,cpu);}
}
