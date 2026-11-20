/* test_lc.c - host tests for the Last Checkpoint record and AT parser.
 * Build and run: python build.py test (argv[1] = fixtures directory). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/lc/record.h"
#include "../../src/lc/at_parse.h"
#include "lc_test_util.h"

static int g_fail;
static int g_pass;

static void check(int ok, const char *file, int line, const char *expr)
{
    if (ok) {
        g_pass++;
    } else {
        g_fail++;
        printf("FAIL %s:%d: %s\n", file, line, expr);
    }
}

static void check_str(const char *got, const char *want, const char *file, int line)
{
    if (strcmp(got, want) == 0) {
        g_pass++;
    } else {
        g_fail++;
        printf("FAIL %s:%d:\n  got  \"%s\"\n  want \"%s\"\n", file, line, got, want);
    }
}

#define CHECK(cond) check((cond) != 0, __FILE__, __LINE__, #cond)
#define CHECK_STR(got, want) check_str((got), (want), __FILE__, __LINE__)

/* The record wrapped in guard bytes: the parser must never touch them. */
typedef struct {
    uint8_t pre[64];
    lc_record_t r;
    uint8_t post[64];
} guarded_t;

static void guard_init(guarded_t *g)
{
    mem_fill(g->pre, 0xA5, sizeof g->pre);
    mem_fill(g->post, 0x5A, sizeof g->post);
    lc_record_init(&g->r);
}

static int guard_ok(const guarded_t *g)
{
    unsigned i;
    for (i = 0; i < sizeof g->pre; i++) {
        if (g->pre[i] != 0xA5 || g->post[i] != 0x5A) {
            return 0;
        }
    }
    return 1;
}

static int roundtrip_equal(const lc_record_t *r)
{
    uint8_t bin[LC_BIN_MAX];
    lc_record_t back;
    int n = record_encode_bin(r, bin, sizeof bin);

    if (n <= 0 || n > LC_BIN_MAX) {
        return 0;
    }
    if (record_decode_bin(bin, (size_t)n, &back) != 0) {
        return 0;
    }
    return record_equal(r, &back);
}

/* ------------------------------------------------------------------------ */

static void test_crc16(void)
{
    /* CRC-16/CCITT-FALSE check value. */
    CHECK(lc_crc16((const uint8_t *)"123456789", 9) == 0x29B1U);
    CHECK(lc_crc16((const uint8_t *)"", 0) == 0xFFFFU);
}

static void test_init_unknown(void)
{
    lc_record_t r;
    char txt[LC_TEXT_MAX + 1];

    lc_record_init(&r);
    CHECK(r.emm.ns == LC_NS_UNKNOWN && r.emm.value == LC_U16_UNKNOWN);
    CHECK(r.esm.at_s == LC_U32_UNKNOWN);
    CHECK(r.attempts == LC_U16_UNKNOWN && r.backoff == LC_BO_UNKNOWN);
    CHECK(r.rsrp_dbm == LC_I16_UNKNOWN && r.rsrq_ddb == LC_I16_UNKNOWN);
    CHECK(r.snr_ddb == LC_I16_UNKNOWN && r.ce_level == LC_U8_UNKNOWN);
    CHECK(r.plmn.mcc == LC_U16_UNKNOWN && r.tac == LC_U32_UNKNOWN);
    CHECK(r.fplmn_count == LC_U8_UNKNOWN);
    CHECK(r.t3412_nw_s == LC_U32_UNKNOWN && r.edrx_act == LC_U8_UNKNOWN);
    CHECK(r.fw[0] == '\0' && r.battery_mv == LC_U16_UNKNOWN);
    CHECK(record_encode_text(&r, txt, sizeof txt) == 22);
    CHECK_STR(txt, "NO REGISTRATION STATUS");
    CHECK(roundtrip_equal(&r));
}

/* The example line from the task, reproduced exactly. */
static void make_example(lc_record_t *r)
{
    lc_record_init(r);
    r->reg_stat = LC_REG_DENIED;
    r->attempts = 3;
    r->rat = LC_RAT_LTE_M;
    lc_record_set_emm(r, LC_NS_EMM, 15, 15000);
    r->band = 20;
    r->rsrp_dbm = -112;
    r->t3412_req_s = 3600;
    r->t3324_req_s = 60;
    lc_record_set_fw(r, "0.3.1");
    lc_record_set_battery(r, 3610);
    r->uptime_s = (4 * 3600) + (12 * 60);
}

static void test_text_example(void)
{
    lc_record_t r;
    char txt[LC_TEXT_MAX + 1];
    int n;

    make_example(&r);
    CHECK(r.backoff == LC_BO_TA_FORBIDDEN);
    n = record_encode_text(&r, txt, sizeof txt);
    CHECK_STR(txt, "REGISTRATION DENIED x3 | EMM #15 No suitable cells in tracking area | "
                   "LTE-M B20 RSRP -112 dBm | PSM req 3600/60 s, granted - | fw 0.3.1 | "
                   "3.61 V | uptime 4h12m");
    CHECK(n == (int)strlen(txt));
}

static void fill_full(lc_record_t *r)
{
    unsigned i;

    lc_record_init(r);
    r->reg_stat = LC_REG_DENIED;
    r->reg_stat_s = 99999;
    r->attempts = 65000;
    r->rat = LC_RAT_LTE_M;
    lc_record_set_emm(r, LC_NS_EMM, 35, 123456);
    lc_record_set_esm(r, 66, 123457);
    r->last_success_s = 7;
    r->band = 255 - 1;
    r->plmn.mcc = 999; r->plmn.mnc = 999; r->plmn.mnc_digits = 3;
    r->tac = 0xFFFE;
    r->cell_id = 0x0FFFFFFF;
    r->rsrp_dbm = -141; r->rsrq_ddb = -195; r->snr_ddb = -1280;
    r->ce_level = 3;
    r->cell_s = 4000000000U;
    r->fplmn_count = LC_FPLMN_MAX;
    for (i = 0; i < LC_FPLMN_MAX; i++) {
        r->fplmn[i].mcc = (uint16_t)(100 + i);
        r->fplmn[i].mnc = (uint16_t)(200 + i);
        r->fplmn[i].mnc_digits = 3;
    }
    r->psm_mode_req = 1;
    r->t3412_req_s = 35712000U; r->t3324_req_s = LC_TIMER_DEACTIVATED;
    r->t3412_nw_s = 0;          r->t3324_nw_s = 0;     /* zero is a real value */
    r->edrx_act = 4; r->edrx_req = 15; r->edrx_nw = 0; r->ptw_nw = 15;
    lc_record_set_fw(r, "fw-version-23-chars-xyz-and-more");
    r->battery_mv = 65534;
    r->uptime_s = 4294967294U;
}

static void test_text_limits(void)
{
    lc_record_t r;
    char txt[LC_TEXT_MAX + 1];
    char small[30];
    char big[600];
    int n;
    int i;

    fill_full(&r);
    n = record_encode_text(&r, txt, sizeof txt);
    CHECK(n > 0 && n <= LC_TEXT_MAX && n == (int)strlen(txt));
    for (i = 0; i < n; i++) {
        CHECK(txt[i] >= 0x20 && txt[i] <= 0x7E);
    }
    /* The headline and the causes survive when the line is full. */
    CHECK(!strncmp(txt, "REGISTRATION DENIED x65000 | EMM #35 ", 37));
    CHECK(strstr(txt, "ESM #66 ") != NULL);

    /* A bigger buffer does not lift the 300-character limit. */
    n = record_encode_text(&r, big, sizeof big);
    CHECK(n <= LC_TEXT_MAX);

    /* A small buffer: whole segments only, NUL-terminated. */
    n = record_encode_text(&r, small, sizeof small);
    CHECK(n >= 0 && n < (int)sizeof small && (size_t)n == strlen(small));
    CHECK_STR(small, "REGISTRATION DENIED x65000");

    CHECK(record_encode_text(&r, small, 0) == -1);
    CHECK(record_encode_text(&r, NULL, 10) == -1);
    n = record_encode_text(&r, small, 1);
    CHECK(n == 0 && small[0] == '\0');
}

static void test_bin_roundtrip(void)
{
    lc_record_t r;
    lc_record_t back;
    uint8_t bin[LC_BIN_MAX];
    int n;

    make_example(&r);
    CHECK(roundtrip_equal(&r));
    fill_full(&r);
    n = record_encode_bin(&r, bin, sizeof bin);
    printf("lc: full record = %d bytes binary (LC_BIN_MAX %d)\n", n, LC_BIN_MAX);
    CHECK(n > 0 && n <= LC_BIN_MAX);
    CHECK(record_decode_bin(bin, (size_t)n, &back) == 0);
    CHECK(record_equal(&r, &back));
    CHECK(back.t3412_nw_s == 0 && back.t3324_req_s == LC_TIMER_DEACTIVATED);

    /* Too small an output buffer. */
    CHECK(record_encode_bin(&r, bin, (size_t)n - 1) == -1);
    CHECK(record_encode_bin(&r, bin, (size_t)n) == n);

    /* Header and version. */
    CHECK(bin[0] == 'L' && bin[1] == 'C' && bin[2] == LC_BIN_VERSION);
}

static void test_bin_corruption(void)
{
    lc_record_t r;
    lc_record_t back;
    uint8_t bin[LC_BIN_MAX];
    uint8_t bad[LC_BIN_MAX];
    int n;
    int i;
    int bit;
    int ok = 1;

    fill_full(&r);
    n = record_encode_bin(&r, bin, sizeof bin);

    /* Every single-bit flip is rejected (CRC-16 detects all of them), and
     * the output is then the all-unknown record. */
    for (i = 0; i < n; i++) {
        for (bit = 0; bit < 8; bit++) {
            mem_copy(bad, bin, (size_t)n);
            bad[i] ^= (uint8_t)(1U << bit);
            if (record_decode_bin(bad, (size_t)n, &back) == 0) {
                ok = 0;
            }
            if (back.emm.value != LC_U16_UNKNOWN || back.fplmn_count != LC_U8_UNKNOWN) {
                ok = 0;
            }
        }
    }
    CHECK(ok);

    /* Every truncation is rejected. */
    ok = 1;
    for (i = 0; i < n; i++) {
        if (record_decode_bin(bin, (size_t)i, &back) == 0) {
            ok = 0;
        }
    }
    CHECK(ok);

    mem_copy(bad, bin, (size_t)n);
    bad[2] = 2;
    CHECK(record_decode_bin(bad, (size_t)n, &back) == LC_BIN_EVERSION);
    bad[2] = LC_BIN_VERSION;
    bad[0] = 'X';
    CHECK(record_decode_bin(bad, (size_t)n, &back) == LC_BIN_EMAGIC);
    CHECK(record_decode_bin(NULL, 10, &back) == LC_BIN_ESHORT);
}

/* Builds a frame around hand-written TLVs, with a correct CRC. */
static size_t frame(const uint8_t *tlv, size_t n, uint8_t *out)
{
    uint16_t crc;
    out[0] = 'L'; out[1] = 'C'; out[2] = LC_BIN_VERSION;
    out[3] = (uint8_t)(n >> 8); out[4] = (uint8_t)n;
    mem_copy(&out[5], tlv, n);
    crc = lc_crc16(out, 5 + n);
    out[5 + n] = (uint8_t)(crc >> 8);
    out[6 + n] = (uint8_t)crc;
    return 7 + n;
}

static void test_bin_tlv_rules(void)
{
    uint8_t buf[64];
    lc_record_t r;
    size_t n;

    /* Unknown tags are skipped; LwM2M 4/2 (RSRP) is read. */
    {
        const uint8_t t[] = { 0xF0, 0x7F, 3, 1, 2, 3,  0x04, 0x02, 2, 0xFF, 0x90 };
        n = frame(t, sizeof t, buf);
        CHECK(record_decode_bin(buf, n, &r) == 0);
        CHECK(r.rsrp_dbm == -112);
    }
    /* A known tag with the wrong length. */
    {
        const uint8_t t[] = { 0x04, 0x02, 3, 0xFF, 0x90, 0 };
        n = frame(t, sizeof t, buf);
        CHECK(record_decode_bin(buf, n, &r) == LC_BIN_ETLV);
        CHECK(r.rsrp_dbm == LC_I16_UNKNOWN);
    }
    /* A TLV running past the end. */
    {
        const uint8_t t[] = { 0x03, 0x03, 9, '1', '.', '0' };
        n = frame(t, sizeof t, buf);
        CHECK(record_decode_bin(buf, n, &r) == LC_BIN_ETLV);
    }
    /* Out-of-range values. */
    {
        const uint8_t t[] = { 0x04, 0x0D, 1, 4 };              /* CE level 4 */
        n = frame(t, sizeof t, buf);
        CHECK(record_decode_bin(buf, n, &r) == LC_BIN_EVALUE);
    }
    {
        const uint8_t t[] = { 0xF0, 0x0B, 1, 5 };              /* 5 FPLMN entries */
        n = frame(t, sizeof t, buf);
        CHECK(record_decode_bin(buf, n, &r) == LC_BIN_EVALUE);
    }
    {
        const uint8_t t[] = { 0x03, 0x03, 2, '1', 0x07 };      /* control char in fw */
        n = frame(t, sizeof t, buf);
        CHECK(record_decode_bin(buf, n, &r) == LC_BIN_EVALUE);
    }
    /* An empty FPLMN list is a value, distinct from "not read". */
    {
        const uint8_t t[] = { 0xF0, 0x0B, 1, 0 };
        n = frame(t, sizeof t, buf);
        CHECK(record_decode_bin(buf, n, &r) == 0);
        CHECK(r.fplmn_count == 0);
    }
}

static void test_timers(void)
{
    /* TS 27.007 10.1.22 examples. */
    CHECK(at_gprs_timer2_s(0x24) == 240);          /* "00100100" = 4 min */
    CHECK(at_gprs_timer3_s(0x47) == 70U * 3600U);  /* "01000111" = 70 h */
    CHECK(at_gprs_timer2_s(0xE0) == LC_TIMER_DEACTIVATED);
    CHECK(at_gprs_timer3_s(0xE5) == LC_TIMER_DEACTIVATED);
    CHECK(at_gprs_timer2_s(0x00) == 0);            /* a real zero */
    CHECK(at_gprs_timer3_s(0xDF) == 31U * 320U * 3600U);
    CHECK(at_gprs_timer2_s(0x45) == 5 * 360);      /* decihours */
    CHECK(at_gprs_timer3_s(0x7F) == 31 * 2);
}

static int line(at_parser_t *p, lc_record_t *r, const char *s, uint32_t now)
{
    return at_parse_line(p, r, s, strlen(s), now);
}

/* The longest text in a cause table, scanning every possible value. */
static uint16_t longest_cause(const char *(*text)(uint16_t))
{
    uint32_t v;
    uint16_t best = 0;
    size_t best_len = 0;

    for (v = 0; v < 0xFFFFU; v++) {
        const char *t = text((uint16_t)v);
        if (t && strlen(t) >= best_len) {
            best_len = strlen(t);
            best = (uint16_t)v;
        }
    }
    return best;
}

/* Every field at its longest printable value. Values a binary record can
 * carry are allowed even where the parser would never produce them. */
static void fill_worst(lc_record_t *r, uint8_t stat)
{
    unsigned i;

    lc_record_init(r);
    r->reg_stat = stat;
    r->reg_stat_s = 4294967294U;
    r->attempts = 65534;
    r->rat = LC_RAT_OTHER;                                  /* "other RAT" */
    lc_record_set_emm(r, LC_NS_EMM, longest_cause(lc_emm_cause_text), 4294967294U);
    lc_record_set_esm(r, longest_cause(lc_esm_cause_text), 4294967294U);
    r->last_success_s = 4294967294U;
    r->band = 254;
    r->plmn.mcc = 999; r->plmn.mnc = 999; r->plmn.mnc_digits = 3;
    r->tac = 0xFFFF;
    r->cell_id = 0x0FFFFFFF;
    r->rsrp_dbm = -32767; r->rsrq_ddb = -32767; r->snr_ddb = -32767;
    r->ce_level = 3;
    r->cell_s = 4294967294U;
    r->fplmn_count = LC_FPLMN_MAX;
    for (i = 0; i < LC_FPLMN_MAX; i++) {
        r->fplmn[i].mcc = 999;
        r->fplmn[i].mnc = 999;
        r->fplmn[i].mnc_digits = 3;
    }
    r->psm_mode_req = 1;
    r->t3412_req_s = 4294967293U; r->t3324_req_s = 4294967293U;
    r->t3412_nw_s = 4294967293U;  r->t3324_nw_s = 4294967293U;
    r->edrx_act = 4; r->edrx_req = 15; r->edrx_nw = 15; r->ptw_nw = 15;
    lc_record_set_fw(r, "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW");  /* 23 kept */
    r->battery_mv = 65534;
    r->uptime_s = 4294967294U;
}

/* The 300-character limit in the worst case: every field filled with its
 * longest value, for every possible <stat>. The headline and both causes
 * must always survive; lower-priority segments may be left out whole. */
static void test_text_worst_case(void)
{
    lc_record_t r;
    char txt[600];
    unsigned stat;
    int n;
    int worst = 0;
    int ok = 1;

    for (stat = 0; stat <= 255; stat++) {
        fill_worst(&r, (uint8_t)stat);
        n = record_encode_text(&r, txt, sizeof txt);
        if (n < 0 || n > LC_TEXT_MAX || (size_t)n != strlen(txt)) {
            ok = 0;
        }
        if (!strstr(txt, " | EMM #83 Procedure cannot be completed") ||
            !strstr(txt, " | ESM #112 APN restriction value")) {
            ok = 0;
        }
        if (n > worst) {
            worst = n;
        }
    }
    CHECK(ok);
    CHECK(worst <= LC_TEXT_MAX);
    fill_worst(&r, LC_U8_UNKNOWN);
    n = record_encode_text(&r, txt, sizeof txt);
    printf("lc: worst-case text = %d chars (limit %d), longest over all <stat> = %d:\n    %s\n",
           n, LC_TEXT_MAX, worst, txt);
    CHECK(!strncmp(txt, "NO REGISTRATION STATUS x65534 | EMM #83 ", 40));
    CHECK(roundtrip_equal(&r));
}

/* Cause names checked against TS 24.301 V19.8.0 Tables 9.9.3.9.1/9.9.4.4.1. */
static void test_cause_tables(void)
{
    CHECK_STR(lc_emm_cause_text(8), "EPS services and non-EPS services not allowed");
    CHECK_STR(lc_emm_cause_text(36), "IAB-node operation not authorized");
    CHECK(lc_emm_cause_text(80) != NULL && lc_emm_cause_text(83) != NULL);
    CHECK(lc_emm_cause_text(1) == NULL && lc_emm_cause_text(4) == NULL);
    CHECK_STR(lc_esm_cause_text(29), "User authentication or authorization failed");
    CHECK_STR(lc_esm_cause_text(113), "Multiple accesses to a PDN connection not allowed");
    CHECK(lc_esm_cause_text(46) == NULL);          /* "Unused" in the table */
}

/* eDRX: TS 24.008 V19.5.0 Table 10.5.5.32, S1 mode, with its NOTEs 4-6. */
static void test_edrx_text(void)
{
    static const struct {
        uint8_t act, req, nw;
        const char *want;
    } cases[] = {
        { 4, 15, 14, "eDRX req 2621.44 s, granted 2621.44 s" },  /* WB: 111x -> 1101 */
        { 5,  0,  1, "eDRX req n/a, granted n/a" },              /* NB: 000x = none */
        { 5, 15,  4, "eDRX req 10485.76 s, granted 20.48 s" },   /* NB: 0100 -> 0010 */
        { 7,  0,  9, "eDRX req 5.12 s, granted 163.84 s" },      /* satellite WB-S1 */
        { 2,  5, LC_U8_UNKNOWN, "eDRX req code 0101, granted -" }, /* GSM: raw code */
    };
    lc_record_t r;
    char txt[LC_TEXT_MAX + 1];
    unsigned i;

    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        lc_record_init(&r);
        r.reg_stat = LC_REG_HOME;
        r.edrx_act = cases[i].act;
        r.edrx_req = cases[i].req;
        r.edrx_nw = cases[i].nw;
        record_encode_text(&r, txt, sizeof txt);
        CHECK(strstr(txt, cases[i].want) != NULL);
        if (!strstr(txt, cases[i].want)) {
            printf("  got \"%s\"\n", txt);
        }
    }
}

/* Lines that must not change the record at all. */
static void test_parser_hostile(void)
{
    static const struct {
        const char *s;
        size_t len;                     /* 0 = strlen */
        int pend;                       /* expect an FPLMN read first */
        int want;
    } bad[] = {
        /* Overlong numbers: more than 9 digits, or more hex digits than the field has. */
        { "+CEREG: 0,0000000001", 0, 0, AT_MALFORMED },
        { "+CEREG: 0,99999999999999999999", 0, 0, AT_MALFORMED },
        { "+CEREG: 5,3,\"1A2B\",\"01A2B3C\",7,0,4294967311", 0, 0, AT_MALFORMED },
        { "+CEREG: 3,\"12345\",\"01A2B3C\",7", 0, 0, AT_MALFORMED },
        { "+CEREG: 3,\"1A2B\",\"101A2B3C0\",7", 0, 0, AT_MALFORMED },
        { "+CEREG: 3,\"1A2B\",\"01A2B3C\",7,0,256", 0, 0, AT_MALFORMED },
        { "+CESQ: 99,99,255,255,1234567890,1", 0, 0, AT_MALFORMED },
        { "+CEDRXRDP: 4,\"01011\"", 0, 0, AT_MALFORMED },
        { "+CPSMS: 1,,,\"001000011\"", 0, 0, AT_MALFORMED },
        { "+CSIM: 99999999999,\"9000\"", 0, 1, AT_MALFORMED },
        { "+COPS: 0,2,\"0010101\",7", 0, 0, AT_MALFORMED },
        /* Negative values where the field is unsigned. */
        { "+CEREG: -1", 0, 0, AT_MALFORMED },
        { "+CEREG: 0,-1", 0, 0, AT_MALFORMED },
        { "+CEREG: 3,\"1A2B\",\"01A2B3C\",-7", 0, 0, AT_MALFORMED },
        { "+CEREG: 3,\"1A2B\",\"01A2B3C\",7,0,-15", 0, 0, AT_MALFORMED },
        { "+CESQ: 99,99,255,255,-1,-1", 0, 0, AT_MALFORMED },
        { "+CEDRXRDP: -4,\"0101\"", 0, 0, AT_MALFORMED },
        { "+CPSMS: -1", 0, 0, AT_MALFORMED },
        { "+COPS: -1,2,\"00101\"", 0, 0, AT_MALFORMED },
        { "+CRSM: -112,0,\"00F110\"", 0, 1, AT_MALFORMED },
        { "+CSIM: -16,\"00F1109000\"", 0, 1, AT_MALFORMED },
        { "%CEER: \"EMM\",\"ATTACH\",\"REJECT\",-15", 0, 0, AT_IGNORED },
        { "%MEAS: Signal Quality: RSRP = --95", 0, 0, AT_MALFORMED },
        { "+CEREG: --1", 0, 0, AT_MALFORMED },
        { "+CEREG: -", 0, 0, AT_MALFORMED },
        /* Embedded NUL bytes (explicit lengths). */
        { "+CE\0REG: 1", 10, 0, AT_UNKNOWN },
        { "+CEREG: 0,\0", 11, 0, AT_MALFORMED },
        { "+CEREG: 3,\"1A\0B\",\"01A2B3C\",7", 28, 0, AT_MALFORMED },
        { "+CRSM: 144,0,\"00F1" "\0" "00F110\"", 26, 1, AT_MALFORMED },
        { "%MEAS: Signal Quality: RSRP = -9\0", 33, 0, AT_MALFORMED },
        { "\0\0\0\0", 4, 0, AT_UNKNOWN },
        /* Unterminated quotes. */
        { "+CEREG: 3,\"1A2B", 0, 0, AT_MALFORMED },
        { "+CRSM: 144,0,\"00F110", 0, 1, AT_MALFORMED },
        /* Hex that is not BCD, and "empty" slots that are not FFFFFF. */
        { "+CRSM: 144,0,\"AAAAAA\"", 0, 1, AT_MALFORMED },
        { "+CRSM: 144,0,\"00F110FFFF0F\"", 0, 1, AT_MALFORMED },
        { "+CSIM: 16,\"00F1A0FFFFFF9000\"", 0, 1, AT_MALFORMED },
        /* A TS 24.301 cause is one octet. */
        { "%CEER: \"EMM\",\"ATTACH\",\"REJECT\",300", 0, 0, AT_MALFORMED },
        { "%CEER: 1,\"ESM\",\"PDN\",\"REJECT\",256", 0, 0, AT_MALFORMED },
    };
    at_parser_t p;
    guarded_t g;
    lc_record_t before;
    unsigned i;

    guard_init(&g);
    at_parser_init(&p);
    before = g.r;
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        size_t len = bad[i].len ? bad[i].len : strlen(bad[i].s);
        int rc;
        int same;
        if (bad[i].pend) {
            at_parse_expect(&p, AT_PEND_FPLMN);
        }
        rc = at_parse_line(&p, &g.r, bad[i].s, len, 50);
        same = record_equal(&before, &g.r);
        if (rc != bad[i].want || !same) {
            printf("FAIL hostile line %u: rc %d, want %d, record %s\n", i, rc, bad[i].want,
                   same ? "unchanged" : "CHANGED");
            g_fail++;
        } else {
            g_pass++;
        }
    }
    CHECK(guard_ok(&g));
}

/* Stream edge cases, and the satellite AcT-types of +CEDRXRDP. */
static void test_parser_stream_edges(void)
{
    at_parser_t p;

    at_parser_init(&p);
    /* A stream with no terminator: nothing is parsed until one arrives. */
    {
        at_parser_t q;
        lc_record_t r2;
        char big[600];
        at_parser_init(&q);
        lc_record_init(&r2);
        at_parse_feed(&q, &r2, "+CEREG: 0,1", 11, 1);
        CHECK(r2.reg_stat == LC_U8_UNKNOWN && q.len == 11 && q.lines == 0);
        at_parse_feed(&q, &r2, "\r", 1, 2);
        CHECK(r2.reg_stat == 1 && q.len == 0);

        /* 600 bytes and no terminator: the buffer stays bounded, and the
         * line is counted once its terminator comes. */
        mem_fill(big, '7', sizeof big);
        at_parse_feed(&q, &r2, big, sizeof big, 3);
        CHECK(q.len <= AT_LINE_MAX && q.discarding && q.too_long == 0);
        at_parse_feed(&q, &r2, "\n+CEREG: 0,5\n", 13, 4);
        CHECK(q.too_long == 1 && r2.reg_stat == 5 && q.len == 0);

        /* A NUL inside a streamed line is an ordinary byte. */
        at_parse_feed(&q, &r2, "+CEREG: 0,2\0\r\n", 14, 5);
        CHECK(r2.reg_stat == 5 && q.malformed == 1);
    }

    /* Satellite AcT-types of TS 27.007 V19.6.0 clause 7.41 are accepted. */
    {
        lc_record_t r3;
        lc_record_init(&r3);
        CHECK(line(&p, &r3, "+CEDRXRDP: 7,\"0101\",\"0101\",\"0001\"", 60) == AT_UPDATED);
        CHECK(r3.edrx_act == 7 && r3.rat == LC_RAT_LTE_M);
        CHECK(line(&p, &r3, "+CEDRXRDP: 10", 61) == AT_MALFORMED);
    }
}

/* Decoder edge cases found in review. */
static void test_bin_review(void)
{
    uint8_t buf[96];
    lc_record_t r;
    size_t n;

    /* A repeated FPLMN TLV with a shorter list leaves no stale entries. */
    {
        const uint8_t t[] = {
            0xF0, 0x0B, 11, 2, 0, 1, 0, 1, 2, 0, 1, 0, 1, 3,
            0xF0, 0x0B, 6, 1, 3, 0xE7, 0, 99, 2
        };
        n = frame(t, sizeof t, buf);
        CHECK(record_decode_bin(buf, n, &r) == 0);
        CHECK(r.fplmn_count == 1 && r.fplmn[0].mcc == 999 && r.fplmn[1].mcc == LC_U16_UNKNOWN);
        CHECK(roundtrip_equal(&r));
    }
    /* The E-UTRAN cell identity has 28 bits. */
    {
        const uint8_t t[] = { 0x04, 0x08, 4, 0x10, 0, 0, 0 };
        n = frame(t, sizeof t, buf);
        CHECK(record_decode_bin(buf, n, &r) == LC_BIN_EVALUE);
    }
    /* eDRX AcT-type 9 (satellite NG-RAN) is valid, 10 is not. */
    {
        uint8_t t[] = { 0xF0, 0x0F, 4, 9, 5, 5, 1 };
        n = frame(t, sizeof t, buf);
        CHECK(record_decode_bin(buf, n, &r) == 0 && r.edrx_act == 9);
        t[3] = 10;
        n = frame(t, sizeof t, buf);
        CHECK(record_decode_bin(buf, n, &r) == LC_BIN_EVALUE);
    }
}

/* Regression tests for the findings of the second code review. */
static void test_review_b(void)
{
    uint8_t buf[64];
    lc_record_t r;
    lc_record_t before;
    at_parser_t p;
    char txt[LC_TEXT_MAX + 1];
    size_t n;

    /* A bad FPLMN answer leaves the previous list in place. */
    lc_record_init(&r);
    at_parser_init(&p);
    at_parse_expect(&p, AT_PEND_FPLMN);
    CHECK(line(&p, &r, "+CRSM: 144,0,\"00F110FFFFFF\"", 1) == AT_UPDATED);
    CHECK(r.fplmn_count == 1);
    before = r;
    at_parse_expect(&p, AT_PEND_FPLMN);
    CHECK(line(&p, &r, "+CRSM: 144,0,\"AAAAAA\"", 2) == AT_MALFORMED);
    CHECK(record_equal(&before, &r));

    /* Status 4 is "unknown", not "no coverage", and differs from no status. */
    lc_record_init(&r);
    CHECK(line(&p, &r, "+CEREG: 4", 3) == AT_UPDATED);
    record_encode_text(&r, txt, sizeof txt);
    CHECK(!strncmp(txt, "REG STATUS UNKNOWN", 18));

    /* MNC with no digit count: below 100 it is marked ambiguous. */
    {
        const uint8_t t[] = { 0x04, 0x0A, 2, 0, 1,  0x04, 0x09, 2, 0, 1 };
        n = frame(t, sizeof t, buf);
        CHECK(record_decode_bin(buf, n, &r) == 0 && r.plmn.mnc_digits == LC_U8_UNKNOWN);
        record_encode_text(&r, txt, sizeof txt);
        CHECK(strstr(txt, "PLMN 001-01?") != NULL);
    }
    {
        const uint8_t t[] = { 0x04, 0x0A, 2, 0, 1,  0x04, 0x09, 2, 0, 150 };
        n = frame(t, sizeof t, buf);
        CHECK(record_decode_bin(buf, n, &r) == 0);
        record_encode_text(&r, txt, sizeof txt);
        CHECK(strstr(txt, "PLMN 001-150") != NULL);
        CHECK(strstr(txt, "?") == NULL);
    }
    /* A 2-digit MNC above 99 is a contradiction, in either TLV order. */
    {
        const uint8_t t[] = { 0x04, 0x09, 2, 0, 150,  0xF0, 0x09, 1, 2 };
        n = frame(t, sizeof t, buf);
        CHECK(record_decode_bin(buf, n, &r) == LC_BIN_EVALUE);
        CHECK(r.plmn.mnc == LC_U16_UNKNOWN);
    }
    {
        const uint8_t t[] = { 0xF0, 0x0B, 6, 1, 0, 1, 0, 150, 2 };
        n = frame(t, sizeof t, buf);
        CHECK(record_decode_bin(buf, n, &r) == LC_BIN_EVALUE);
    }

    /* ESM #29 carries the Annex B wording. */
    lc_record_init(&r);
    CHECK(line(&p, &r, "%CEER: \"ESM\",\"PDN\",\"REJECT\",29", 4) == AT_UPDATED);
    record_encode_text(&r, txt, sizeof txt);
    CHECK(strstr(txt, "ESM #29 User authentication or authorization failed") != NULL);
}

/* Lines that reach the remaining branches of at_parse.c. Each runs on a
 * fresh parser and record. Syntax: TS 27.007 V19.6.0 (+CEREG 10.1.22,
 * +CEDRXRDP 7.41, +CPSMS 7.38, +COPS 7.3, +CSIM 8.17, +CRSM 8.18) and the
 * Murata Type 1SC guide rev 1.1 (%MEAS 3.3.1, %CEER 3.3.35). */
static void test_at_branches(void)
{
    static const struct {
        const char *s;
        int pend;                       /* expect an FPLMN read first */
        int want;
    } cases[] = {
        { "+CEREG: 3,\"1a2b\",\"01a2b3c\",7", 0, AT_UPDATED },       /* lower-case hex */
        { "+CEREG: 3,\"1A2B\"  ,\"01A2B3C\",7", 0, AT_UPDATED },     /* blanks after a quote */
        { "+CEREG: 3,\"1A2B\"x,\"01A2B3C\",7", 0, AT_MALFORMED },    /* text after a quote */
        { "+CEREG: 0 ,1", 0, AT_UPDATED },                           /* blank before a comma */
        { "+CPSMS: ", 0, AT_MALFORMED },                             /* empty <mode> */
        { "+CEREG: 3,,,7,2", 0, AT_MALFORMED },                      /* <cause_type> 2 */
        { "+CEREG: 1,,,,,,\"0010\"", 0, AT_MALFORMED },               /* Active-Time not 8 bits */
        { "+CEREG: 10,1", 0, AT_MALFORMED },                         /* <n> above 9 */
        { "%MEAS: Signal Quality: RSRP -95", 0, AT_MALFORMED },      /* no '=' */
        { "%MEAS: Signal Quality: RSRP = N/A, RSRQ = -11", 0, AT_UPDATED },
        { "%MEAS: Signal Quality: RSSI = -70", 0, AT_IGNORED },      /* nothing stored */
        { "%MEAS: Signal Quality: \"RSRP = -95", 0, AT_MALFORMED },   /* open quote */
        { "+CEDRXRDP: 4,\"0101\",\"01\"", 0, AT_MALFORMED },
        { "+CEDRXRDP: 4,\"0101\",\"0101\",\"1\"", 0, AT_MALFORMED },
        { "+CPSMS: 1,,,\"00100001\",\"0010\"", 0, AT_MALFORMED },
        { "+CPSMS: 1,,,\"0010000X\"", 0, AT_MALFORMED },          /* not a bit */
        { "+COPS: 0,2,\"00A01\"", 0, AT_MALFORMED },
        { "+COPS: 0,3,\"x\"", 0, AT_MALFORMED },
        { "+COPS: 0,2,\"00101\",256", 0, AT_MALFORMED },
        { "+CRSM: 144,0,\"00G110\"", 1, AT_MALFORMED },              /* not hex */
        { "+CRSM: 144,0", 1, AT_MALFORMED },                         /* no data */
        { "+CSIM: 4,\"90G0\"", 1, AT_MALFORMED },                    /* status word not hex */
        { "+CSIM: 4,\"9000\"", 0, AT_IGNORED },                      /* no read pending */
        { "+CSIM: 4,\"6A82\"", 1, AT_IGNORED },                      /* SIM refused */
        { "%CEER: \"EMM\",\"ATTACH\"", 0, AT_MALFORMED },             /* too few parameters */
        { "%CEER: \"emm\",\"ATTACH\",\"REJECT\",15", 0, AT_UPDATED }, /* module, any case */
    };
    unsigned i;

    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        at_parser_t p;
        guarded_t g;
        int rc;

        at_parser_init(&p);
        guard_init(&g);
        if (cases[i].pend) {
            at_parse_expect(&p, AT_PEND_FPLMN);
        }
        rc = line(&p, &g.r, cases[i].s, 1);
        if (rc != cases[i].want) {
            printf("  case %u \"%s\": rc %d\n", i, cases[i].s, rc);
        }
        CHECK(rc == cases[i].want);
        CHECK(guard_ok(&g));
    }
}

/* <AcT> groups of TS 27.007 10.1.22, and the RAT rules of the parser. */
static void test_at_rat(void)
{
    static const struct {
        const char *s;
        uint8_t rat;
    } acts[] = {
        { "+CEREG: 1,\"0001\",\"00000001\",0", LC_RAT_GSM },        /* GSM */
        { "+CEREG: 1,\"0001\",\"00000001\",15", LC_RAT_LTE_M },     /* WB-S1 via 5GCN */
        { "+CEREG: 1,\"0001\",\"00000001\",11", LC_RAT_NR },        /* NR via 5GCN */
        { "+CEREG: 1,\"0001\",\"00000001\",2", LC_RAT_OTHER },      /* UTRAN */
    };
    at_parser_t p;
    lc_record_t r;
    unsigned i;

    for (i = 0; i < sizeof acts / sizeof acts[0]; i++) {
        at_parser_init(&p);
        lc_record_init(&r);
        CHECK(line(&p, &r, acts[i].s, 1) == AT_UPDATED);
        CHECK(r.rat == acts[i].rat);
    }
    /* LTE-M learnt from +CEDRXRDP survives a later "E-UTRAN" (AcT 7). */
    at_parser_init(&p);
    lc_record_init(&r);
    CHECK(line(&p, &r, "+CEDRXRDP: 4,\"0101\",\"0101\",\"0001\"", 1) == AT_UPDATED);
    CHECK(line(&p, &r, "+CEREG: 1,\"0001\",\"00000001\",7", 2) == AT_UPDATED);
    CHECK(r.rat == LC_RAT_LTE_M);
}

/* Cause stamping, the attempt counter's ceiling, and NULL arguments. */
static void test_at_state(void)
{
    at_parser_t p;
    lc_record_t r;

    /* The same cause while searching is not new: its stamp stays. */
    at_parser_init(&p);
    lc_record_init(&r);
    CHECK(line(&p, &r, "+CEREG: 3,\"0001\",\"00000001\",7,0,15", 10) == AT_UPDATED);
    CHECK(line(&p, &r, "+CEREG: 2,\"0001\",\"00000001\",7,0,15", 20) == AT_UPDATED);
    CHECK(r.emm.value == 15 && r.emm.at_s == 10 && r.backoff == LC_BO_TA_FORBIDDEN);

    /* The attempt counter stops at 0xFFFE (0xFFFF is "unknown"). */
    lc_record_init(&r);
    r.attempts = 0xFFFE;
    r.reg_stat = LC_REG_HOME;
    CHECK(line(&p, &r, "+CEREG: 3", 30) == AT_UPDATED);
    CHECK(r.attempts == 0xFFFE && r.backoff == LC_BO_RETRY);

    /* NULL arguments are refused, and nothing is touched. */
    CHECK(at_parse_line(NULL, &r, "OK", 2, 0) == AT_MALFORMED);
    CHECK(at_parse_line(&p, NULL, "OK", 2, 0) == AT_MALFORMED);
    CHECK(at_parse_line(&p, &r, NULL, 1, 0) == AT_MALFORMED);
    at_parse_feed(NULL, &r, "OK\r", 3, 0);
    at_parse_feed(&p, NULL, "OK\r", 3, 0);
    at_parse_feed(&p, &r, NULL, 3, 0);
    CHECK(p.lines == 3 && p.malformed == 0);
}

static void test_parser_units(void)
{
    at_parser_t p;
    guarded_t g;

    at_parser_init(&p);
    guard_init(&g);

    /* Read response with n=0: only <n>,<stat>. */
    CHECK(line(&p, &g.r, "+CEREG: 0,1", 5) == AT_UPDATED);
    CHECK(g.r.reg_stat == 1 && g.r.attempts == 0 && g.r.backoff == LC_BO_NONE);
    CHECK(g.r.last_success_s == 5 && g.r.tac == LC_U32_UNKNOWN);

    /* URC, stat only. */
    CHECK(line(&p, &g.r, "+CEREG: 3", 6) == AT_UPDATED);
    CHECK(g.r.reg_stat == 3 && g.r.attempts == 1 && g.r.backoff == LC_BO_RETRY);

    /* Manufacturer-specific cause_type 1. */
    CHECK(line(&p, &g.r, "+CEREG: 3,\"0001\",\"00000002\",7,1,1234", 7) == AT_UPDATED);
    CHECK(g.r.emm.ns == LC_NS_VENDOR && g.r.emm.value == 1234 && g.r.emm.at_s == 7);
    CHECK(g.r.attempts == 1);      /* still denied: not a new attempt */

    /* EMM #7: USIM invalid until power-off. */
    CHECK(line(&p, &g.r, "+CEREG: 0", 8) == AT_UPDATED);
    CHECK(line(&p, &g.r, "+CEREG: 5,3,\"0001\",\"00000002\",7,0,7", 9) == AT_UPDATED);
    CHECK(g.r.attempts == 2 && g.r.backoff == LC_BO_SIM_INVALID);
    CHECK(g.r.emm.ns == LC_NS_EMM && g.r.emm.value == 7 && g.r.emm.rat == LC_RAT_LTE);

    /* ci FFFFFFFF = not available, not stored. */
    CHECK(line(&p, &g.r, "+CEREG: 2,\"0002\",\"FFFFFFFF\",9", 10) == AT_UPDATED);
    CHECK(g.r.tac == 2 && g.r.cell_id == 2 && g.r.rat == LC_RAT_NB_IOT);

    /* +CESQ 255 keeps the last known value; index 0 is a value, not unknown. */
    CHECK(line(&p, &g.r, "+CESQ: 99,99,255,255,0,0", 11) == AT_UPDATED);
    CHECK(g.r.rsrq_ddb == -200 && g.r.rsrp_dbm == -141);
    CHECK(line(&p, &g.r, "+CESQ: 99,99,255,255,255,255", 12) == AT_IGNORED);
    CHECK(g.r.rsrq_ddb == -200 && g.r.rsrp_dbm == -141 && g.r.cell_s == 11);
    CHECK(line(&p, &g.r, "+CESQ: 99,99,255,255,34,97", 13) == AT_UPDATED);
    CHECK(g.r.rsrq_ddb == -30 && g.r.rsrp_dbm == -44);
    CHECK(guard_ok(&g));
}

static void test_parser_units_b(void)
{
    at_parser_t p;
    guarded_t g;
    char buf[AT_LINE_MAX + 2];

    at_parser_init(&p);
    guard_init(&g);

    /* +COPS 3-digit MNC, and non-numeric formats are ignored. */
    CHECK(line(&p, &g.r, "+COPS: 1,2,\"001001\",9", 14) == AT_UPDATED);
    CHECK(g.r.plmn.mcc == 1 && g.r.plmn.mnc == 1 && g.r.plmn.mnc_digits == 3);
    CHECK(line(&p, &g.r, "+COPS: 0,0,\"Some Net\",7", 15) == AT_IGNORED);
    CHECK(line(&p, &g.r, "+COPS: 2", 15) == AT_IGNORED);

    /* +CPSMS with no requested timers: unknown, not zero. */
    CHECK(line(&p, &g.r, "+CPSMS: 0", 16) == AT_UPDATED);
    CHECK(g.r.psm_mode_req == 0 && g.r.t3412_req_s == LC_U32_UNKNOWN);

    /* FPLMN through +CSIM (READ BINARY answer + status word). */
    at_parse_expect(&p, AT_PEND_FPLMN);
    CHECK(line(&p, &g.r, "+CSIM: 16,\"99F999FFFFFF9000\"", 17) == AT_UPDATED);
    CHECK(g.r.fplmn_count == 1 && g.r.fplmn[0].mcc == 999 && g.r.fplmn[0].mnc == 99);
    CHECK(p.pending == AT_PEND_NONE);
    /* A later, empty read gives count 0 and clears the stale entry. */
    at_parse_expect(&p, AT_PEND_FPLMN);
    CHECK(line(&p, &g.r, "+CRSM: 144,0,\"FFFFFFFFFFFF\"", 18) == AT_UPDATED);
    CHECK(g.r.fplmn_count == 0 && g.r.fplmn[0].mcc == LC_U16_UNKNOWN);
    /* +CSIM length must match. */
    at_parse_expect(&p, AT_PEND_FPLMN);
    CHECK(line(&p, &g.r, "+CSIM: 10,\"FFFFFF9000\"", 19) == AT_UPDATED);
    at_parse_expect(&p, AT_PEND_FPLMN);
    CHECK(line(&p, &g.r, "+CSIM: 12,\"FFFFFF9000\"", 19) == AT_MALFORMED);

    /* Leading blanks and a trailing CR/LF are tolerated. */
    CHECK(line(&p, &g.r, "  +CEREG: 1\r\n", 20) == AT_UPDATED);
    CHECK(g.r.reg_stat == 1);

    /* %CEER read form with ESM module. */
    CHECK(line(&p, &g.r, "%CEER: 1,\"ESM\",\"PDN\",\"REJECT\",33", 21) == AT_UPDATED);
    CHECK(g.r.esm.ns == LC_NS_ESM && g.r.esm.value == 33 && g.r.esm.at_s == 21);

    /* Exactly AT_LINE_MAX bytes is parsed (here: unknown); one more is dropped. */
    mem_fill(buf, 'X', sizeof buf);
    CHECK(at_parse_line(&p, &g.r, buf, AT_LINE_MAX, 22) == AT_UNKNOWN);
    CHECK(at_parse_line(&p, &g.r, buf, AT_LINE_MAX + 1, 22) == AT_TOO_LONG);
    CHECK(p.too_long == 1);

    /* Stream path: a long line split over calls, then a good line. */
    {
        at_parser_t q;
        lc_record_t r2;
        at_parser_init(&q);
        lc_record_init(&r2);
        at_parse_feed(&q, &r2, buf, 200, 1);
        at_parse_feed(&q, &r2, buf, 200, 1);
        at_parse_feed(&q, &r2, "\r\n+CEREG: 0,5\r", 14, 1);
        CHECK(q.too_long == 1 && r2.reg_stat == 5);
        at_parse_feed(&q, &r2, buf, AT_LINE_MAX, 2);       /* exactly 256 */
        at_parse_feed(&q, &r2, "\n", 1, 2);
        CHECK(q.too_long == 1 && q.unknown == 1);
    }

    /* NUL bytes inside a line are ordinary bytes. */
    CHECK(at_parse_line(&p, &g.r, "+CEREG: 1\0garbage", 17, 23) == AT_MALFORMED);
    CHECK(at_parse_line(&p, &g.r, NULL, 0, 23) == AT_IGNORED);

    CHECK(guard_ok(&g));
}

/* ------------------------------------------------------------------------ */
/* Q-LC: record.c paths the other tests do not reach                        */
/* ------------------------------------------------------------------------ */

static void test_record_q_setters(void)
{
    lc_record_t r;

    lc_record_init(&r);
    lc_record_set_fw(&r, "\x01" "ab\x7F");     /* non-printable -> '?' */
    CHECK_STR(r.fw, "?ab?");
    lc_record_set_ce_level(&r, 2);
    CHECK(r.ce_level == 2);
    lc_record_set_ce_level(&r, 4);              /* CE levels are 0..3 */
    CHECK(r.ce_level == LC_U8_UNKNOWN);

    /* TS 24.301 5.5.1.2.5, as simplified in record.h. */
    CHECK(lc_backoff_for_emm(3) == LC_BO_SIM_INVALID);
    CHECK(lc_backoff_for_emm(6) == LC_BO_SIM_INVALID);
    CHECK(lc_backoff_for_emm(11) == LC_BO_PLMN_FORBIDDEN);
    CHECK(lc_backoff_for_emm(14) == LC_BO_PLMN_FORBIDDEN);
    CHECK(lc_backoff_for_emm(12) == LC_BO_TA_FORBIDDEN);
    CHECK(lc_backoff_for_emm(13) == LC_BO_TA_FORBIDDEN);
    CHECK(lc_backoff_for_emm(22) == LC_BO_CONGESTION);
    CHECK(lc_backoff_for_emm(99) == LC_BO_RETRY);
}

static void test_record_q_text(void)
{
    lc_record_t r;
    char txt[LC_TEXT_MAX + 1];

    lc_record_init(&r);
    lc_record_set_emm(&r, LC_NS_VENDOR, 1234, 5);
    r.esm.value = 7;                            /* namespace unknown */
    r.rat = LC_RAT_GSM;
    r.plmn.mnc = 5;                             /* MCC and digit count unknown */
    r.psm_mode_req = 1;
    r.t3412_req_s = 60;                         /* T3324 unknown */
    r.fplmn_count = 0;
    CHECK(record_encode_text(&r, txt, sizeof txt) > 0);
    CHECK_STR(txt, "NO REGISTRATION STATUS | vendor cause #1234 | ESM? #7 | GSM | "
                   "PLMN --05? | PSM req 60/- s, granted - | FPLMN none | uptime 0h00m");

    r.emm.ns = LC_NS_UNKNOWN;
    r.rat = LC_RAT_NR;
    r.plmn.mcc = 1;
    r.plmn.mnc = LC_U16_UNKNOWN;
    r.psm_mode_req = 0;
    CHECK(record_encode_text(&r, txt, sizeof txt) > 0);
    CHECK(strstr(txt, " | cause #1234 | ") != NULL);
    CHECK(strstr(txt, " | NR | PLMN 001-- | PSM req off, granted - | ") != NULL);

    CHECK(record_encode_text(NULL, txt, sizeof txt) == 0 && txt[0] == '\0');
}

static void test_record_q_bin(void)
{
    static const uint8_t fw0[] = { 0x03, 0x03, 0 };
    static const uint8_t cause7[] = { 0xF0, 0x01, 7, 1, 2, 0, 3, 0, 0, 0 };
    static const uint8_t reg4[] = { 0xF0, 0x03, 4, 1, 0, 0, 0 };
    static const uint8_t fplmn0[] = { 0xF0, 0x0B, 0 };
    static const uint8_t fplmn_short[] = { 0xF0, 0x0B, 2, 1, 0 };
    static const uint8_t edrx3[] = { 0xF0, 0x0F, 3, 4, 5, 5 };
    static const uint8_t half_hdr[] = { 0xF0, 0x10 };
    uint8_t tlv[3 + 24];
    uint8_t buf[64];
    lc_record_t r;
    size_t n;

    fill_full(&r);
    CHECK(record_encode_bin(NULL, buf, sizeof buf) == -1);
    CHECK(record_encode_bin(&r, buf, 10) == -1);  /* cap too small */
    CHECK(record_decode_bin(buf, sizeof buf, NULL) == LC_BIN_ESHORT);

    /* A known tag with the wrong length is LC_BIN_ETLV. */
    n = frame(fw0, sizeof fw0, buf);
    CHECK(record_decode_bin(buf, n, &r) == LC_BIN_ETLV);
    tlv[0] = 0x03; tlv[1] = 0x03; tlv[2] = 24;  /* LC_FW_MAX + 1 characters */
    mem_fill(&tlv[3], (uint8_t)'a', 24);
    n = frame(tlv, sizeof tlv, buf);
    CHECK(record_decode_bin(buf, n, &r) == LC_BIN_ETLV);
    n = frame(cause7, sizeof cause7, buf);
    CHECK(record_decode_bin(buf, n, &r) == LC_BIN_ETLV);
    n = frame(reg4, sizeof reg4, buf);
    CHECK(record_decode_bin(buf, n, &r) == LC_BIN_ETLV);
    n = frame(fplmn0, sizeof fplmn0, buf);
    CHECK(record_decode_bin(buf, n, &r) == LC_BIN_ETLV);
    n = frame(fplmn_short, sizeof fplmn_short, buf);
    CHECK(record_decode_bin(buf, n, &r) == LC_BIN_ETLV);
    n = frame(edrx3, sizeof edrx3, buf);
    CHECK(record_decode_bin(buf, n, &r) == LC_BIN_ETLV);
    /* Two bytes left in the TLV area: not even a TLV header. */
    n = frame(half_hdr, sizeof half_hdr, buf);
    CHECK(record_decode_bin(buf, n, &r) == LC_BIN_ETLV);
    CHECK(r.uptime_s == LC_U32_UNKNOWN);         /* rejected: all-unknown */
}

static void test_record_q(void)
{
    test_record_q_setters();
    test_record_q_text();
    test_record_q_bin();
}

/* ------------------------------------------------------------------------ */
/* Fixtures                                                                 */
/* ------------------------------------------------------------------------ */

static char *trim(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) {
        *--e = '\0';
    }
    return s;
}

static unsigned get_count(const char *spec, const char *key)
{
    const char *p = strstr(spec, key);
    return p ? (unsigned)strtoul(p + strlen(key), NULL, 10) : 9999U;
}

typedef struct {
    const char *name;
    at_parser_t ps;           /* stream path */
    at_parser_t pl;           /* line path */
    guarded_t gs;
    guarded_t gl;
    uint32_t now;
    int lineno;
    int at_lines;
    int bad_label;
    int had_text;
} fixture_t;

/* An AT line: fed as a CR LF stream to one parser, as a line to the other. */
static void fx_at_line(fixture_t *fx, const char *val)
{
    char crlf[1024];
    size_t n = strlen(val);

    if (n + 2U > sizeof crlf) {
        CHECK(n + 2U <= sizeof crlf);
        return;
    }
    fx->at_lines++;
    mem_copy(crlf, val, n);
    crlf[n] = '\r';
    crlf[n + 1U] = '\n';
    at_parse_feed(&fx->ps, &fx->gs.r, crlf, n + 2U, fx->now);
    (void)at_parse_line(&fx->pl, &fx->gl.r, val, n, fx->now);
}

static void fx_expect_text(fixture_t *fx, const char *val)
{
    char txt[LC_TEXT_MAX + 1];
    int n = record_encode_text(&fx->gs.r, txt, sizeof txt);

    fx->had_text = 1;
    CHECK(n >= 0 && n <= LC_TEXT_MAX);
    if (strcmp(txt, val) != 0) {
        printf("  in %s:\n", fx->name);
    }
    CHECK_STR(txt, val);
}

static void fx_expect_counts(const fixture_t *fx, const char *val)
{
    unsigned u = get_count(val, "unknown=");
    unsigned m = get_count(val, "malformed=");
    unsigned t = get_count(val, "too_long=");

    if (fx->ps.unknown != u || fx->ps.malformed != m || fx->ps.too_long != t) {
        printf("  %s: counts unknown=%u malformed=%u too_long=%u\n", fx->name,
               (unsigned)fx->ps.unknown, (unsigned)fx->ps.malformed, (unsigned)fx->ps.too_long);
    }
    CHECK(fx->ps.unknown == u && fx->ps.malformed == m && fx->ps.too_long == t);
    CHECK(fx->pl.unknown == u && fx->pl.malformed == m && fx->pl.too_long == t);
}

/* fw / battery / band: application setters. Returns 0 if key is none of them. */
static int fx_setter(fixture_t *fx, const char *key, const char *val)
{
    unsigned long v = strtoul(val, NULL, 10);

    if (strcmp(key, "fw") == 0) {
        lc_record_set_fw(&fx->gs.r, val);
        lc_record_set_fw(&fx->gl.r, val);
    } else if (strcmp(key, "battery") == 0) {
        lc_record_set_battery(&fx->gs.r, (uint16_t)v);
        lc_record_set_battery(&fx->gl.r, (uint16_t)v);
    } else if (strcmp(key, "band") == 0) {
        lc_record_set_band(&fx->gs.r, (uint8_t)v);
        lc_record_set_band(&fx->gl.r, (uint8_t)v);
    } else {
        return 0;
    }
    return 1;
}

/* "label | value": exactly one blank after '|'; only the line terminator is
 * stripped from the value. */
static void fx_labelled(fixture_t *fx, char *s, char *bar)
{
    char *key;
    char *val;
    size_t n;

    *bar = '\0';
    key = trim(s);
    val = &bar[1];
    if (*val == ' ') {
        val++;
    }
    n = strlen(val);
    while (n > 0U && (val[n - 1U] == '\r' || val[n - 1U] == '\n')) {
        n--;
        val[n] = '\0';
    }
    if (strcmp(key, "SYNTHETIC") == 0 || strcmp(key, "FROM-SPEC") == 0) {
        fx_at_line(fx, val);
    } else if (strcmp(key, "expect-text") == 0) {
        fx_expect_text(fx, val);
    } else if (strcmp(key, "expect-counts") == 0) {
        fx_expect_counts(fx, val);
    } else if (!fx_setter(fx, key, val)) {
        printf("  %s:%d: unknown label \"%s\" (want SYNTHETIC or FROM-SPEC)\n",
               fx->name, fx->lineno, key);
        fx->bad_label++;
    }
}

/* One line of the fixture file: blank, '#' comment, "@<uptime>", or labelled. */
static void fx_line(fixture_t *fx, char *raw)
{
    char *s = trim(raw);
    char *bar;

    fx->lineno++;
    if (*s == '\0' || *s == '#') {
        return;
    }
    if (*s == '@') {
        fx->now = (uint32_t)strtoul(&s[1], NULL, 10);
        return;
    }
    bar = strchr(s, '|');
    if (bar == NULL) {
        printf("  %s:%d: no '|'\n", fx->name, fx->lineno);
        fx->bad_label++;
        return;
    }
    fx_labelled(fx, s, bar);
}

/* out = dir + '/' + name. Returns 0 if it does not fit. */
static int join_path(char *out, size_t cap, const char *dir, const char *name)
{
    size_t d = strlen(dir);
    size_t n = strlen(name);

    if (d + 1U + n + 1U > cap) {
        return 0;
    }
    mem_copy(out, dir, d);
    out[d] = '/';
    mem_copy(&out[d + 1U], name, n);
    out[d + 1U + n] = '\0';
    return 1;
}

static void run_fixture(const char *dir, const char *name)
{
    char path[512];
    char raw[1024];
    FILE *f;
    fixture_t fx;
    int w;

    w = join_path(path, sizeof path, dir, name);
    CHECK(w);
    if (!w) {
        return;
    }
    f = fopen(path, "rb");
    CHECK(f != NULL);
    if (f == NULL) {
        printf("  cannot open %s\n", path);
        return;
    }
    fx.name = name;
    fx.now = 0;
    fx.lineno = 0;
    fx.at_lines = 0;
    fx.bad_label = 0;
    fx.had_text = 0;
    at_parser_init(&fx.ps);
    at_parser_init(&fx.pl);
    guard_init(&fx.gs);
    guard_init(&fx.gl);

    while (fgets(raw, sizeof raw, f) != NULL) {
        fx_line(&fx, raw);
    }
    (void)fclose(f);

    CHECK(fx.bad_label == 0);       /* every line is provenance-labelled */
    CHECK(fx.at_lines > 0 && fx.had_text);
    CHECK(guard_ok(&fx.gs) && guard_ok(&fx.gl));
    /* The stream and line paths agree, and the result survives the binary. */
    CHECK(record_equal(&fx.gs.r, &fx.gl.r));
    CHECK(roundtrip_equal(&fx.gs.r));
    printf("lc: fixture %-20s %3d AT lines, %u updates\n", name, fx.at_lines,
           (unsigned)fx.ps.updated);
}

int main(int argc, char **argv)
{
    static const char *fixtures[] = {
        "denied_15.at", "registered_psm.at", "recovered_22.at", "noise.at"
    };
    const char *dir = argc > 1 ? argv[1] : "tests/lc/fixtures";
    unsigned i;

    test_crc16();
    test_init_unknown();
    test_text_example();
    test_text_limits();
    test_bin_roundtrip();
    test_bin_corruption();
    test_bin_tlv_rules();
    test_timers();
    test_parser_units();
    test_parser_units_b();
    test_text_worst_case();
    test_cause_tables();
    test_edrx_text();
    test_parser_hostile();
    test_parser_stream_edges();
    test_at_branches();
    test_at_rat();
    test_at_state();
    test_bin_review();
    test_review_b();
    test_record_q();
    for (i = 0; i < sizeof fixtures / sizeof fixtures[0]; i++) {
        run_fixture(dir, fixtures[i]);
    }
    printf("lc: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
