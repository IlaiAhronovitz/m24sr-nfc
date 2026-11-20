/* report.c - the SWD-readable status block and a tiny text logger. */
#include "regs.h"
#include "report.h"

report_t g_report __attribute__((section(".report")));

void report_init(void)
{
    uint8_t *p = (uint8_t *)&g_report;
    for (uint32_t i = 0U; i < sizeof g_report; i++) {
        p[i] = 0U;
    }
    g_report.magic = REPORT_MAGIC;
    g_report.version = REPORT_VERSION;
    g_report.status = ST_RUNNING;
}

static void put(char c)
{
    if (g_report.log_len < (REPORT_LOG_SIZE - 1U)) {
        g_report.log[g_report.log_len] = c;
        g_report.log_len++;
    }
}

void log_str(const char *s)
{
    for (uint32_t i = 0U; s[i] != '\0'; i++) {
        put(s[i]);
    }
}

void log_hex(uint32_t v, unsigned digits)
{
    static const char hex[] = "0123456789ABCDEF";
    for (unsigned d = digits; d > 0U; d--) {
        put(hex[(v >> (4U * (d - 1U))) & 0xFU]);
    }
}

void log_dec(int32_t v)
{
    char tmp[11];
    unsigned n = 0U;
    uint32_t u = (v < 0) ? ((uint32_t)(-(v + 1)) + 1U) : (uint32_t)v;
    if (v < 0) {
        put('-');
    }
    do {
        tmp[n] = (char)('0' + (u % 10U));
        n++;
        u /= 10U;
    } while (u != 0U);
    while (n > 0U) {
        n--;
        put(tmp[n]);
    }
}

void log_bytes(const uint8_t *b, uint32_t n)
{
    for (uint32_t i = 0U; i < n; i++) {
        if (i != 0U) {
            put(' ');
        }
        log_hex(b[i], 2U);
    }
}

void fault_report(const uint32_t *frame)
{
    g_report.fault_pc = frame[6];
    /* NOLINTNEXTLINE(clang-analyzer-core.FixedAddressDereference) -- register, docs/deviations/target.md T-1 */
    g_report.fault_cfsr = SCB_CFSR;
    g_report.status = ST_FAULT;
    for (;;) {}
}
