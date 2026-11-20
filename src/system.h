/* system.h - 8 MHz HSI (the reset clock, no crystal assumed), 1 ms tick. */
#pragma once
#include <stdint.h>

#define SYSCLK_HZ 8000000u

void     system_init(void);
uint32_t ms_now(void);
void     delay_ms(uint32_t ms);
void     delay_us(uint32_t us);
