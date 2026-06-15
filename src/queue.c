/*
 * queue.c - fixed-size message queue with blocking send/receive.
 *
 * Hand-off semantics (no retry loops, so no lost wakeups and the logic is
 * deterministically testable):
 *   - send into an empty queue with a waiting receiver: copy straight into the
 *     receiver's buffer and wake it (rendezvous) - no enqueue needed.
 *   - recv from a full queue with a waiting sender: after dequeuing, pull the
 *     sender's item into the freed slot and wake it.
 * Otherwise items sit in the ring buffer.
 */
#include "kernel_internal.h"

/* local byte copy: keeps the kernel free of any libc dependency */
static void copy(void *dst, const void *src, int n) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (n--) *d++ = *s++;
}

void queue_init(queue_t *q, void *storage, int item_size, int capacity) {
    q->storage   = storage;
    q->item_size = item_size;
    q->capacity  = capacity;
    q->count = q->head = q->tail = 0;
}

static void enqueue(queue_t *q, const void *item) {
    copy(q->storage + q->tail * q->item_size, item, q->item_size);
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;
}

static void dequeue(queue_t *q, void *item) {
    copy(item, q->storage + q->head * q->item_size, q->item_size);
    q->head = (q->head + 1) % q->capacity;
    q->count--;
}

int queue_count(queue_t *q) { return q->count; }

int queue_send(queue_t *q, const void *item) {
    uint32_t p = port_enter_critical();

    /* a receiver is parked (so the queue is empty): deliver directly */
    tcb_t *r = os_highest_waiter(&q->recv_wait);
    if (r) {
        copy(r->msg, item, q->item_size);
        r->msg = 0;
        os_unblock(r);
        port_exit_critical(p);
        os_yield();
        return 0;
    }

    if (q->count < q->capacity) {
        enqueue(q, item);
        port_exit_critical(p);
        os_yield();
        return 0;
    }

    /* Full: park ourselves, recording where the item is. The receiver only
     * *reads* through this pointer (copies the item out), so discarding const
     * here is safe; the field is void* because it doubles as a writable
     * destination buffer when a task blocks on receive instead. */
    current_tcb->msg = (void *)(uintptr_t)item;
    os_block_and_yield(&q->send_wait, p);
    return 0;
}

int queue_recv(queue_t *q, void *item) {
    uint32_t p = port_enter_critical();

    if (q->count > 0) {
        dequeue(q, item);
        /* let a blocked sender fill the slot we just freed */
        tcb_t *s = os_highest_waiter(&q->send_wait);
        if (s) { enqueue(q, s->msg); s->msg = 0; os_unblock(s); }
        port_exit_critical(p);
        os_yield();
        return 0;
    }

    /* empty: park ourselves; a sender will copy straight into `item` */
    current_tcb->msg = item;
    os_block_and_yield(&q->recv_wait, p);
    return 0;
}
