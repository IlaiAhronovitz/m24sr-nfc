/* fuzz_at.c - fuzz target over at_parse_line() / at_parse_feed().
 *
 * libFuzzer:   clang -g -O1 -fsanitize=fuzzer,address,undefined \
 *                  tests/lc/fuzz_at.c src/lc/record.c src/lc/at_parse.c
 * Standalone:  cc -DSTANDALONE_FUZZ ... ; runs 200000 deterministic
 *              iterations (python build.py test does this).
 *
 * Each input is parsed as one line, then as a byte stream, into a record
 * wrapped in guard bytes. Checked after every input:
 *   - the guard bytes are untouched (the parser writes only the record);
 *   - record_encode_text() gives <= 300 printable ASCII characters;
 *   - the binary encoding fits LC_BIN_MAX and decodes to the same record;
 *   - record_decode_bin() on the raw input does not crash, and anything it
 *     accepts re-encodes and decodes to the same record.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/lc/record.h"
#include "../../src/lc/at_parse.h"
#include "lc_test_util.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static unsigned long g_fail;
static unsigned long g_updated, g_malformed, g_too_long, g_bin_ok; /* reach, for the log */

static void fuzz_fail(const char *msg)
{
    g_fail++;
#ifdef STANDALONE_FUZZ
    if (g_fail <= 5) {
        printf("fuzz: FAIL %s\n", msg);
    }
#else
    (void)msg;
    abort();
#endif
}

#define FUZZ_FAIL(msg) fuzz_fail(msg)

typedef struct {
    uint8_t pre[32];
    lc_record_t r;
    uint8_t post[32];
} guarded_t;

static void check_record(const lc_record_t *r)
{
    char txt[LC_TEXT_MAX + 8];
    uint8_t bin[LC_BIN_MAX + 8];
    lc_record_t back;
    int n;
    int i;

    n = record_encode_text(r, txt, sizeof txt);
    if (n < 0 || n > LC_TEXT_MAX || (size_t)n != strlen(txt)) {
        FUZZ_FAIL("text length");
    }
    for (i = 0; i < n; i++) {
        if (txt[i] < 0x20 || txt[i] > 0x7E) {
            FUZZ_FAIL("text character");
        }
    }

    n = record_encode_bin(r, bin, sizeof bin);
    if (n <= 0 || n > LC_BIN_MAX) {
        FUZZ_FAIL("binary size");
    } else if (record_decode_bin(bin, (size_t)n, &back) != 0) {
        FUZZ_FAIL("binary decode");
    } else if (!record_equal(r, &back)) {
        FUZZ_FAIL("binary round trip");
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    guarded_t g;
    at_parser_t p;
    lc_record_t d;
    unsigned i;

    mem_fill(g.pre, 0xA5, sizeof g.pre);
    mem_fill(g.post, 0x5A, sizeof g.post);
    lc_record_init(&g.r);
    at_parser_init(&p);

    (void)at_parse_line(&p, &g.r, (const char *)data, size, 1);
    at_parse_feed(&p, &g.r, (const char *)data, size, 2);
    /* A second pass with a pending FPLMN read, to reach +CRSM/+CSIM. */
    at_parse_expect(&p, AT_PEND_FPLMN);
    at_parse_feed(&p, &g.r, (const char *)data, size, 3);
    at_parse_feed(&p, &g.r, "\n", 1, 4);

    for (i = 0; i < sizeof g.pre; i++) {
        if (g.pre[i] != 0xA5 || g.post[i] != 0x5A) {
            FUZZ_FAIL("guard bytes");
        }
    }
    if (p.len > AT_LINE_MAX) {
        FUZZ_FAIL("line buffer index");
    }
    check_record(&g.r);
    g_updated += p.updated != 0;
    g_malformed += p.malformed != 0;
    g_too_long += p.too_long != 0;

    if (record_decode_bin(data, size, &d) == 0) {
        g_bin_ok++;
        check_record(&d);
    }
    return 0;
}

#ifdef STANDALONE_FUZZ
/* Seeds: realistic lines, so that mutation reaches the handlers. */
static const char *const k_seeds[] = {
    "+CEREG: 5,3,\"1A2B\",\"01A2B3C\",7,0,15",
    "+CEREG: 1,\"00C3\",\"0001F3A1\",7,,,\"00100100\",\"01000111\"",
    "+CEREG: 2",
    "+CEREG: 0,1",
    "+CESQ: 99,99,255,255,11,29,255,255,255",
    "+CEDRXRDP: 4,\"0101\",\"0010\",\"0011\"",
    "+CPSMS: 1,,,\"00100001\",\"00100001\"",
    "+COPS: 0,2,\"00101\",7",
    "+CRSM: 144,0,\"00F110001100FFFFFFFFFFFF\"",
    "+CSIM: 16,\"99F999FFFFFF9000\"",
    "%CEER: 0,\"EMM\",\"ATTACH\",\"REJECT\",15,\"No Suitable Cells\"",
    "%CEER: \"ESM\",\"PDN\",\"REJECT\",27",
    "%MEAS: Signal Quality: RSRP = -95, RSRQ = -11, SINR = 12, RSSI = -70",
    "AT+CRSM=176,28539,0,0,12",
    "OK",
    "+CME ERROR: 10",
    /* Added in review: satellite eDRX, out-of-range and negative values. */
    "+CEDRXRDP: 7,\"1111\",\"0000\",\"1111\"",
    "+CEREG: 3,\"1A2B\",\"01A2B3C\",15,0,256",
    "+CESQ: 99,99,255,255,-1,1234567890",
    "+CEREG: 0,-1",
};
static const char k_alphabet[] = "0123456789,\"-+:%=ABCDEFabcdef \r\n\t\x00\xFF";

static uint32_t g_rng = 0x1234567U;

static uint32_t rnd(void)
{
    /* xorshift32 */
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

static size_t make_input(uint8_t *buf, size_t cap)
{
    size_t n = 0;
    unsigned lines;
    unsigned l;
    unsigned m;
    unsigned muts;

    if (rnd() % 10 == 0) {
        /* raw random bytes */
        n = rnd() % cap;
        for (l = 0; l < n; l++) {
            buf[l] = (uint8_t)rnd();
        }
        return n;
    }
    lines = 1 + (rnd() % 4);
    for (l = 0; l < lines; l++) {
        const char *s = k_seeds[rnd() % (sizeof k_seeds / sizeof k_seeds[0])];
        size_t sl = strlen(s);
        if (n + sl + 2 > cap) {
            break;
        }
        mem_copy(&buf[n], s, sl);
        n += sl;
        if (rnd() % 8 == 0) {
            /* sometimes an over-long tail */
            size_t extra = rnd() % 300;
            while (extra-- && n + 2 < cap) {
                buf[n++] = (uint8_t)k_alphabet[rnd() % (sizeof k_alphabet - 1)];
            }
        }
        buf[n++] = '\r';
        buf[n++] = '\n';
    }
    muts = rnd() % 6;
    for (m = 0; m < muts && n; m++) {
        size_t at = rnd() % n;
        switch (rnd() % 4) {
        case 0: buf[at] = (uint8_t)k_alphabet[rnd() % (sizeof k_alphabet - 1)]; break;
        case 1: buf[at] ^= (uint8_t)(1U << (rnd() % 8)); break;
        case 2: n = at; break;                   /* truncate */
        default:                                  /* duplicate a byte */
            if (n < cap) {
                mem_move(&buf[at + 1], &buf[at], n - at);
                n++;
            }
            break;
        }
    }
    return n;
}

int main(void)
{
    static uint8_t buf[1024];
    const unsigned long iters = 200000UL;
    unsigned long i;

    for (i = 0; i < iters; i++) {
        size_t n = make_input(buf, sizeof buf);
        (void)LLVMFuzzerTestOneInput(buf, n);
    }
    /* Valid binaries with bit flips exercise the decoder past the CRC check
     * only rarely; feed the raw frames of a parsed record as well. */
    {
        lc_record_t r;
        at_parser_t p;
        uint8_t bin[LC_BIN_MAX];
        int n;
        int k;
        lc_record_init(&r);
        at_parser_init(&p);
        for (k = 0; k < (int)(sizeof k_seeds / sizeof k_seeds[0]); k++) {
            (void)at_parse_line(&p, &r, k_seeds[k], strlen(k_seeds[k]), (uint32_t)k);
        }
        n = record_encode_bin(&r, bin, sizeof bin);
        for (k = 0; k < n; k++) {
            (void)LLVMFuzzerTestOneInput(bin, (size_t)k);
        }
        (void)LLVMFuzzerTestOneInput(bin, (size_t)n);

        /* TLV-level mutation with the CRC fixed up, so the decoder's TLV
         * walk and range checks are reached (random bytes rarely pass CRC). */
        for (i = 0; i < 20000UL && n > 7; i++) {
            uint8_t mut[LC_BIN_MAX];
            unsigned m;
            unsigned muts = 1 + (rnd() % 4);
            uint16_t crc;
            lc_record_t d;
            mem_copy(mut, bin, (size_t)n);
            for (m = 0; m < muts; m++) {
                unsigned at = 5 + (rnd() % (unsigned)(n - 7));  /* two statements: */
                mut[at] = (uint8_t)rnd();                     /* fixed call order */
            }
            crc = lc_crc16(mut, (size_t)n - 2);
            mut[n - 2] = (uint8_t)(crc >> 8);
            mut[n - 1] = (uint8_t)crc;
            if (record_decode_bin(mut, (size_t)n, &d) == 0) {
                g_bin_ok++;
                check_record(&d);
            } else if (d.emm.value != LC_U16_UNKNOWN || d.uptime_s != LC_U32_UNKNOWN) {
                FUZZ_FAIL("rejected binary left data in the record");
            }
        }
    }
    printf("fuzz: %lu iterations, %lu failures (inputs that updated the record %lu, "
           "hit a malformed line %lu, an over-long line %lu; valid binaries %lu)\n",
           iters, g_fail, g_updated, g_malformed, g_too_long, g_bin_ok);
    return g_fail ? 1 : 0;
}
#endif
