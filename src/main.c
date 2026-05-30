/*
 * main.c - RTOS demo exercising every primitive:
 *
 *   - a SOFTWARE TIMER blinks the PC13 LED every 500 ms (no dedicated task)
 *   - a PRODUCER task sends a counter into a MESSAGE QUEUE every 300 ms
 *   - a CONSUMER task receives from the queue and accumulates the running
 *     total under a PRIORITY-INHERITANCE MUTEX
 *
 * On hardware you watch the LED blink while the consumer's total advances;
 * the portable kernel logic underneath is unit-tested on the host (make test).
 */
#include "rtos.h"
#include "stm32f103.h"

static uint32_t stack_prod[128];
static uint32_t stack_cons[128];

static queue_t   g_queue;
static int       g_storage[8];
static mutex_t   g_lock;
volatile int     g_total;          /* shared; guarded by g_lock */
static timer_t   g_led_timer;

static void led_init(void) {
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
    GPIOC->CRH &= ~(0xFu << ((13 - 8) * 4));
    GPIOC->CRH |=  (0x2u << ((13 - 8) * 4));   /* PC13 output 2 MHz */
}

static void led_blink(timer_t *t) {            /* software-timer callback */
    (void)t;
    GPIOC->ODR ^= (1u << 13);
}

static void task_producer(void *arg) {
    (void)arg;
    int i = 0;
    for (;;) {
        i++;
        queue_send(&g_queue, &i);              /* blocks if the queue is full */
        os_delay(300);
    }
}

static void task_consumer(void *arg) {
    (void)arg;
    int v;
    for (;;) {
        queue_recv(&g_queue, &v);              /* blocks until an item arrives */
        mutex_lock(&g_lock);
        g_total += v;                          /* shared resource */
        mutex_unlock(&g_lock);
    }
}

int main(void) {
    led_init();
    queue_init(&g_queue, g_storage, sizeof(int), 8);
    mutex_init(&g_lock);

    os_init();
    os_task_create(task_producer, 0, 2, stack_prod, 128);
    os_task_create(task_consumer, 0, 2, stack_cons, 128);

    timer_init(&g_led_timer, 500, 1 /* periodic */, led_blink, 0);
    timer_start(&g_led_timer);

    os_start();                                /* does not return */
    for (;;) { }
}
