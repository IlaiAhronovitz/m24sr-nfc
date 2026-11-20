/* regs.h - STM32F103 registers this project touches (RM0008). */
#pragma once
#include <stdint.h>

#define REG32(a)        (*(volatile uint32_t *)(uintptr_t)(a))

static inline void irq_off(void) { __asm volatile("cpsid i" ::: "memory"); }
static inline void irq_on(void)  { __asm volatile("cpsie i" ::: "memory"); }

/* ---- Core ----------------------------------------------------------------- */
#define SCB_VTOR        REG32(0xE000ED08u)
#define SCB_CFSR        REG32(0xE000ED28u)
#define SCB_HFSR        REG32(0xE000ED2Cu)
#define SCB_BFAR        REG32(0xE000ED38u)
#define SYST_CSR        REG32(0xE000E010u)
#define SYST_RVR        REG32(0xE000E014u)
#define SYST_CVR        REG32(0xE000E018u)
#define DBGMCU_CR       REG32(0xE0042004u)
#define   DBGMCU_SLEEP      (1u << 0)     /* debug access stays alive in WFI */

/* ---- RCC (RM0008 section 7.3) -------------------------------------------- */
#define RCC_BASE        0x40021000u
#define RCC_APB2ENR     REG32(RCC_BASE + 0x18u)
#define   APB2ENR_IOPBEN    (1u << 3)
#define   APB2ENR_IOPCEN    (1u << 4)
#define RCC_APB1ENR     REG32(RCC_BASE + 0x1Cu)
#define   APB1ENR_I2C2EN    (1u << 22)
#define RCC_APB1RSTR    REG32(RCC_BASE + 0x10u)
#define   APB1RSTR_I2C2RST  (1u << 22)

/* ---- GPIO (RM0008 section 9.2) ------------------------------------------- */
typedef struct {
    volatile uint32_t CRL, CRH, IDR, ODR, BSRR, BRR, LCKR;
} gpio_t;
#define GPIOA           ((gpio_t *)0x40010800u)
#define GPIOB           ((gpio_t *)0x40010C00u)
#define GPIOC           ((gpio_t *)0x40011000u)

/* Pin modes, MODE[1:0] + CNF[1:0] packed as one nibble. */
#define GPIO_IN_FLOAT   0x4u
#define GPIO_OUT_PP     0x3u      /* push-pull, 50 MHz */
#define GPIO_OUT_OD     0x7u      /* open-drain, 50 MHz */
#define GPIO_AF_OD      0xFu      /* alternate open-drain, 50 MHz */

/* ---- I2C (RM0008 section 26.6) ------------------------------------------- */
typedef struct {
    volatile uint32_t CR1, CR2, OAR1, OAR2, DR, SR1, SR2, CCR, TRISE;
} i2c_t;
#define I2C2            ((i2c_t *)0x40005800u)
#define   I2C_CR1_PE        (1u << 0)
#define   I2C_CR1_START     (1u << 8)
#define   I2C_CR1_STOP      (1u << 9)
#define   I2C_CR1_ACK       (1u << 10)
#define   I2C_CR1_POS       (1u << 11)
#define   I2C_CR1_SWRST     (1u << 15)
#define   I2C_SR1_SB        (1u << 0)
#define   I2C_SR1_ADDR      (1u << 1)
#define   I2C_SR1_BTF       (1u << 2)
#define   I2C_SR1_RXNE      (1u << 6)
#define   I2C_SR1_TXE       (1u << 7)
#define   I2C_SR1_BERR      (1u << 8)
#define   I2C_SR1_ARLO      (1u << 9)
#define   I2C_SR1_AF        (1u << 10)
#define   I2C_SR2_BUSY      (1u << 1)
