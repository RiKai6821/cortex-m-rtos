/*
 * rtos.h - public API and types for the mini preemptive RTOS kernel.
 *
 * Scheduling: fixed-priority preemptive, round-robin among equal priority.
 * Higher `priority` number = more urgent. An always-ready idle task (priority
 * 0) is created automatically so there is always something to run.
 *
 * All CPU-specific code lives behind the port_* layer, so the kernel logic
 * (scheduler, mutex, queue, timers) is plain portable C and is unit-tested on
 * the host with a stub port (see test/).
 */
#ifndef RTOS_H
#define RTOS_H

#include <stdint.h>

enum { TASK_READY, TASK_RUNNING, TASK_BLOCKED };

/* Task Control Block. `sp` MUST be the first member: the PendSV assembly
 * stores/loads the saved stack pointer at offset 0 of the TCB. */
typedef struct tcb {
    uint32_t *sp;
    uint8_t   state;
    uint8_t   priority;       /* effective priority (may be boosted by PI) */
    uint8_t   base_priority;  /* priority the task was created with         */
    uint32_t  delay;          /* ticks left when sleeping in os_delay       */
    void     *waiting;        /* the object this task is blocked on, or NULL */
    void     *msg;            /* queue rendezvous buffer while blocked      */
} tcb_t;

/* ---- kernel ---- */
void os_init(void);
int  os_task_create(void (*entry)(void *), void *arg, uint8_t priority,
                    uint32_t *stack, uint32_t stack_words);
void os_start(void);                 /* does not return */
void os_delay(uint32_t ticks);
void os_yield(void);

void os_tick(void);                  /* called from SysTick_Handler */
void scheduler(void);                /* picks next_tcb              */

/* ---- counting semaphore (blocking, priority-ordered wakeup) ---- */
typedef struct { volatile int32_t count; } sem_t;
void sem_init(sem_t *s, int32_t initial);
void sem_wait(sem_t *s);
void sem_post(sem_t *s);

/* ---- mutex with priority inheritance ---- */
typedef struct {
    tcb_t *owner;            /* task holding the lock, or NULL              */
} mutex_t;
void mutex_init(mutex_t *m);
void mutex_lock(mutex_t *m);
void mutex_unlock(mutex_t *m);

/* ---- fixed-size message queue (blocking send/receive) ---- */
typedef struct {
    uint8_t *storage;
    int      item_size, capacity, count, head, tail;
    char     recv_wait, send_wait;   /* their addresses are the wait objects */
} queue_t;
void queue_init(queue_t *q, void *storage, int item_size, int capacity);
int  queue_send(queue_t *q, const void *item);   /* blocks while full  */
int  queue_recv(queue_t *q, void *item);          /* blocks while empty */
int  queue_count(queue_t *q);

/* ---- software timers (tick-driven; callbacks run in tick context) ---- */
typedef struct timer {
    uint32_t      period, remaining;
    int           periodic, active;
    void        (*cb)(struct timer *);
    void         *arg;
    struct timer *next;
} timer_t;
void timer_init(timer_t *t, uint32_t period_ticks, int periodic,
                void (*cb)(timer_t *), void *arg);
void timer_start(timer_t *t);
void timer_stop(timer_t *t);
void timers_init(void);              /* reset the active-timer list */
void timers_tick(void);              /* called from os_tick */

/* ---- port layer (CPU-specific; ARM in port.c, stub in test/) ---- */
void     port_init(void);
void     port_start_first_task(void);
void     port_trigger_pendsv(void);
uint32_t port_enter_critical(void);
void     port_exit_critical(uint32_t state);
void     port_idle(void);            /* wfi on ARM, nothing on host */
/* Initialize a task's machine context so the first switch enters `entry`. */
void     port_setup_stack(tcb_t *t, void (*entry)(void *), void *arg,
                          uint32_t *stack, uint32_t stack_words);

/* Shared with the PendSV assembly (must be plain globals, not static). */
extern tcb_t *current_tcb;
extern tcb_t *next_tcb;

#endif /* RTOS_H */
