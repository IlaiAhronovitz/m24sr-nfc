/* main.c - m24sr-nfc bring-up: take the I2C token, read the capability
 * container and the NDEF message the tag holds, decode its records into the
 * log, and give the token back so a phone can read the tag again.
 * Read-only: nothing is written to the tag in this step. */
#include "regs.h"
#include "system.h"
#include "i2c.h"
#include "m24sr.h"
#include "ndef.h"
#include "report.h"

static int bus_write(void *ctx, const uint8_t *buf, uint32_t len)
{
    (void)ctx;
    int rc = i2c2_write(M24SR_ADDR, buf, len);
    return rc == I2C_NACK ? M24SR_NACK : rc;
}

static int bus_read(void *ctx, uint8_t *buf, uint32_t len)
{
    (void)ctx;
    int rc = i2c2_read(M24SR_ADDR, buf, len);
    return rc == I2C_NACK ? M24SR_NACK : rc;
}

static void bus_delay(void *ctx, uint32_t ms)
{
    (void)ctx;
    delay_ms(ms);
}

static const m24sr_bus_t k_bus = { bus_write, bus_read, bus_delay, 0 };

static void log_records(const uint8_t *msg, uint32_t len)
{
    uint32_t pos = 0;
    ndef_record_t r;
    char a[160], b[16];
    while (pos < len) {
        int rc = ndef_parse_record(msg, len, &pos, &r);
        if (rc) {
            log_str("  record parse error ");
            log_dec(rc);
            log_str("\n");
            return;
        }
        log_str("  record: TNF ");
        log_dec(r.tnf);
        log_str(", type '");
        for (uint32_t i = 0; i < r.type_len && i < 32u; i++) {
            char t[2] = { (char)r.type[i], 0 };
            log_str(t);
        }
        log_str("', payload ");
        log_dec((int32_t)r.payload_len);
        log_str(" bytes");
        if (ndef_uri_to_string(&r, a, sizeof a) >= 0) {
            log_str(", URI ");
            log_str(a);
        } else if (ndef_text_get(&r, b, sizeof b, a, sizeof a) >= 0) {
            log_str(", text [");
            log_str(b);
            log_str("] ");
            log_str(a);
        }
        log_str("\n");
        if (r.me) break;
    }
}

static int step(int rc, const char *what, m24sr_t *d)
{
    g_report.step++;
    log_str(what);
    if (rc < 0) {
        log_str(": error ");
        log_dec(rc);
        log_str(", SW ");
        log_hex(d->last_sw, 4);
        log_str("\n");
        g_report.error = rc;
        g_report.last_sw = d->last_sw;
    } else {
        log_str(": ok\n");
    }
    return rc;
}

static int run(m24sr_t *d)
{
    m24sr_cc_t cc;
    uint8_t ccb[15];

    int rc = m24sr_get_session(d);
    if (rc == M24SR_NACK) {
        log_str("GetI2Csession refused (RF session open?), killing RF session\n");
        rc = m24sr_kill_rf_session(d);
    }
    if (step(rc, "I2C session", d)) return rc;
    if (step(m24sr_select_ndef_app(d), "select NDEF application", d)) return -1;
    if (step(m24sr_select_file(d, M24SR_FILE_CC), "select CC file E103", d)) return -1;
    if (step(m24sr_read_binary(d, 0, ccb, sizeof ccb), "read CC", d)) return -1;
    for (uint32_t i = 0; i < sizeof ccb; i++) g_report.cc[i] = ccb[i];
    g_report.cc_len = sizeof ccb;
    log_str("  CC: ");
    log_bytes(ccb, sizeof ccb);
    log_str("\n");
    if (step(m24sr_parse_cc(ccb, sizeof ccb, &cc), "parse CC", d)) return -1;
    log_str("  mapping version ");
    log_hex(cc.version, 2);
    log_str(", NDEF file ");
    log_hex(cc.ndef_file_id, 4);
    log_str(", max ");
    log_dec(cc.ndef_max);
    log_str(" bytes, MLe ");
    log_dec(cc.mle);
    log_str(", read/write access ");
    log_hex(cc.read_access, 2);
    log_str("/");
    log_hex(cc.write_access, 2);
    log_str("\n");

    rc = m24sr_read_ndef(d, cc.ndef_file_id, g_report.ndef, REPORT_NDEF_MAX);
    if (step(rc, "read NDEF message", d) < 0) return rc;
    g_report.ndef_len = (uint32_t)rc;
    log_str("  NLEN ");
    log_dec(rc);
    log_str("\n");
    log_records(g_report.ndef, (uint32_t)rc);

#ifdef WRITE_URI
    /* Replace the message with one URI record, then read it back. */
    static uint8_t msg[256], back[256];
    int n = ndef_build_uri(WRITE_URI, msg, sizeof msg);
    if (step(n, "build URI record", d) < 0) return n;
    log_str("  writing ");
    log_dec(n);
    log_str(" bytes: ");
    log_str(WRITE_URI);
    log_str("\n");
    if (step(m24sr_write_ndef(d, cc.ndef_file_id, msg, (uint32_t)n), "write NDEF message", d)) return -1;
    rc = m24sr_read_ndef(d, cc.ndef_file_id, back, sizeof back);
    if (step(rc, "read back", d) < 0) return rc;
    int same = rc == n;
    for (int i = 0; same && i < n; i++) same = back[i] == msg[i];
    if (step(same ? 0 : M24SR_ERR_FRAME, "verify", d)) return -1;
    for (int i = 0; i < n; i++) g_report.ndef[i] = back[i];
    g_report.ndef_len = (uint32_t)n;
    log_records(back, (uint32_t)n);
#endif
    return 0;
}

int main(void)
{
    report_init();
    DBGMCU_CR |= DBGMCU_SLEEP;
    system_init();
    log_str("m24sr-nfc bring-up, HSI 8 MHz, I2C2 100 kHz\n");

    g_report.recover_pulses = (uint32_t)i2c2_init();
    log_str("I2C2 up, bus recovery pulses ");
    log_dec((int32_t)g_report.recover_pulses);
    log_str("\n");

    m24sr_t d;
    m24sr_init(&d, &k_bus);
    int rc = run(&d);
    step(m24sr_deselect(&d), "deselect (token back to RF)", &d);

    g_report.i2c_errors = g_i2c_errors;
    g_report.i2c_nacks = g_i2c_nacks;
    g_report.busy_polls = d.busy_polls;
    g_report.wtx_count = d.wtx_count;
    g_report.status = rc < 0 ? ST_FAILED : ST_DONE;
    for (;;) __asm volatile("wfi");
}
