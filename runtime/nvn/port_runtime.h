#pragma once
#include "recomp.h"
extern "C" {
int opensmash_port_selected(void);
void opensmash_port_bootstrap(Cpu *);
void opensmash_port_start(void *window);
void opensmash_port_frame(void);
void opensmash_port_close(void);
void opensmash_port_report(void);
void opensmash_port_memory_written(u64,u64);
void opensmash_port_memory_retire(u64,u64);
}
