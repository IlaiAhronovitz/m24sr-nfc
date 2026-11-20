/* report.h - the block the host reads over SWD at 0x20000000. The debugger
 * on this board is an ST-LINK/V2 with no virtual COM port, so there is no
 * UART console: the firmware writes a status block and a text log here.
 * Keep in step with tools/report.py. */
#ifndef M24SR_NFC_REPORT_H
#define M24SR_NFC_REPORT_H

#include <stdint.h>

#define REPORT_MAGIC    0x4E33424DU   /* "MB3N" in memory order */
#define REPORT_VERSION  1U
#define REPORT_LOG_SIZE 1536U
#define REPORT_NDEF_MAX 512U

/* report_t.status values */
#define ST_RUNNING      1U
#define ST_DONE         2U
#define ST_FAILED       3U
#define ST_FAULT        4U

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t status;
    uint32_t step;
    int32_t  error;                     /* last error code, 0 if none */
    uint32_t last_sw;                   /* ISO 7816 status word */
    uint32_t i2c_errors;
    uint32_t i2c_nacks;
    uint32_t recover_pulses;
    uint32_t busy_polls;
    uint32_t wtx_count;
    uint32_t fault_pc;
    uint32_t fault_cfsr;
    uint32_t cc_len;
    uint32_t ndef_len;
    uint32_t log_len;
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

/* Entered from Fault_Handler in startup.s with the stacked exception frame. */
void fault_report(const uint32_t *frame);

#endif /* M24SR_NFC_REPORT_H */
