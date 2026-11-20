/* report.h - the block the host reads over SWD at 0x20000000. The debugger
 * on this board is an ST-LINK/V2 with no virtual COM port, so there is no
 * UART console: the firmware writes a status block and a text log here.
 * Keep in step with tools/report.py. */
#pragma once
#include <stdint.h>

#define REPORT_MAGIC    0x4E33424Du   /* "MB3N" in memory order */
#define REPORT_VERSION  1u
#define REPORT_LOG_SIZE 1536u
#define REPORT_NDEF_MAX 512u

enum { ST_RUNNING = 1, ST_DONE = 2, ST_FAILED = 3, ST_FAULT = 4 };

typedef struct {
    uint32_t magic, version, status, step;
    int32_t  error;                     /* last error code, 0 if none */
    uint32_t last_sw;                   /* ISO 7816 status word */
    uint32_t i2c_errors, i2c_nacks, recover_pulses, busy_polls, wtx_count;
    uint32_t fault_pc, fault_cfsr;
    uint32_t cc_len, ndef_len, log_len;
    uint8_t  cc[16];
    uint8_t  ndef[REPORT_NDEF_MAX];
    char     log[REPORT_LOG_SIZE];
} report_t;

extern report_t g_report;

void report_init(void);
void log_str(const char *s);
void log_hex(uint32_t v, unsigned digits);
void log_dec(int32_t v);
void log_bytes(const uint8_t *b, uint32_t n);
