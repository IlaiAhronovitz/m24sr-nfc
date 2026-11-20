/* system.c - SysTick millisecond tick and short delays on the HSI clock. */
#include "regs.h"
#include "system.h"

static volatile uint32_t s_ms;

/* Register accesses: docs/deviations/target.md, record T-1. */
/* NOLINTBEGIN(clang-analyzer-core.FixedAddressDereference) */

void SysTick_Handler(void)
{
    s_ms++;
}

void system_init(void)
{
    SCB_VTOR = (uint32_t)(uintptr_t)g_vectors;
    SYST_RVR = (SYSCLK_HZ / 1000U) - 1U;
    SYST_CVR = 0U;
    SYST_CSR = 7U;                       /* core clock, interrupt, enable */
}

uint32_t ms_now(void)
{
    return s_ms;
}

void delay_ms(uint32_t ms)
{
    uint32_t t0 = s_ms;
    while ((ms_now() - t0) < ms) {}
}

/* Counts SysTick down-counter ticks (8 per microsecond), across reloads. */
void delay_us(uint32_t us)
{
    uint32_t need = us * (SYSCLK_HZ / 1000000U);
    uint32_t last = SYST_CVR;
    uint32_t done = 0U;
    while (done < need) {
        uint32_t now = SYST_CVR;
        done += (last >= now) ? (last - now) : ((last + (SYST_RVR + 1U)) - now);
        last = now;
    }
}

/* NOLINTEND(clang-analyzer-core.FixedAddressDereference) */
