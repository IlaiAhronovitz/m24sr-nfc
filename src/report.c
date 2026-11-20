/* report.c - the SWD-readable status block and a tiny text logger. */
#include "regs.h"
#include "report.h"

report_t g_report __attribute__((section(".report")));

void report_init(void)
{
    uint8_t *p = (uint8_t *)&g_report;
    for (uint32_t i = 0; i < sizeof g_report; i++) p[i] = 0;
    g_report.magic = REPORT_MAGIC;
    g_report.version = REPORT_VERSION;
    g_report.status = ST_RUNNING;
}

static void put(char c)
{
    if (g_report.log_len < REPORT_LOG_SIZE - 1u) g_report.log[g_report.log_len++] = c;
}

void log_str(const char *s)
{
    while (*s) put(*s++);
}

void log_hex(uint32_t v, unsigned digits)
{
    static const char hex[] = "0123456789ABCDEF";
    while (digits--) put(hex[(v >> (4u * digits)) & 0xFu]);
}

void log_dec(int32_t v)
{
    char tmp[11];
    unsigned n = 0;
    uint32_t u = v < 0 ? (uint32_t)(-(v + 1)) + 1u : (uint32_t)v;
    if (v < 0) put('-');
    do { tmp[n++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    while (n) put(tmp[--n]);
}

void log_bytes(const uint8_t *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        if (i) put(' ');
        log_hex(b[i], 2);
    }
}

/* From Fault_Handler in startup.s, with the stacked exception frame. */
void fault_report(const uint32_t *frame)
{
    g_report.fault_pc = frame[6];
    g_report.fault_cfsr = SCB_CFSR;
    g_report.status = ST_FAULT;
    for (;;) {}
}
