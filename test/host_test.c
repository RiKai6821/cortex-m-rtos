/*
 * host_test.c - unit tests for the portable kernel logic, run on the host.
 *
 * Method: os_init() builds the idle task in slot 0; we create more tasks and
 * model "task i is running" by setting current_tcb = &g_tasks[i]. We then call
 * the real scheduler/primitives and assert the resulting TCB/queue/timer state.
 * The primitives use hand-off semantics (no spin loops), so each blocking
 * decision and wake is a clean, observable state transition.
 */
#include "kernel_internal.h"
#include "test_framework.h"

static void dummy(void *arg) { (void)arg; }

static int runnable(int i) {
    return g_tasks[i].state == TASK_READY || g_tasks[i].state == TASK_RUNNING;
}

/* ------------------------------------------------------------------ */
static void test_scheduler(void) {
    GROUP("scheduler: priority preemption + round-robin");
    static uint32_t s0[32], s1[32], s2[32];
    os_init();                                   /* slot 0 = idle (prio 0) */
    int a = os_task_create(dummy, 0, 2, s0, 32);
    int b = os_task_create(dummy, 0, 2, s1, 32);
    int c = os_task_create(dummy, 0, 3, s2, 32); /* highest */

    current_tcb = &g_tasks[a];
    scheduler();
    CHECK(next_tcb == &g_tasks[c]);              /* prio 3 beats prio 2 */

    g_tasks[c].state = TASK_BLOCKED;             /* c steps aside */
    g_tasks[a].state = TASK_RUNNING; current_tcb = &g_tasks[a];
    scheduler();
    CHECK(next_tcb == &g_tasks[b]);              /* round-robin a -> b */
    g_tasks[b].state = TASK_RUNNING; current_tcb = &g_tasks[b];
    scheduler();
    CHECK(next_tcb == &g_tasks[a]);              /* round-robin b -> a */
}

/* ------------------------------------------------------------------ */
static void test_mutex_priority_inheritance(void) {
    GROUP("mutex: priority inheritance");
    static uint32_t sl[32], sh[32];
    os_init();
    int L = os_task_create(dummy, 0, 1, sl, 32); /* low  priority */
    int H = os_task_create(dummy, 0, 3, sh, 32); /* high priority */
    mutex_t m; mutex_init(&m);

    current_tcb = &g_tasks[L];
    mutex_lock(&m);
    CHECK(m.owner == &g_tasks[L]);
    CHECK(g_tasks[L].priority == 1);             /* no boost yet */

    current_tcb = &g_tasks[H];
    mutex_lock(&m);                              /* contended -> H blocks */
    CHECK(g_tasks[H].state == TASK_BLOCKED);
    CHECK(g_tasks[H].waiting == &m);
    CHECK(g_tasks[L].priority == 3);             /* L INHERITS H's priority */
    CHECK(m.owner == &g_tasks[L]);

    current_tcb = &g_tasks[L];
    mutex_unlock(&m);
    CHECK(g_tasks[L].priority == 1);             /* boost removed */
    CHECK(m.owner == &g_tasks[H]);               /* handed to waiter */
    CHECK(runnable(H));                           /* H unblocked */
}

/* ------------------------------------------------------------------ */
static void test_semaphore(void) {
    GROUP("semaphore: blocking + hand-off");
    static uint32_t sa[32], sb[32];
    os_init();
    int A = os_task_create(dummy, 0, 2, sa, 32);
    int B = os_task_create(dummy, 0, 2, sb, 32);
    sem_t s; sem_init(&s, 0);

    current_tcb = &g_tasks[A];
    sem_wait(&s);                                /* count 0 -> A blocks */
    CHECK(g_tasks[A].state == TASK_BLOCKED);
    CHECK(g_tasks[A].waiting == &s);

    current_tcb = &g_tasks[B];
    sem_post(&s);                                /* hands token to A */
    CHECK(runnable(A));
    CHECK(s.count == 0);                         /* no count bump (hand-off) */
}

/* ------------------------------------------------------------------ */
static void test_queue(void) {
    GROUP("queue: buffered + blocking rendezvous (with data)");
    static uint32_t sp[32], sc[32];
    static int storage[4];
    os_init();
    int P = os_task_create(dummy, 0, 2, sp, 32);
    int C = os_task_create(dummy, 0, 2, sc, 32);
    queue_t q; queue_init(&q, storage, sizeof(int), 4);

    /* buffered path */
    current_tcb = &g_tasks[P];
    int v = 42; queue_send(&q, &v);
    CHECK(queue_count(&q) == 1);
    current_tcb = &g_tasks[C];
    int out = 0; queue_recv(&q, &out);
    CHECK(out == 42 && queue_count(&q) == 0);

    /* blocking rendezvous: recv on empty, then send delivers straight in */
    current_tcb = &g_tasks[C];
    out = -1;
    queue_recv(&q, &out);                        /* empty -> C parks */
    CHECK(g_tasks[C].state == TASK_BLOCKED);
    CHECK(g_tasks[C].waiting == &q.recv_wait);

    current_tcb = &g_tasks[P];
    int v2 = 99; queue_send(&q, &v2);            /* delivers into C's buffer */
    CHECK(out == 99);                            /* DATA arrived */
    CHECK(runnable(C));
}

/* ------------------------------------------------------------------ */
static void test_queue_full(void) {
    GROUP("queue: full -> sender blocks, recv hands off the freed slot");
    static uint32_t sp[32], sc[32];
    static int storage[2];                       /* capacity 2 */
    os_init();
    int P = os_task_create(dummy, 0, 2, sp, 32);
    int C = os_task_create(dummy, 0, 2, sc, 32);
    queue_t q; queue_init(&q, storage, sizeof(int), 2);

    current_tcb = &g_tasks[P];
    int v10 = 10, v20 = 20, v30 = 30;
    queue_send(&q, &v10);
    queue_send(&q, &v20);
    CHECK(queue_count(&q) == 2);                  /* full */

    queue_send(&q, &v30);                         /* full -> P parks */
    CHECK(g_tasks[P].state == TASK_BLOCKED);
    CHECK(g_tasks[P].waiting == &q.send_wait);

    current_tcb = &g_tasks[C];
    int out = 0;
    queue_recv(&q, &out);                         /* gets 10, pulls 30 into slot */
    CHECK(out == 10);
    CHECK(runnable(P));                            /* sender woken */
    CHECK(queue_count(&q) == 2);                   /* 20, 30 now queued */

    queue_recv(&q, &out); CHECK(out == 20);
    queue_recv(&q, &out); CHECK(out == 30);        /* FIFO preserved incl handoff */
    CHECK(queue_count(&q) == 0);

    /* ring-buffer wraparound: head/tail must wrap correctly */
    for (int i = 0; i < 5; i++) {
        int in = 100 + i, got = -1;
        queue_send(&q, &in);
        queue_recv(&q, &got);
        CHECK(got == 100 + i);
    }
}

static int g_fired;
static void on_timer(timer_t *t) { (void)t; g_fired++; }

static void test_timers(void) {
    GROUP("software timers: periodic + one-shot");
    os_init();
    g_fired = 0;

    timer_t periodic; timer_init(&periodic, 5, 1, on_timer, 0);
    timer_start(&periodic);
    for (int i = 0; i < 12; i++) os_tick();      /* fires at tick 5 and 10 */
    CHECK(g_fired == 2);

    timer_stop(&periodic);
    for (int i = 0; i < 10; i++) os_tick();
    CHECK(g_fired == 2);                          /* stopped */

    timer_t oneshot; timer_init(&oneshot, 3, 0, on_timer, 0);
    timer_start(&oneshot);
    for (int i = 0; i < 10; i++) os_tick();
    CHECK(g_fired == 3);                          /* fires exactly once */
}

int main(void) {
    test_scheduler();
    test_mutex_priority_inheritance();
    test_semaphore();
    test_queue();
    test_queue_full();
    test_timers();
    return TEST_SUMMARY();
}
