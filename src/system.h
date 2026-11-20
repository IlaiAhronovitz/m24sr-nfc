/* system.h - 8 MHz HSI (the reset clock, no crystal assumed), 1 ms tick. */
#ifndef M24SR_NFC_SYSTEM_H
#define M24SR_NFC_SYSTEM_H

#include <stdint.h>

#define SYSCLK_HZ 8000000U

/* The vector table of startup.s: 16 core entries (initial MSP .. SysTick). */
#define VECTOR_COUNT 16U
extern const uint32_t g_vectors[VECTOR_COUNT];

void     system_init(void);
uint32_t ms_now(void);
void     delay_ms(uint32_t ms);
void     delay_us(uint32_t us);

/* SysTick exception handler, entered from the vector table only. */
void     SysTick_Handler(void);

#endif /* M24SR_NFC_SYSTEM_H */
