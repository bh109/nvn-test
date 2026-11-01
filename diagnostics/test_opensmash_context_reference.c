// Compiled separately with the actual reference header, never our Cpu alias.
#include <stddef.h>
#include "runtime.h"
size_t opensmash_reference_layout(unsigned field){
    const size_t layout[]={sizeof(Ctx),offsetof(Ctx,x),offsetof(Ctx,v),offsetof(Ctx,N),offsetof(Ctx,tpidr),offsetof(Ctx,ex_addr),offsetof(Ctx,in_import),offsetof(Ctx,ex_val2)};
    return field<sizeof layout/sizeof layout[0]?layout[field]:0;
}
void opensmash_reference_callback(Ctx *c){
    c->x[0]=c->x[1]+7;c->x[31]-=16;c->v[0].f[0]+=3.5f;
    c->tpidr+=8;c->N=c->Z=c->C=c->V=0xffffffffu;
}
