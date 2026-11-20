/* test_main.c - host tests: CRC_A vectors, NDEF codec, CC parsing, and the
 * M24SR command layer against mock_m24sr.c. Build: python build.py test
 *
 * Each test names the clause it checks. Standards: ISO/IEC 14443-3 (CRC_A),
 * ISO/IEC 14443-4 (block protocol), ISO/IEC 7816-4 (APDUs), NFC Forum Type 4
 * Tag (T4T: NDEF application, CC file, NDEF file), NFC Forum NDEF 1.0 and the
 * RTD URI 1.0 / RTD Text 1.0 record types. */
#include <stdio.h>
#include <string.h>
#include "../src/crc_a.h"
#include "../src/ndef.h"
#include "../src/m24sr.h"
#include "mock_m24sr.h"

static int g_fail;
static int g_pass;

static void check(int ok, int line, const char *what)
{
    if (ok != 0) {
        g_pass++;
    } else {
        g_fail++;
        printf("FAIL %s:%d: %s\n", __FILE__, line, what);
    }
}

#define CHECK(cond) check((cond) ? 1 : 0, __LINE__, #cond)

/* A plain byte copy: the C11 Annex K check flags memcpy() in test code. */
static void memcpy_s_free(char *dst, const char *src, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        dst[i] = src[i];
    }
}

/* ---- CRC_A: ISO/IEC 14443-3 6.2.4 and Annex B ----------------------------- */

static void test_crc_a(void)
{
    /* Annex B worked examples: 00 00 -> sent A0 1E; 12 34 -> sent 26 CF. */
    const uint8_t a[] = { 0x00, 0x00 };
    const uint8_t b[] = { 0x12, 0x34 };
    CHECK(crc_a(a, 2) == 0x1EA0U);
    CHECK(crc_a(b, 2) == 0xCF26U);
    /* Preset 6363h: the CRC of no bytes is the preset itself. */
    CHECK(crc_a(a, 0) == 0x6363U);
}

/* ---- NDEF 1.0 and RTD URI 1.0 -------------------------------------------- */

static void test_ndef_uri(void)
{
    uint8_t msg[64];
    int n = ndef_build_uri("https://www.st.com", msg, sizeof msg);
    const uint8_t want[] = { 0xD1, 0x01, 0x07, 'U', 0x02, 's', 't', '.', 'c', 'o', 'm' };
    CHECK(n == (int)sizeof want);
    CHECK((n > 0) && (memcmp(msg, want, sizeof want) == 0));

    uint32_t pos = 0;
    ndef_record_t r;
    char uri[64];
    CHECK(ndef_parse_record(msg, (uint32_t)n, &pos, &r) == 0);
    CHECK(r.mb && r.me && r.sr && (r.tnf == NDEF_TNF_WELL_KNOWN));
    CHECK(pos == (uint32_t)n);
    CHECK(ndef_uri_to_string(&r, uri, sizeof uri) == 18);
    CHECK(strcmp(uri, "https://www.st.com") == 0);

    n = ndef_build_uri("tel:+972", msg, sizeof msg);   /* prefix 0x05 */
    CHECK((n == (4 + 1 + 4)) && (msg[4] == 0x05));
    n = ndef_build_uri("geo:32.1,34.8", msg, sizeof msg); /* no prefix */
    CHECK((n > 0) && (msg[4] == 0x00));
}

/* RTD URI 1.0 Table 3, typed in from the specification: every code 0x01..0x23
 * round-trips, and the builder picks the longest matching prefix. */
static void test_uri_table(void)
{
    static const char *const table3[] = {
        "", "http://www.", "https://www.", "http://", "https://", "tel:", "mailto:",
        "ftp://anonymous:anonymous@", "ftp://ftp.", "ftps://", "sftp://", "smb://",
        "nfs://", "ftp://", "dav://", "news:", "telnet://", "imap:", "rtsp://", "urn:",
        "pop:", "sip:", "sips:", "tftp:", "btspp://", "btl2cap://", "btgoep://",
        "tcpobex://", "irdaobex://", "file://", "urn:epc:id:", "urn:epc:tag:",
        "urn:epc:pat:", "urn:epc:raw:", "urn:epc:", "urn:nfc:",
    };
    int bad = 0;
    for (unsigned c = 1; c < sizeof table3 / sizeof table3[0]; c++) {
        char in[64];
        char out[64];
        uint8_t msg[80];
        uint32_t pos = 0;
        ndef_record_t r;
        size_t k = strlen(table3[c]);
        memcpy_s_free(in, table3[c], k);
        in[k] = 'x';
        in[k + 1U] = '\0';
        int n = ndef_build_uri(in, msg, sizeof msg);
        if ((n < 5) || (msg[4] != c) || (ndef_parse_record(msg, (uint32_t)n, &pos, &r) != 0) ||
            (ndef_uri_to_string(&r, out, sizeof out) < 0) || (strcmp(in, out) != 0)) {
            printf("  URI code 0x%02X (%s) does not round-trip\n", c, table3[c]);
            bad++;
        }
    }
    CHECK(bad == 0);

    /* 0x24..0xFF are RFU: no prefix is prepended. */
    const uint8_t rfu[] = { 0xD1, 0x01, 0x02, 'U', 0x24, 'a' };
    uint32_t pos = 0;
    ndef_record_t r;
    char out[8];
    CHECK(ndef_parse_record(rfu, sizeof rfu, &pos, &r) == 0);
    CHECK((ndef_uri_to_string(&r, out, sizeof out) == 1) && (strcmp(out, "a") == 0));
    CHECK(ndef_uri_to_string(&r, out, 1) == -1);              /* no room */
}

/* RTD Text 1.0 3.2.1: status bit 7 = UTF-16, bit 6 RFU, bits 5..0 = language
 * code length. */
static void test_ndef_text(void)
{
    uint8_t msg[64];
    int n = ndef_build_text("en", "Hello", msg, sizeof msg);
    const uint8_t want[] = { 0xD1, 0x01, 0x08, 'T', 0x02, 'e', 'n', 'H', 'e', 'l', 'l', 'o' };
    CHECK((n == (int)sizeof want) && (memcmp(msg, want, sizeof want) == 0));

    uint32_t pos = 0;
    ndef_record_t r;
    char lang[8];
    char text[32];
    CHECK(ndef_parse_record(msg, (uint32_t)n, &pos, &r) == 0);
    CHECK(ndef_text_get(&r, lang, sizeof lang, text, sizeof text) == 5);
    CHECK((strcmp(lang, "en") == 0) && (strcmp(text, "Hello") == 0));
    CHECK(ndef_uri_to_string(&r, text, sizeof text) == -1);   /* not a URI */
    CHECK(ndef_text_get(&r, lang, 2, text, sizeof text) == -1);   /* lang buffer */
    CHECK(ndef_text_get(&r, lang, sizeof lang, text, 5) == -1);   /* text buffer */

    const uint8_t utf16[] = { 0xD1, 0x01, 0x03, 'T', 0x82, 'e', 'n' };
    pos = 0;
    CHECK(ndef_parse_record(utf16, sizeof utf16, &pos, &r) == 0);
    CHECK(ndef_text_get(&r, lang, sizeof lang, text, sizeof text) == -1);
    const uint8_t rfu6[] = { 0xD1, 0x01, 0x04, 'T', 0x42, 'e', 'n', 'x' };   /* bit 6 ignored */
    pos = 0;
    CHECK(ndef_parse_record(rfu6, sizeof rfu6, &pos, &r) == 0);
    CHECK(ndef_text_get(&r, lang, sizeof lang, text, sizeof text) == 1);
    const uint8_t longlang[] = { 0xD1, 0x01, 0x02, 'T', 0x05, 'e' };   /* says 5, has 1 */
    pos = 0;
    CHECK(ndef_parse_record(longlang, sizeof longlang, &pos, &r) == 0);
    CHECK(ndef_text_get(&r, lang, sizeof lang, text, sizeof text) == -1);
    const uint8_t empty[] = { 0xD1, 0x01, 0x00, 'T' };
    pos = 0;
    CHECK(ndef_parse_record(empty, sizeof empty, &pos, &r) == 0);
    CHECK(ndef_text_get(&r, lang, sizeof lang, text, sizeof text) == -1);
    CHECK(ndef_uri_to_string(&r, text, sizeof text) == -1);
}

static void test_ndef_build_limits(void)
{
    char big[300];
    uint8_t out[300];
    for (size_t i = 0; i < sizeof big; i++) {
        big[i] = 'a';
    }
    big[255] = '\0';                                  /* 1 + 255 > 255 */
    CHECK(ndef_build_uri(big, out, sizeof out) == -1);
    big[254] = '\0';                                  /* exactly 255 */
    CHECK(ndef_build_uri(big, out, sizeof out) == 4 + 255);
    CHECK(ndef_build_uri("tel:1", out, 5) == -1);     /* out too small */
    big[64] = '\0';
    CHECK(ndef_build_text(big, "x", out, sizeof out) == -1);   /* lang > 63 */
}

/* NDEF 1.0 3.2: header bits, SR, IL, CF; 3.2.6: TNF rules. */
static void test_ndef_malformed(void)
{
    const uint8_t trunc[] = { 0xD1, 0x01, 0x10, 'U', 0x01, 'a' };   /* says 16, has 2 */
    const uint8_t chunked[] = { 0xB1, 0x01, 0x01, 'U', 0x00 };
    uint32_t pos = 0;
    ndef_record_t r;
    CHECK(ndef_parse_record(trunc, sizeof trunc, &pos, &r) == NDEF_ERR_TRUNCATED);
    pos = 0;
    CHECK(ndef_parse_record(chunked, sizeof chunked, &pos, &r) == NDEF_ERR_CHUNKED);

    /* Long record (SR = 0) with an ID field. */
    const uint8_t lr[] = { 0xC9, 0x01, 0x00, 0x00, 0x00, 0x02, 0x01, 'U', 'i', 0x00, 'x' };
    pos = 0;
    CHECK(ndef_parse_record(lr, sizeof lr, &pos, &r) == 0);
    CHECK(!r.sr && (r.id_len == 1) && (r.payload_len == 2) && (pos == sizeof lr));
    pos = 0;
    CHECK(ndef_parse_record(lr, 5, &pos, &r) == NDEF_ERR_TRUNCATED);   /* PAYLOAD_LENGTH cut */
    pos = 0;
    CHECK(ndef_parse_record(lr, 6, &pos, &r) == NDEF_ERR_TRUNCATED);   /* ID_LENGTH missing */
    pos = 0;
    CHECK(ndef_parse_record(lr, 7, &pos, &r) == NDEF_ERR_TRUNCATED);   /* type + ID cut */
    pos = 2;
    CHECK(ndef_parse_record(lr, 4, &pos, &r) == NDEF_ERR_TRUNCATED);   /* *pos near the end */
    pos = 0;
    CHECK(ndef_parse_record(lr, 2, &pos, &r) == NDEF_ERR_TRUNCATED);
    /* *pos close to UINT32_MAX: "pos + 3 > len" would wrap; the parser must not read. */
    pos = 0xFFFFFFFEU;
    CHECK(ndef_parse_record(lr, sizeof lr, &pos, &r) == NDEF_ERR_TRUNCATED);
    CHECK(pos == 0xFFFFFFFEU);

    /* 3.2.6: Empty (TNF 0) has all lengths 0; Unknown (5) has no type;
     * Unchanged (6) only inside a chunked record. */
    const uint8_t empty_ok[] = { 0xD0, 0x00, 0x00 };
    const uint8_t empty_bad[] = { 0xD0, 0x00, 0x01, 'x' };
    const uint8_t unknown_ok[] = { 0xD5, 0x00, 0x01, 'x' };
    const uint8_t unknown_bad[] = { 0xD5, 0x01, 0x00, 'T' };
    const uint8_t unchanged[] = { 0xD6, 0x00, 0x00 };
    pos = 0;
    CHECK(ndef_parse_record(empty_ok, sizeof empty_ok, &pos, &r) == 0);
    pos = 0;
    CHECK(ndef_parse_record(empty_bad, sizeof empty_bad, &pos, &r) == NDEF_ERR_HEADER);
    pos = 0;
    CHECK(ndef_parse_record(unknown_ok, sizeof unknown_ok, &pos, &r) == 0);
    pos = 0;
    CHECK(ndef_parse_record(unknown_bad, sizeof unknown_bad, &pos, &r) == NDEF_ERR_HEADER);
    pos = 0;
    CHECK(ndef_parse_record(unchanged, sizeof unchanged, &pos, &r) == NDEF_ERR_HEADER);
    const uint8_t empty_type[] = { 0xD0, 0x01, 0x00, 'x' };
    const uint8_t empty_id[] = { 0xD8, 0x00, 0x00, 0x01, 'i' };
    pos = 0;
    CHECK(ndef_parse_record(empty_type, sizeof empty_type, &pos, &r) == NDEF_ERR_HEADER);
    pos = 0;
    CHECK(ndef_parse_record(empty_id, sizeof empty_id, &pos, &r) == NDEF_ERR_HEADER);

    /* Only TNF 1 with the one-byte type "U" / "T" is a URI / Text record. */
    char s[8];
    char l[8];
    const uint8_t mime_u[] = { 0xD2, 0x01, 0x01, 'U', 0x00 };
    const uint8_t wk_two[] = { 0xD1, 0x02, 0x01, 'U', 'x', 0x00 };
    const uint8_t uri_empty[] = { 0xD1, 0x01, 0x00, 'U' };
    pos = 0;
    CHECK(ndef_parse_record(mime_u, sizeof mime_u, &pos, &r) == 0);
    CHECK(ndef_uri_to_string(&r, s, sizeof s) == -1);
    pos = 0;
    CHECK(ndef_parse_record(wk_two, sizeof wk_two, &pos, &r) == 0);
    CHECK(ndef_uri_to_string(&r, s, sizeof s) == -1);
    CHECK(ndef_text_get(&r, l, sizeof l, s, sizeof s) == -1);
    pos = 0;
    CHECK(ndef_parse_record(uri_empty, sizeof uri_empty, &pos, &r) == 0);
    CHECK(ndef_uri_to_string(&r, s, sizeof s) == -1);
}

/* ---- T4T capability container: T4T 5.1, Table "CC file" ------------------- */

static void test_cc(void)
{
    mock_m24sr_t m;
    m24sr_cc_t cc;
    mock_m24sr_init(&m);
    CHECK(m24sr_parse_cc(m.cc, sizeof m.cc, &cc) == 0);
    CHECK((cc.cclen == 15) && (cc.version == 0x20) && (cc.mle == 0xF6) && (cc.mlc == 0xF6));
    CHECK((cc.ndef_file_id == 0x0001) && (cc.ndef_max == 0x2000));
    CHECK((cc.read_access == 0) && (cc.write_access == 0));
    uint8_t bad[15];
    memcpy_s_free((char *)bad, (const char *)m.cc, sizeof bad);
    bad[7] = 0x05;                                    /* T must be 04h (NDEF File Control TLV) */
    CHECK(m24sr_parse_cc(bad, sizeof bad, &cc) == M24SR_ERR_FRAME);
    bad[7] = 0x04;
    bad[8] = 0x08;                                    /* L must be 06h */
    CHECK(m24sr_parse_cc(bad, sizeof bad, &cc) == M24SR_ERR_FRAME);
    CHECK(m24sr_parse_cc(m.cc, 14, &cc) == M24SR_ERR_FRAME);
}

/* ---- Command layer against the mock --------------------------------------- */

static void open_session(mock_m24sr_t *m, m24sr_t *d)
{
    mock_m24sr_init(m);
    m24sr_init(d, &m->bus);
    CHECK(m24sr_get_session(d) == 0);
    CHECK(m24sr_select_ndef_app(d) == 0);
}

static void test_m24sr_flow(void)
{
    mock_m24sr_t m;
    m24sr_t d;
    open_session(&m, &d);

    /* Read the CC, write a URI message over several chunks, read it back. */
    uint8_t cc[15];
    CHECK(m24sr_select_file(&d, M24SR_FILE_CC) == 0);
    CHECK(m24sr_read_binary(&d, 0, cc, sizeof cc) == 0);
    CHECK(memcmp(cc, m.cc, sizeof cc) == 0);

    uint8_t msg[200];
    uint8_t back[256];
    int n = ndef_build_uri("https://www.example.com/a/rather/long/path/that/needs/"
                           "more/than/one/chunk/to/be/written/to/the/tag/0123456789", msg, sizeof msg);
    CHECK(n > (int)M24SR_CHUNK);
    CHECK(m24sr_write_ndef(&d, M24SR_FILE_NDEF, msg, (uint32_t)n) == 0);
    CHECK(m24sr_read_ndef(&d, M24SR_FILE_NDEF, back, sizeof back) == n);
    CHECK(memcmp(back, msg, (size_t)n) == 0);
    CHECK((m.ndef[0] == 0) && (m.ndef[1] == (uint8_t)n));
    CHECK(m24sr_read_ndef(&d, M24SR_FILE_NDEF, back, (uint32_t)n - 1U) == M24SR_ERR_ARG);

    /* Errors carry the status word. */
    CHECK(m24sr_select_file(&d, 0x1234) == M24SR_ERR_SW);
    CHECK(d.last_sw == 0x6A82U);

    /* Busy tag and a waiting-time extension are both absorbed. */
    m.busy_left = 5;
    m.wtx_next = 1;
    CHECK(m24sr_select_file(&d, M24SR_FILE_NDEF) == 0);
    CHECK((d.wtx_count == 1) && (d.busy_polls >= 5));

    CHECK(m24sr_deselect(&d) == 0);
    CHECK(!m.session);
    CHECK((m.crc_errors == 0) && (m.frame_errors == 0) && (m.block_errors == 0));
}

/* T4T 5.4 (NDEF update procedure): NLEN = 0000h first, the message, NLEN last. */
static void test_nlen_order(void)
{
    mock_m24sr_t m;
    m24sr_t d;
    const uint8_t msg[3] = { 0xD0, 0x00, 0x00 };
    open_session(&m, &d);
    CHECK(m24sr_write_ndef(&d, M24SR_FILE_NDEF, msg, sizeof msg) == 0);
    CHECK(m.n_updates == 3);
    CHECK((m.updates[0].off == 0) && (m.updates[0].len == 2) &&
          (m.updates[0].first[0] == 0) && (m.updates[0].first[1] == 0));
    CHECK((m.updates[1].off == 2) && (m.updates[1].len == 3));
    CHECK((m.updates[2].off == 0) && (m.updates[2].first[0] == 0) && (m.updates[2].first[1] == 3));
}

/* ISO/IEC 7816-4 command APDUs and ISO/IEC 14443-4 I-block framing, checked
 * byte by byte on the frames the mock received. */
static int last_tx_is(const mock_m24sr_t *m, const uint8_t *want, uint32_t n)
{
    uint16_t c = crc_a(want, n);
    return (m->last_tx_len == (n + 2U)) && (memcmp(m->last_tx, want, n) == 0) &&
           (m->last_tx[n] == (uint8_t)(c & 0xFFU)) && (m->last_tx[n + 1U] == (uint8_t)(c >> 8));
}

static void test_apdu_bytes(void)
{
    mock_m24sr_t m;
    m24sr_t d;
    mock_m24sr_init(&m);
    m24sr_init(&d, &m.bus);
    CHECK(m24sr_get_session(&d) == 0);
    CHECK((m.last_tx_len == 1) && (m.last_tx[0] == 0x26));       /* GetI2Csession */

    /* T4T 5.4.2: NDEF Tag Application Select, AID D2760000850101, Le = 00.
     * 14443-4 7.1.1.1: I-block PCB 02h | block number, no CID, no NAD. */
    const uint8_t sel_app[] = { 0x02, 0x00, 0xA4, 0x04, 0x00, 0x07,
                                0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01, 0x00 };
    CHECK(m24sr_select_ndef_app(&d) == 0);
    CHECK(last_tx_is(&m, sel_app, sizeof sel_app));
    /* 14443-4 7.5.3.2 rule B: the block number toggles on each valid I-block. */
    CHECK(d.block == 1);
    const uint8_t sel_cc[] = { 0x03, 0x00, 0xA4, 0x00, 0x0C, 0x02, 0xE1, 0x03 };
    CHECK(m24sr_select_file(&d, M24SR_FILE_CC) == 0);
    CHECK(last_tx_is(&m, sel_cc, sizeof sel_cc));
    uint8_t b[2];
    const uint8_t rd[] = { 0x02, 0x00, 0xB0, 0x00, 0x05, 0x02 };
    CHECK(m24sr_read_binary(&d, 5, b, 2) == 0);
    CHECK(last_tx_is(&m, rd, sizeof rd));
    /* An error status word still comes in a valid I-block: it toggles too. */
    CHECK(m24sr_select_file(&d, 0x1234) == M24SR_ERR_SW);
    CHECK(d.block == 0);
    CHECK(m.block_errors == 0);

    /* 14443-4 7.3: the PCD answers S(WTX) with S(WTX) and the same WTXM. */
    m.wtx_next = 1;
    CHECK(m24sr_select_file(&d, M24SR_FILE_NDEF) == 0);
    const uint8_t wtx[] = { 0xF2, 0x0B };             /* the mock asks with WTXM 0Bh */
    CHECK(last_tx_is(&m, wtx, sizeof wtx));           /* last frame written = S(WTX) answer */
    /* 14443-4 8.3 / M24SR: S(DESELECT) is C2h + CRC_A, the answer echoes it. */
    const uint8_t desel[] = { 0xC2 };
    CHECK(m24sr_deselect(&d) == 0);
    CHECK(last_tx_is(&m, desel, sizeof desel));
    CHECK(d.block == 0);
}

/* The 7816-4 header (CLA INS P1 P2 P3) of the last I-block the mock received. */
static int last_apdu_is(const mock_m24sr_t *m, uint8_t ins, uint8_t p1, uint8_t p2, uint8_t p3)
{
    return (m->last_tx_len >= 8U) && (m->last_tx[1] == 0x00U) && (m->last_tx[2] == ins) &&
           (m->last_tx[3] == p1) && (m->last_tx[4] == p2) && (m->last_tx[5] == p3);
}

static void test_wtx_bytes(void)
{
    mock_m24sr_t m;
    m24sr_t d;
    const uint8_t wtx[] = { 0xF2, 0x0B };
    open_session(&m, &d);
    m.wtx_next = 1;
    m.fail_write_call = m.write_calls + 2U;           /* I-block ok, the WTX answer fails */
    CHECK(m24sr_select_file(&d, M24SR_FILE_NDEF) == M24SR_ERR_BUS);
    CHECK(d.wtx_count == 1);

    open_session(&m, &d);
    m.wtx_next = 1;
    m.fail_read_call = m.read_calls + 2U;             /* the read after the WTX fails */
    CHECK(m24sr_select_file(&d, M24SR_FILE_NDEF) == M24SR_ERR_BUS);
    CHECK(last_tx_is(&m, wtx, sizeof wtx));

    /* A WTX request with a bad CRC_A is rejected. */
    open_session(&m, &d);
    m.wtx_next = 1;
    m.corrupt_next = 1;
    CHECK(m24sr_select_file(&d, M24SR_FILE_NDEF) == M24SR_ERR_CRC);
}

/* Bus errors, timeouts and corrupted answers: no state change on the PCD side
 * unless a valid I-block arrived. */
/* Large enough for every length the range tests pass, so a missing check reads or
 * writes inside this buffer instead of past a small one (undefined behaviour). */
static uint8_t g_big[M24SR_OFFSET_END];

static void test_m24sr_errors(void)
{
    mock_m24sr_t m;
    m24sr_t d;
    uint8_t buf[70];

    open_session(&m, &d);
    m.fail_write_call = m.write_calls + 1U;
    CHECK(m24sr_select_file(&d, M24SR_FILE_CC) == M24SR_ERR_BUS);
    m.fail_read_call = m.read_calls + 1U;
    CHECK(m24sr_select_file(&d, M24SR_FILE_CC) == M24SR_ERR_BUS);
    m.busy_left = 1000;                               /* never answers: 200 polls */
    CHECK(m24sr_select_file(&d, M24SR_FILE_CC) == M24SR_ERR_TIMEOUT);
    m.busy_left = 0;

    open_session(&m, &d);
    m.busy_read_left = 1000;
    CHECK(m24sr_select_file(&d, M24SR_FILE_CC) == M24SR_ERR_TIMEOUT);
    m.busy_read_left = 0;

    /* CRC error: no toggle (rule B applies only to a valid block). */
    open_session(&m, &d);
    uint8_t before = d.block;
    m.corrupt_next = 1;
    CHECK(m24sr_select_file(&d, M24SR_FILE_CC) == M24SR_ERR_CRC);
    CHECK(d.block == before);

    /* Wrong block number in the answer. */
    open_session(&m, &d);
    m.flip_pcb_next = 1;
    CHECK(m24sr_select_file(&d, M24SR_FILE_CC) == M24SR_ERR_FRAME);

    /* Data expected, short error answer (PCB SW1 SW2 CRC): 5-byte re-check. */
    open_session(&m, &d);
    CHECK(m24sr_select_file(&d, M24SR_FILE_CC) == 0);
    CHECK(m24sr_read_binary(&d, 10, buf, 10) == M24SR_ERR_SW);
    CHECK(d.last_sw == 0x6A82U);
    /* Data expected, 90 00 with no data: a frame error. */
    m.ok_no_data = 1;
    CHECK(m24sr_read_binary(&d, 0, buf, 4) == M24SR_ERR_FRAME);

    /* ISO/IEC 7816-4: P1 bit 8 = 0 leaves a 15-bit offset. Reject side: one byte past
     * offset 7FFFh, and nothing reaches the bus. */
    uint32_t writes_before = m.write_calls;
    uint32_t updates_before = m.n_updates;
    CHECK(m24sr_read_binary(&d, 0x7FFF, g_big, 2) == M24SR_ERR_ARG);
    CHECK(m24sr_read_binary(&d, 0x8000, g_big, 0) == M24SR_ERR_ARG);
    CHECK(m24sr_update_binary(&d, 0x7FFF, g_big, 2) == M24SR_ERR_ARG);
    CHECK(m24sr_write_ndef(&d, M24SR_FILE_NDEF, g_big, 0x7FFFU) == M24SR_ERR_ARG);
    CHECK((m.write_calls == writes_before) && (m.n_updates == updates_before));

    /* Accept side, at the boundary: the last legal bytes end exactly at 7FFFh. The
     * command goes out with P1 = 7Fh (the mock's file is smaller, so it answers 6A82). */
    CHECK(m24sr_read_binary(&d, 0x7FFE, g_big, 2) == M24SR_ERR_SW);
    CHECK(last_apdu_is(&m, 0xB0U, 0x7FU, 0xFEU, 0x02U));
    CHECK(m24sr_read_binary(&d, 0x7FFF, g_big, 1) == M24SR_ERR_SW);
    CHECK(last_apdu_is(&m, 0xB0U, 0x7FU, 0xFFU, 0x01U));
    CHECK(m24sr_update_binary(&d, 0x7FFF, g_big, 1) == M24SR_ERR_SW);
    CHECK(last_apdu_is(&m, 0xD6U, 0x7FU, 0xFFU, 0x01U));
    /* write_ndef: the message starts at offset 2, so 7FFEh bytes is the largest. It passes
     * the length check: NLEN is cleared (update at offset 0), then the message is sent. */
    updates_before = m.n_updates;
    CHECK(m24sr_select_ndef_app(&d) == 0);
    CHECK(m24sr_write_ndef(&d, M24SR_FILE_NDEF, g_big, 0x7FFEU) == M24SR_ERR_SW);
    CHECK((m.n_updates >= updates_before + 2U) && (m.updates[updates_before].off == 0U) &&
          (m.updates[updates_before].len == 2U) && (m.updates[updates_before + 1U].off == 2U));
}

static void test_session_errors(void)
{
    mock_m24sr_t m;
    m24sr_t d;
    uint8_t buf[8];

    mock_m24sr_init(&m);
    m24sr_init(&d, &m.bus);
    m.fail_write_call = 1;
    CHECK(m24sr_get_session(&d) == M24SR_ERR_BUS);
    m.busy_left = 1000;
    CHECK(m24sr_kill_rf_session(&d) == M24SR_ERR_TIMEOUT);

    /* DESELECT: bus errors and a bad answer. */
    open_session(&m, &d);
    m.fail_write_call = m.write_calls + 1U;
    CHECK(m24sr_deselect(&d) == M24SR_ERR_BUS);
    m.fail_read_call = m.read_calls + 1U;
    CHECK(m24sr_deselect(&d) == M24SR_ERR_BUS);
    open_session(&m, &d);
    m.corrupt_next = 1;
    CHECK(m24sr_deselect(&d) == M24SR_ERR_FRAME);
    open_session(&m, &d);
    m.flip_pcb_next = 1;                              /* answer C3h: not S(DESELECT) */
    CHECK(m24sr_deselect(&d) == M24SR_ERR_FRAME);

    /* Whole-message helpers stop at the first failing step. */
    open_session(&m, &d);
    CHECK(m24sr_read_ndef(&d, 0x1234, buf, sizeof buf) == M24SR_ERR_SW);
    m.fail_read_call = m.read_calls + 2U;             /* select ok, NLEN read fails */
    CHECK(m24sr_read_ndef(&d, M24SR_FILE_NDEF, buf, sizeof buf) == M24SR_ERR_BUS);
    m.ndef[1] = 4;
    m.fail_read_call = m.read_calls + 3U;             /* NLEN ok, message read fails */
    CHECK(m24sr_read_ndef(&d, M24SR_FILE_NDEF, buf, sizeof buf) == M24SR_ERR_BUS);
    CHECK(m24sr_write_ndef(&d, 0x1234, buf, 2) == M24SR_ERR_SW);
    m.fail_write_call = m.write_calls + 2U;           /* select ok, NLEN clear fails */
    CHECK(m24sr_write_ndef(&d, M24SR_FILE_NDEF, buf, 2) == M24SR_ERR_BUS);
    m.fail_write_call = m.write_calls + 3U;           /* message write fails */
    CHECK(m24sr_write_ndef(&d, M24SR_FILE_NDEF, buf, 2) == M24SR_ERR_BUS);
    m.fail_write_call = m.write_calls + 2U;
    CHECK(m24sr_update_binary(&d, 0, buf, 2) == 0);   /* call 1 passes */
    CHECK(m24sr_update_binary(&d, 0, buf, 2) == M24SR_ERR_BUS);
}

static void test_rf_session(void)
{
    mock_m24sr_t m;
    m24sr_t d;
    mock_m24sr_init(&m);
    m24sr_init(&d, &m.bus);
    m.rf_session = 1;                                 /* a phone holds the tag */
    CHECK(m24sr_get_session(&d) == M24SR_NACK);
    CHECK(m24sr_kill_rf_session(&d) == 0);
    CHECK(m24sr_select_ndef_app(&d) == 0);
}

int main(void)
{
    test_crc_a();
    test_ndef_uri();
    test_uri_table();
    test_ndef_text();
    test_ndef_build_limits();
    test_ndef_malformed();
    test_cc();
    test_m24sr_flow();
    test_nlen_order();
    test_apdu_bytes();
    test_wtx_bytes();
    test_m24sr_errors();
    test_session_errors();
    test_rf_session();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return (g_fail != 0) ? 1 : 0;
}
