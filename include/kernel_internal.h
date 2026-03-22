/*
 * kernel_internal.h - shared between the kernel and the sync primitives
 * (mutex/queue/timer) and used by the host unit tests to inspect state.
 * Not part of the public API.
 */
#ifndef KERNEL_INTERNAL_H
#define KERNEL_INTERNAL_H

#include "rtos.h"

#define MAX_TASKS 8

extern tcb_t g_tasks[MAX_TASKS];
extern int   g_ntasks;

/* Mark a blocked task ready again (clears its wait object). Caller holds the
 * critical section. */
void os_unblock(tcb_t *t);

/* Highest-priority task currently blocked on `obj`, or NULL. Caller holds the
 * critical section. */
tcb_t *os_highest_waiter(void *obj);

/* The standard "block the running task on `obj` then switch away" pattern,
 * used by the primitives. Must be called with the critical section already
 * entered (state `p`); it releases it and yields. */
void os_block_and_yield(void *obj, uint32_t p);

#endif /* KERNEL_INTERNAL_H */
