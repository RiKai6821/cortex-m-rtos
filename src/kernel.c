/*
 * kernel.c - task management, the scheduler, the tick, and the building-block
 * helpers the sync primitives share. No CPU-specific code lives here (it is
 * all behind port_*), so this file also compiles and runs on the host for
 * unit testing.
 */
#include "kernel_internal.h"

tcb_t   g_tasks[MAX_TASKS];
int     g_ntasks;
tcb_t  *current_tcb;          /* shared with PendSV asm */
tcb_t  *next_tcb;

static uint32_t idle_stack[64];

static int task_is_runnable(const tcb_t *t) {
    return t->state == TASK_READY || t->state == TASK_RUNNING;
}

/* Build an initial stack that looks like a task was interrupted right before
 * its first instruction, so the first PendSV can "return" into it. */
int os_task_create(void (*entry)(void *), void *arg, uint8_t priority,
                   uint32_t *stack, uint32_t stack_words) {
    if (g_ntasks >= MAX_TASKS) return -1;
    tcb_t *t = &g_tasks[g_ntasks];

    t->state         = TASK_READY;
    t->priority      = priority;
    t->base_priority = priority;
    t->delay         = 0;
    t->waiting       = 0;
    t->msg           = 0;
    port_setup_stack(t, entry, arg, stack, stack_words);   /* machine context */
    return g_ntasks++;
}

/* Fixed-priority preemptive with round-robin among equals. */
void scheduler(void) {
    int n = g_ntasks;
    int cur = current_tcb ? (int)(current_tcb - g_tasks) : -1;

    int maxp = -1;
    for (int i = 0; i < n; i++)
        if (task_is_runnable(&g_tasks[i]) && g_tasks[i].priority > maxp)
            maxp = g_tasks[i].priority;

    tcb_t *pick = current_tcb;
    for (int k = 1; k <= n; k++) {
        int i = ((cur < 0 ? -1 : cur) + k) % n;
        if (i < 0) i += n;
        tcb_t *t = &g_tasks[i];
        if (task_is_runnable(t) && t->priority == maxp) { pick = t; break; }
    }

    if (current_tcb && current_tcb->state == TASK_RUNNING)
        current_tcb->state = TASK_READY;
    pick->state = TASK_RUNNING;
    next_tcb = pick;
}

void os_tick(void) {
    for (int i = 0; i < g_ntasks; i++) {
        tcb_t *t = &g_tasks[i];
        if (t->state == TASK_BLOCKED && t->delay > 0 && --t->delay == 0) {
            t->waiting = 0;
            t->state   = TASK_READY;
        }
    }
    timers_tick();
}

void os_yield(void) {
    uint32_t p = port_enter_critical();
    scheduler();
    if (next_tcb != current_tcb) port_trigger_pendsv();
    port_exit_critical(p);
}

void os_delay(uint32_t ticks) {
    uint32_t p = port_enter_critical();
    current_tcb->delay = ticks;
    current_tcb->state = TASK_BLOCKED;
    port_exit_critical(p);
    os_yield();
}

/* ---- helpers shared with mutex/queue/sem ---- */

void os_unblock(tcb_t *t) {
    t->waiting = 0;
    t->state   = TASK_READY;
}

tcb_t *os_highest_waiter(void *obj) {
    tcb_t *best = 0;
    for (int i = 0; i < g_ntasks; i++) {
        tcb_t *t = &g_tasks[i];
        if (t->state == TASK_BLOCKED && t->waiting == obj)
            if (!best || t->priority > best->priority) best = t;
    }
    return best;
}

void os_block_and_yield(void *obj, uint32_t p) {
    current_tcb->state   = TASK_BLOCKED;
    current_tcb->waiting = obj;
    port_exit_critical(p);
    os_yield();
}

/* ---- bring-up ---- */

static void idle_task(void *arg) {
    (void)arg;
    for (;;) port_idle();
}

void os_init(void) {
    g_ntasks    = 0;
    current_tcb = 0;
    next_tcb    = 0;
    timers_init();
    os_task_create(idle_task, 0, 0, idle_stack,
                   sizeof(idle_stack) / sizeof(idle_stack[0]));
}

void os_start(void) {
    port_init();                  /* disables IRQs, sets up PendSV + tick */
    current_tcb = 0;              /* first PendSV skips the save step      */
    scheduler();
    port_start_first_task();      /* triggers PendSV, then enables IRQs    */
    for (;;) port_idle();         /* unreachable                           */
}

/* ---- counting semaphore (hands the token to the highest-priority waiter) ---- */
void sem_init(sem_t *s, int32_t initial) { s->count = initial; }

void sem_wait(sem_t *s) {
    uint32_t p = port_enter_critical();
    if (s->count > 0) { s->count--; port_exit_critical(p); return; }
    os_block_and_yield(s, p);
}

void sem_post(sem_t *s) {
    uint32_t p = port_enter_critical();
    tcb_t *w = os_highest_waiter(s);
    if (w) os_unblock(w);
    else   s->count++;
    port_exit_critical(p);
    os_yield();
}
