/* at_parse.h - bounded, line-based AT response parser that updates a
 * Last Checkpoint record (record.h). No heap, no libc.
 *
 * Understood lines (syntax: 3GPP TS 27.007 V19.6.0 (2026-03), and the Murata
 * Type 1SC AT Command Reference Guide rev 1.1 (2024-06-25) for '%' lines):
 *   +CEREG: read response (<n>,<stat>,...) and URC (<stat>,...), n = 0..5
 *   +CESQ:  <rsrq>, <rsrp> (6 or 9 parameters)
 *   %MEAS:  "Signal Quality: RSRP = .., RSRQ = .., SINR = .., RSSI = .." (Murata)
 *   +CEDRXRDP: (AcT-type 0..9), +CPSMS: (read response), +COPS: (numeric format 2 only)
 *   %CEER:  read response and URC (Murata; see the UNVERIFIED note in at_parse.c)
 *   +CRSM: / +CSIM: carrying EF_FPLMN (6F7B), only while an FPLMN read is
 *          pending: after an echoed "AT+CRSM=176,28539,..." or at_parse_expect().
 *   OK, ERROR, +CME ERROR:, echoed "AT..." lines and blank lines: ignored.
 * Anything else is "unknown": ignored and counted.
 *
 * Lines longer than AT_LINE_MAX bytes (terminator not counted) are dropped
 * and counted. The parser writes only to the record and parser passed in.
 */
#ifndef LC_AT_PARSE_H
#define LC_AT_PARSE_H

#include <stddef.h>
#include <stdint.h>
#include "record.h"

#define AT_LINE_MAX 256U

/* at_parse_line() results */
#define AT_UPDATED    1   /* the record changed */
#define AT_IGNORED    0   /* a known line with nothing to store */
#define AT_UNKNOWN   (-1)  /* not a line this parser knows; counted */
#define AT_TOO_LONG  (-2)  /* over AT_LINE_MAX; dropped and counted */
#define AT_MALFORMED (-3)  /* a known prefix with bad parameters; counted */

/* Pending reads */
#define AT_PEND_NONE  0U
#define AT_PEND_FPLMN 1U

typedef struct {
    uint32_t lines;      /* non-blank lines seen */
    uint32_t updated;
    uint32_t too_long;
    uint32_t unknown;
    uint32_t malformed;
    uint8_t  pending;    /* AT_PEND_* */
    /* at_parse_feed() line assembly */
    uint8_t  discarding; /* inside an over-long line */
    uint16_t len;
    char     buf[AT_LINE_MAX];
} at_parser_t;

void at_parser_init(at_parser_t *p);

/* Tell the parser that the next +CRSM:/+CSIM: answer is an EF_FPLMN read
 * (for +CSIM: the response must end in the status word, 9000 or 91xx). */
void at_parse_expect(at_parser_t *p, uint8_t what);

/* One line without its terminator (a trailing CR/LF is tolerated).
 * now_s is the uptime used for stamps. */
int at_parse_line(at_parser_t *p, lc_record_t *r, const char *line, size_t len, uint32_t now_s);

/* Raw byte stream: splits on CR or LF and calls at_parse_line(). */
void at_parse_feed(at_parser_t *p, lc_record_t *r, const char *data, size_t n, uint32_t now_s);

/* GPRS Timer 2 / GPRS Timer 3 octet (TS 24.008 10.5.7.4 / 10.5.7.4a) to
 * seconds; LC_TIMER_DEACTIVATED for unit 111. */
uint32_t at_gprs_timer2_s(uint8_t octet);
uint32_t at_gprs_timer3_s(uint8_t octet);

#endif /* LC_AT_PARSE_H */
