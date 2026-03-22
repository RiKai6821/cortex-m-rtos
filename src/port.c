/*
 * port.c - Cortex-M3 port layer: the context switch.
 *
 * Why PendSV? Context switching is deferred to PendSV, configured at the
 * LOWEST exception priority. That guarantees the switch runs only after all
 * other ISRs have finished, so we never switch context in the middle of a
 * higher-priority interrupt, and the switch itself is atomic w.r.t. threads.
 *
 * Stacking model (Cortex-M, full-descending PSP):
 *   - on exception entry the HARDWARE auto-stacks  xPSR,PC,LR,R12,R3-R0
 *   - SOFTWARE (this handler) saves/restores the remaining R4-R11
 * EXC_RETURN 0xFFFFFFFD means "return to Thread mode, use the PSP".
 */
#include "rtos.h"
#include "stm32f103.h"

/* The context switch. Naked: no compiler prologue/epilogue, pure assembly so
 * we control exactly which registers are saved and where. */
__attribute__((naked)) void PendSV_Handler(void) {
    __asm volatile(
        "  mrs   r0, psp            \n"  /* r0 = current task's stack ptr     */
        "  ldr   r3, =current_tcb   \n"
        "  ldr   r2, [r3]           \n"  /* r2 = current_tcb                  */
        "  cbz   r2, 1f             \n"  /* first switch? nothing to save     */
        "  stmdb r0!, {r4-r11}      \n"  /* push R4-R11 (software-saved regs) */
        "  str   r0, [r2]           \n"  /* current_tcb->sp = r0              */
        "1:                         \n"
        "  ldr   r1, =next_tcb      \n"
        "  ldr   r1, [r1]           \n"  /* r1 = next_tcb                     */
        "  str   r1, [r3]           \n"  /* current_tcb = next_tcb            */
        "  ldr   r0, [r1]           \n"  /* r0 = next_tcb->sp                 */
        "  ldmia r0!, {r4-r11}      \n"  /* pop R4-R11                        */
        "  msr   psp, r0            \n"  /* PSP = restored stack pointer      */
        "  ldr   r0, =0xFFFFFFFD    \n"  /* EXC_RETURN: thread mode, use PSP  */
        "  bx    r0                 \n"  /* HW pops xPSR..R0 -> runs the task */
    );
}

/* System tick: age delays, re-schedule, and pend a switch if needed. */
void SysTick_Handler(void) {
    uint32_t p = port_enter_critical();
    os_tick();
    scheduler();
    if (next_tcb != current_tcb) port_trigger_pendsv();
    port_exit_critical(p);
}

/* Build a task's initial stack so the first PendSV "returns" into `entry`. */
void port_setup_stack(tcb_t *t, void (*entry)(void *), void *arg,
                      uint32_t *stack, uint32_t stack_words) {
    uint32_t *sp = stack + stack_words;
    sp = (uint32_t *)((uintptr_t)sp & ~0x7u);     /* 8-byte align (AAPCS) */

    *(--sp) = 0x01000000u;            /* xPSR: Thumb bit set          */
    *(--sp) = (uint32_t)entry;        /* PC: task entry               */
    *(--sp) = 0xFFFFFFFFu;            /* LR: task must not return      */
    *(--sp) = 0;                      /* R12                          */
    *(--sp) = 0; *(--sp) = 0; *(--sp) = 0;   /* R3, R2, R1            */
    *(--sp) = (uint32_t)arg;          /* R0: task argument            */
    for (int i = 0; i < 8; i++) *(--sp) = 0; /* R4-R11                */
    t->sp = sp;
}

void port_idle(void) {
    __asm volatile("wfi");
}

void port_init(void) {
    __asm volatile("cpsid i");        /* no ticks until the first task runs */

    /* PendSV at the lowest priority (0xFF); SysTick stays high so it can
     * preempt threads and pend PendSV. */
    SCB_SHPR3 |= (0xFFu << 16);

    /* 1 ms tick from the 72 MHz core clock. */
    SysTick->LOAD = (SYSCLK_HZ / 1000U) - 1U;
    SysTick->VAL  = 0;
    SysTick->CTRL = SYSTICK_CTRL_CLKSOURCE | SYSTICK_CTRL_TICKINT | SYSTICK_CTRL_ENABLE;
}

void port_trigger_pendsv(void) {
    SCB_ICSR = SCB_ICSR_PENDSVSET;
    __asm volatile("dsb \n isb");
}

void port_start_first_task(void) {
    port_trigger_pendsv();
    __asm volatile("cpsie i");        /* let the pended PendSV fire */
}

/* Critical section = disable interrupts via PRIMASK, restoring prior state. */
uint32_t port_enter_critical(void) {
    uint32_t primask;
    __asm volatile("mrs %0, primask \n cpsid i" : "=r"(primask));
    return primask;
}

void port_exit_critical(uint32_t primask) {
    __asm volatile("msr primask, %0" :: "r"(primask) : "memory");
}
