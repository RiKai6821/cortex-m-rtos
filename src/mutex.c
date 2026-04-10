/*
 * mutex.c - mutual exclusion with PRIORITY INHERITANCE.
 *
 * The classic priority-inversion fix: if a high-priority task blocks on a
 * mutex held by a lower-priority task, the holder is temporarily boosted to
 * the blocker's priority so it can finish and release quickly, instead of
 * being starved by unrelated medium-priority tasks. On release the holder's
 * priority is restored and the lock is handed to the highest-priority waiter.
 */
#include "kernel_internal.h"

void mutex_init(mutex_t *m) {
    m->owner = 0;
}

void mutex_lock(mutex_t *m) {
    uint32_t p = port_enter_critical();

    if (m->owner == 0) {                 /* free: take it */
        m->owner = current_tcb;
        port_exit_critical(p);
        return;
    }

    /* Contended: inherit our priority to the holder if we are more urgent,
     * so it isn't preempted by medium-priority tasks while we wait. */
    if (current_tcb->priority > m->owner->priority)
        m->owner->priority = current_tcb->priority;

    os_block_and_yield(m, p);            /* unlock() hands the lock to us */
}

void mutex_unlock(mutex_t *m) {
    uint32_t p = port_enter_critical();
    if (m->owner != current_tcb) {       /* only the holder may unlock */
        port_exit_critical(p);
        return;
    }

    /* Drop any inherited boost. */
    current_tcb->priority = current_tcb->base_priority;

    /* Hand the lock directly to the highest-priority waiter (no race window). */
    tcb_t *w = os_highest_waiter(m);
    if (w) { os_unblock(w); m->owner = w; }
    else   { m->owner = 0; }

    port_exit_critical(p);
    os_yield();                          /* a higher-priority new owner may run */
}
