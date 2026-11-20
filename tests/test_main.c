/* test_main.c - host tests: CRC_A vectors, NDEF codec, CC parsing, and the
 * M24SR command layer against mock_m24sr.c. Build: python build.py test */
#include <stdio.h>
#include <string.h>
#include "../src/crc_a.h"
#include "../src/ndef.h"
#include "../src/m24sr.h"
#include "mock_m24sr.h"

static int g_fail, g_pass;

#define CHECK(cond) do { \
    if (cond) g_pass++; \
    else { g_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static void test_crc_a(void)
{
    /* ISO/IEC 14443-3 Annex B examples. */
    const uint8_t a[] = { 0x00, 0x00 }, b[] = { 0x12, 0x34 };
    CHECK(crc_a(a, 2) == 0x1EA0u);
    CHECK(crc_a(b, 2) == 0xCF26u);
}

static void test_ndef_uri(void)
{
    uint8_t msg[64];
    int n = ndef_build_uri("https://www.st.com", msg, sizeof msg);
    const uint8_t want[] = { 0xD1, 0x01, 0x07, 'U', 0x02, 's', 't', '.', 'c', 'o', 'm' };
    CHECK(n == (int)sizeof want);
    CHECK(n > 0 && !memcmp(msg, want, sizeof want));

    uint32_t pos = 0;
    ndef_record_t r;
    char uri[64];
    CHECK(ndef_parse_record(msg, (uint32_t)n, &pos, &r) == 0);
    CHECK(r.mb && r.me && r.sr && r.tnf == NDEF_TNF_WELL_KNOWN);
    CHECK(pos == (uint32_t)n);
    CHECK(ndef_uri_to_string(&r, uri, sizeof uri) == 18);
    CHECK(!strcmp(uri, "https://www.st.com"));

    n = ndef_build_uri("tel:+972", msg, sizeof msg);   /* prefix 0x05 */
    CHECK(n == 4 + 1 + 4 && msg[4] == 0x05);
    n = ndef_build_uri("geo:32.1,34.8", msg, sizeof msg); /* no prefix */
    CHECK(n > 0 && msg[4] == 0x00);
}

static void test_ndef_text(void)
{
    uint8_t msg[64];
    int n = ndef_build_text("en", "Hello", msg, sizeof msg);
    const uint8_t want[] = { 0xD1, 0x01, 0x08, 'T', 0x02, 'e', 'n', 'H', 'e', 'l', 'l', 'o' };
    CHECK(n == (int)sizeof want && !memcmp(msg, want, sizeof want));

    uint32_t pos = 0;
    ndef_record_t r;
    char lang[8], text[32];
    CHECK(ndef_parse_record(msg, (uint32_t)n, &pos, &r) == 0);
    CHECK(ndef_text_get(&r, lang, sizeof lang, text, sizeof text) == 5);
    CHECK(!strcmp(lang, "en") && !strcmp(text, "Hello"));
    CHECK(ndef_uri_to_string(&r, text, sizeof text) == -1);   /* not a URI */
}

static void test_ndef_malformed(void)
{
    const uint8_t trunc[] = { 0xD1, 0x01, 0x10, 'U', 0x01, 'a' };   /* says 16, has 2 */
    const uint8_t chunked[] = { 0xB1, 0x01, 0x01, 'U', 0x00 };
    uint32_t pos = 0;
    ndef_record_t r;
    CHECK(ndef_parse_record(trunc, sizeof trunc, &pos, &r) == -1);
    pos = 0;
    CHECK(ndef_parse_record(chunked, sizeof chunked, &pos, &r) == -2);

    /* Long record (SR = 0) with an ID field. */
    const uint8_t lr[] = { 0xC9, 0x01, 0x00, 0x00, 0x00, 0x02, 0x01, 'U', 'i', 0x00, 'x' };
    pos = 0;
    CHECK(ndef_parse_record(lr, sizeof lr, &pos, &r) == 0);
    CHECK(!r.sr && r.id_len == 1 && r.payload_len == 2 && pos == sizeof lr);
}

static void test_cc(void)
{
    mock_m24sr_t m;
    m24sr_cc_t cc;
    mock_m24sr_init(&m);
    CHECK(m24sr_parse_cc(m.cc, sizeof m.cc, &cc) == 0);
    CHECK(cc.cclen == 15 && cc.version == 0x20 && cc.mle == 0xF6);
    CHECK(cc.ndef_file_id == 0x0001 && cc.ndef_max == 0x2000);
    uint8_t bad[15];
    memcpy(bad, m.cc, sizeof bad);
    bad[7] = 0x05;
    CHECK(m24sr_parse_cc(bad, sizeof bad, &cc) == M24SR_ERR_FRAME);
}

static void test_m24sr_flow(void)
{
    mock_m24sr_t m;
    m24sr_t d;
    mock_m24sr_init(&m);
    m24sr_init(&d, &m.bus);

    /* Read an empty NDEF file, write a URI message, read it back. */
    CHECK(m24sr_get_session(&d) == 0);
    CHECK(m24sr_select_ndef_app(&d) == 0);
    uint8_t cc[15];
    CHECK(m24sr_select_file(&d, M24SR_FILE_CC) == 0);
    CHECK(m24sr_read_binary(&d, 0, cc, sizeof cc) == 0);
    CHECK(!memcmp(cc, m.cc, sizeof cc));

    uint8_t msg[200], back[256];
    int n = ndef_build_uri("https://www.example.com/a/rather/long/path/that/needs/"
                           "more/than/one/chunk/to/be/written/to/the/tag/0123456789", msg, sizeof msg);
    CHECK(n > (int)M24SR_CHUNK);
    CHECK(m24sr_write_ndef(&d, M24SR_FILE_NDEF, msg, (uint32_t)n) == 0);
    CHECK(m24sr_read_ndef(&d, M24SR_FILE_NDEF, back, sizeof back) == n);
    CHECK(!memcmp(back, msg, (size_t)n));
    CHECK(m.ndef[0] == 0 && m.ndef[1] == (uint8_t)n);  /* NLEN written last */

    /* Errors carry the status word. */
    CHECK(m24sr_select_file(&d, 0x1234) == M24SR_ERR_SW);
    CHECK(d.last_sw == 0x6A82u);

    /* Busy tag and a waiting-time extension are both absorbed. */
    m.busy_left = 5;
    m.wtx_next = 1;
    CHECK(m24sr_select_file(&d, M24SR_FILE_NDEF) == 0);
    CHECK(d.wtx_count == 1 && d.busy_polls >= 5);

    CHECK(m24sr_deselect(&d) == 0);
    CHECK(!m.session);
    CHECK(m.crc_errors == 0 && m.frame_errors == 0 && m.block_errors == 0);
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
    test_ndef_text();
    test_ndef_malformed();
    test_cc();
    test_m24sr_flow();
    test_rf_session();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
