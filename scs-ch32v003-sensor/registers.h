#ifndef REGISTERS_H
#define REGISTERS_H

#include <stdint.h>

/* Only registers used by this firmware. Offsets checked against WCH headers. */
#define REG32(address) (*(volatile uint32_t *)(address))
#define REG16(address) (*(volatile uint16_t *)(address))

#define RCC_CTLR       REG32(0x40021000u)
#define RCC_CFGR0      REG32(0x40021004u)
#define RCC_APB1PRSTR  REG32(0x40021010u)
#define RCC_APB2PCENR  REG32(0x40021018u)
#define RCC_APB1PCENR  REG32(0x4002101cu)
#define RCC_RSTSCKR    REG32(0x40021024u)

#define PWR_CTLR       REG32(0x40007000u)
#define PWR_AWUCSR     REG32(0x40007008u)
#define PWR_AWUWR      REG32(0x4000700cu)
#define PWR_AWUPSC     REG32(0x40007010u)

#define EXTI_INTENR    REG32(0x40010400u)
#define EXTI_EVENR     REG32(0x40010404u)
#define EXTI_RTENR     REG32(0x40010408u)
#define EXTI_FTENR     REG32(0x4001040cu)
#define EXTI_INTFR     REG32(0x40010414u)

#define PFIC_SCTLR     REG32(0xe000ed10u)

#define FLASH_USER     REG16(0x1ffff802u)

#define AWU_EVENT      (1u << 9)
#define REQUEST_EVENT  (1u << 4) /* PC4 / EXTI4 */

#define AWU_ENABLE     (1u << 1)
#define PWR_PDDS       (1u << 1)

#define SLEEPDEEP      (1u << 2)
#define WFITOWFE       (1u << 3)
#define SETEVENT       (1u << 5)

#define FLASH_ACTLR    REG32(0x40022000u)

#define AFIO_PCFR1     REG32(0x40010004u)
#define AFIO_EXTICR    REG32(0x40010008u)

#define GPIOC_CFGLR    REG32(0x40011000u)
#define GPIOC_INDR     REG32(0x40011008u)
#define GPIOC_BSHR     REG32(0x40011010u)

#define GPIOD_CFGLR    REG32(0x40011400u)
#define GPIOD_BSHR     REG32(0x40011410u)

#define USART_STATR    REG16(0x40013800u)
#define USART_DATAR    REG16(0x40013804u)
#define USART_BRR      REG16(0x40013808u)
#define USART_CTLR1    REG16(0x4001380cu)
#define USART_CTLR2    REG16(0x40013810u)
#define USART_CTLR3    REG16(0x40013814u)

#define USART_TE      (1u << 3)
#define USART_UE      (1u << 13)
#define USART_TC      (1u << 6)
#define USART_TXE     (1u << 7)

#define TIM_CTLR1      REG16(0x40000000u)
#define TIM_CTLR2      REG16(0x40000004u)
#define TIM_SMCFGR     REG16(0x40000008u)
#define TIM_DMAINTENR  REG16(0x4000000cu)
#define TIM_INTFR      REG16(0x40000010u)
#define TIM_SWEVGR     REG16(0x40000014u)
#define TIM_CHCTLR1    REG16(0x40000018u)
#define TIM_CHCTLR2    REG16(0x4000001cu)
#define TIM_CCER       REG16(0x40000020u)
#define TIM_CNT        REG16(0x40000024u)
#define TIM_PSC        REG16(0x40000028u)
#define TIM_ATRLR      REG16(0x4000002cu)
#define TIM_CH1CVR     REG32(0x40000034u)
#define TIM_CH3CVR     REG32(0x4000003cu)

#define CEN       (1u << 0)
#define UG        (1u << 0)
#define CC1IF     (1u << 1)
#define CC1E      (1u << 0)
#define CC1P      (1u << 1)
#define CC3E      (1u << 8)
#define CC1_DIRECT_TI1 1u

/* Channel 3 output mode is in CHCTLR2 bits 6:4. */
#define OC3_HIGH_ON_MATCH (1u << 4)
#define OC3_LOW_ON_MATCH  (2u << 4)
#define OC3_FORCE_LOW     (4u << 4)
#define OC3_FORCE_HIGH    (5u << 4)

#endif
