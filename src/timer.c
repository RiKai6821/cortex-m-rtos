/*
 * timer.c - software timers driven by the system tick.
 *
 * Active timers live on a singly-linked list. timers_tick() (called from
 * os_tick) decrements each and fires its callback on expiry; periodic timers
 * reload, one-shots go inactive. Callbacks run in tick (ISR) context, so they
 * must be short - the usual software-timer contract.
 */
#include "kernel_internal.h"

static timer_t *g_timer_list;

void timers_init(void) {
    g_timer_list = 0;
}

void timer_init(timer_t *t, uint32_t period_ticks, int periodic,
                void (*cb)(timer_t *), void *arg) {
    t->period    = period_ticks;
    t->remaining = period_ticks;
    t->periodic  = periodic;
    t->active    = 0;
    t->cb        = cb;
    t->arg       = arg;
    t->next      = 0;
}

void timer_start(timer_t *t) {
    uint32_t p = port_enter_critical();
    t->remaining = t->period;
    t->active    = 1;
    int present = 0;
    for (timer_t *x = g_timer_list; x; x = x->next)
        if (x == t) { present = 1; break; }
    if (!present) { t->next = g_timer_list; g_timer_list = t; }
    port_exit_critical(p);
}

void timer_stop(timer_t *t) {
    uint32_t p = port_enter_critical();
    t->active = 0;              /* stays on the list, just skipped */
    port_exit_critical(p);
}

void timers_tick(void) {
    for (timer_t *t = g_timer_list; t; t = t->next) {
        if (!t->active) continue;
        if (--t->remaining == 0) {
            if (t->periodic) t->remaining = t->period;
            else             t->active = 0;
            if (t->cb) t->cb(t);
        }
    }
}
