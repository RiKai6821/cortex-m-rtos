/*
 * system.c - SystemInit(): 8 MHz HSE -> PLL x9 -> 72 MHz SYSCLK, so the 1 ms
 * SysTick in the kernel is accurate. Bare-register, no HAL.
 */
#include "stm32f103.h"

void SystemInit(void) {
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY)) { }

    FLASH_ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;

    RCC->CFGR &= ~(0x3FFFFFu);
    RCC->CFGR |= RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PLLSRC_HSE | RCC_CFGR_PLLMULL9;

    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) { }

    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS_PLL) != RCC_CFGR_SWS_PLL) { }
}
