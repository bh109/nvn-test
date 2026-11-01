#pragma once
#include "opensmash_context.h"
namespace opensmash {
struct Import {const char *name;Function function;};
Function find_import(const char *name);
// A verified module contract must be installed before reference SDK addresses
// can be dispatched. Merely subtracting the two bases is not verification.
void configure_sdk(uint64_t reference_base,uint64_t guest_base,uint64_t bytes);
uint64_t sdk_guest_address(uint64_t reference_address);
struct GameAddress {uint64_t reference,guest;};
void configure_game_addresses(const GameAddress *addresses,unsigned count);
void configure_worker_roots(const uint64_t *addresses,unsigned count);
}
extern "C" {
uint64_t hostfn_address(const char *,opensmash::Function);
const char *hostfn_current_name(void);
unsigned long long hostfn_current_calls(void);
void register_imports(const opensmash::Import *,int);
void call_guest(opensmash::Context *,uint64_t);
void sdk_dispatch(opensmash::Context *,uint64_t);
int sdk_image_ready(void);
uint64_t ga(uint64_t);
void opensmash_worker_enter(opensmash::Context *,void *stack,uint64_t stack_bytes);
void opensmash_worker_leave(opensmash::Context *);
}
