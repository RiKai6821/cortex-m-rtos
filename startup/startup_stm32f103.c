/*
 * startup_stm32f103.c - Cortex-M3 vector table + reset handler.
 *
 * PendSV_Handler and SysTick_Handler are declared weak here and overridden by
 * the kernel's strong definitions in port.c - that is how the scheduler hooks
 * into the two exceptions it needs.
 */
#include <stdint.h>

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;

void Reset_Handler(void);
void Default_Handler(void);
extern void SystemInit(void);
extern int  main(void);

#define WEAK __attribute__((weak, alias("Default_Handler")))
void NMI_Handler(void)        WEAK;
void HardFault_Handler(void)  WEAK;
void MemManage_Handler(void)  WEAK;
void BusFault_Handler(void)   WEAK;
void UsageFault_Handler(void) WEAK;
void SVC_Handler(void)        WEAK;
void DebugMon_Handler(void)   WEAK;
void PendSV_Handler(void)     WEAK;   /* overridden by port.c */
void SysTick_Handler(void)    WEAK;   /* overridden by port.c */

__attribute__((section(".isr_vector"), used))
void (*const g_vectors[])(void) = {
    (void (*)(void))&_estack,
    Reset_Handler,
    NMI_Handler, HardFault_Handler, MemManage_Handler,
    BusFault_Handler, UsageFault_Handler,
    0, 0, 0, 0,
    SVC_Handler, DebugMon_Handler, 0,
    PendSV_Handler,    /* 14 */
    SysTick_Handler,   /* 15 */
};

void Reset_Handler(void) {
    uint32_t *src = &_sidata, *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0;
    SystemInit();
    main();
    while (1) { }
}

void Default_Handler(void) {
    while (1) { }
}
