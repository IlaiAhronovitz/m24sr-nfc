/* regs.h - STM32F103 registers this project touches (RM0008).
 *
 * Every register access in the project goes through the macros in this file:
 * the integer-to-pointer casts (MISRA Rule 11.4, clang-tidy
 * performance-no-int-to-ptr) and the inline assembly (MISRA Directive 4.3)
 * live here, once. See docs/deviations/target.md. */
#ifndef M24SR_NFC_REGS_H
#define M24SR_NFC_REGS_H

#include <stdint.h>

/* One 32-bit memory-mapped register at a fixed address. */
#define REG32(a)        (*(volatile uint32_t *)(uintptr_t)(a))  /* NOLINT(performance-no-int-to-ptr) -- T-1 */

/* Bit n of a 32-bit register (n = 0..31). */
#define BIT32(n)        ((uint32_t)1U << (n))

/* Inline assembly, encapsulated (Directive 4.3): interrupt mask and sleep. */
static inline void irq_off(void) { __asm volatile("cpsid i" ::: "memory"); }
static inline void irq_on(void)  { __asm volatile("cpsie i" ::: "memory"); }
static inline void cpu_wfi(void) { __asm volatile("wfi"); }

/* ---- Core ----------------------------------------------------------------- */
#define SCB_VTOR        REG32(0xE000ED08U)
#define SCB_CFSR        REG32(0xE000ED28U)
#define SYST_CSR        REG32(0xE000E010U)
#define SYST_RVR        REG32(0xE000E014U)
#define SYST_CVR        REG32(0xE000E018U)
#define DBGMCU_CR       REG32(0xE0042004U)
#define   DBGMCU_SLEEP      BIT32(0U)     /* debug access stays alive in WFI */

/* ---- RCC (RM0008 section 7.3) -------------------------------------------- */
#define RCC_BASE        0x40021000U
#define RCC_APB2ENR     REG32(RCC_BASE + 0x18U)
#define   APB2ENR_IOPBEN    BIT32(3U)
#define RCC_APB1ENR     REG32(RCC_BASE + 0x1CU)
#define   APB1ENR_I2C2EN    BIT32(22U)
#define RCC_APB1RSTR    REG32(RCC_BASE + 0x10U)
#define   APB1RSTR_I2C2RST  BIT32(22U)

/* ---- GPIO (RM0008 section 9.2) ------------------------------------------- */
typedef struct {
    volatile uint32_t CRL;
    volatile uint32_t CRH;
    volatile uint32_t IDR;
    volatile uint32_t ODR;
    volatile uint32_t BSRR;
    volatile uint32_t BRR;
    volatile uint32_t LCKR;
} gpio_t;
#define GPIOB           ((gpio_t *)0x40010C00U)  /* NOLINT(performance-no-int-to-ptr) -- T-1 */

/* Pin modes, MODE[1:0] + CNF[1:0] packed as one nibble. */
#define GPIO_OUT_OD     0x7U      /* open-drain, 50 MHz */
#define GPIO_AF_OD      0xFU      /* alternate open-drain, 50 MHz */

/* ---- I2C (RM0008 section 26.6) ------------------------------------------- */
typedef struct {
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t OAR1;
    volatile uint32_t OAR2;
    volatile uint32_t DR;
    volatile uint32_t SR1;
    volatile uint32_t SR2;
    volatile uint32_t CCR;
    volatile uint32_t TRISE;
} i2c_t;
#define I2C2            ((i2c_t *)0x40005800U)   /* NOLINT(performance-no-int-to-ptr) -- T-1 */
#define   I2C_CR1_PE        BIT32(0U)
#define   I2C_CR1_START     BIT32(8U)
#define   I2C_CR1_STOP      BIT32(9U)
#define   I2C_CR1_ACK       BIT32(10U)
#define   I2C_CR1_POS       BIT32(11U)
#define   I2C_CR1_SWRST     BIT32(15U)
#define   I2C_SR1_SB        BIT32(0U)
#define   I2C_SR1_ADDR      BIT32(1U)
#define   I2C_SR1_BTF       BIT32(2U)
#define   I2C_SR1_RXNE      BIT32(6U)
#define   I2C_SR1_TXE       BIT32(7U)
#define   I2C_SR1_BERR      BIT32(8U)
#define   I2C_SR1_ARLO      BIT32(9U)
#define   I2C_SR1_AF        BIT32(10U)
#define   I2C_SR2_BUSY      BIT32(1U)

#endif /* M24SR_NFC_REGS_H */
