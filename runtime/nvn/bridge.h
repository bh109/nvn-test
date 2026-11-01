#pragma once
#include "recomp.h"

#ifdef __cplusplus
extern "C" {
#endif
/* Transparent NVN interception seam. Audit mode still executes the original
 * SDK and renderer; it is not the OpenSmash renderer. */
void nvn_bridge_bootstrap(Cpu *c);
void nvn_bridge_report(void);
unsigned nvn_bridge_entry_count(void);
#ifdef __cplusplus
}
#endif
