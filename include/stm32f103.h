/*
 * stm32f103.h - the minimal register subset this RTOS kernel needs:
 * the Cortex-M3 core (SysTick + SCB for PendSV) plus RCC/GPIO so the demo
 * can blink an LED. Hand-written, no vendor HAL.
 */
#ifndef STM32F103_H
#define STM32F103_H

#include <stdint.h>
#define __IO volatile

/* ---- Cortex-M3 SysTick (0xE000E010) ---- */
typedef struct {
    __IO uint32_t CTRL, LOAD, VAL, CALIB;
} SysTick_TypeDef;
#define SysTick ((SysTick_TypeDef *)0xE000E010UL)
#define SYSTICK_CTRL_ENABLE    (1u << 0)
#define SYSTICK_CTRL_TICKINT   (1u << 1)
#define SYSTICK_CTRL_CLKSOURCE (1u << 2)

/* ---- System Control Block bits we use ---- */
#define SCB_ICSR  (*(__IO uint32_t *)0xE000ED04UL)  /* interrupt control/state */
#define SCB_SHPR3 (*(__IO uint32_t *)0xE000ED20UL)  /* PendSV/SysTick priority */
#define SCB_ICSR_PENDSVSET (1u << 28)

/* ---- RCC + GPIO (just enough for the LED) ---- */
#define AHB_BASE   0x40020000UL
#define APB2_BASE  0x40010000UL
typedef struct {
    __IO uint32_t CR, CFGR, CIR, APB2RSTR, APB1RSTR, AHBENR, APB2ENR, APB1ENR;
} RCC_TypeDef;
#define RCC ((RCC_TypeDef *)(AHB_BASE + 0x1000))
#define RCC_APB2ENR_IOPCEN (1u << 4)
#define RCC_CR_HSEON   (1u << 16)
#define RCC_CR_HSERDY  (1u << 17)
#define RCC_CR_PLLON   (1u << 24)
#define RCC_CR_PLLRDY  (1u << 25)
#define RCC_CFGR_SW_PLL     (0x2u << 0)
#define RCC_CFGR_SWS_PLL    (0x2u << 2)
#define RCC_CFGR_PPRE1_DIV2 (0x4u << 8)
#define RCC_CFGR_PLLSRC_HSE (1u << 16)
#define RCC_CFGR_PLLMULL9   (0x7u << 18)

/* FLASH access control: latency for 72 MHz operation. */
#define FLASH_ACR (*(__IO uint32_t *)(AHB_BASE + 0x2000))
#define FLASH_ACR_LATENCY_2 (0x2u << 0)
#define FLASH_ACR_PRFTBE    (1u << 4)

typedef struct {
    __IO uint32_t CRL, CRH, IDR, ODR, BSRR, BRR, LCKR;
} GPIO_TypeDef;
#define GPIOC ((GPIO_TypeDef *)(APB2_BASE + 0x1000))

/* SYSCLK after SystemInit() (72 MHz). Used to size the 1 ms SysTick. */
#define SYSCLK_HZ 72000000UL

void SystemInit(void);

#endif /* STM32F103_H */
