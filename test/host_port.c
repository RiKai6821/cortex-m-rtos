/*
 * host_port.c - a stub port that lets the portable kernel logic run on the
 * host for unit testing. There is no real context switch: critical sections
 * are no-ops and port_trigger_pendsv does nothing. The tests model "which task
 * is running" by setting current_tcb directly, and assert the state-machine
 * transitions the kernel/primitives produce (blocking, waiter selection,
 * priority inheritance, queue rendezvous, timer expiry).
 */
#include "rtos.h"

uint32_t port_enter_critical(void)        { return 0; }
void     port_exit_critical(uint32_t s)   { (void)s; }
void     port_trigger_pendsv(void)        { /* no real switch on host */ }
void     port_init(void)                  { }
void     port_start_first_task(void)      { }
void     port_idle(void)                  { }

void port_setup_stack(tcb_t *t, void (*entry)(void *), void *arg,
                      uint32_t *stack, uint32_t stack_words) {
    (void)entry; (void)arg; (void)stack_words;
    t->sp = stack;                 /* unused on host, but keep it valid */
}
