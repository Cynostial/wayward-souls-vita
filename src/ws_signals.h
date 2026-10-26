#ifndef WS_SIGNALS_H
#define WS_SIGNALS_H
#include <stdint.h>
/* Guest-directed synchronous signals. CPU faults are handled by Vita's crash
 * mechanism; this layer cannot install a Linux kernel signal trampoline. */
uintptr_t ws_signals_symbol(const char *);
uint32_t ws_signal_mask_get(void);
void ws_signal_mask_restore(uint32_t);
void ws_signal_leave_handler(void);
void ws_jump_invalid(void);
#endif
