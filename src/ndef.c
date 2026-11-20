/* ndef.c - NDEF record parser and builder (short records only on the build
 * side; the parser also accepts long records and ID fields). */
#include "ndef.h"

/* Record header bits, NDEF 1.0 section 3.2. */
#define HDR_MB      0x80U
#define HDR_ME      0x40U
#define HDR_CF      0x20U
#define HDR_SR      0x10U
#define HDR_IL      0x08U
#define HDR_TNF     0x07U

#define SHORT_MAX   255U            /* SR payload length is one octet */
#define LANG_MAX    63U             /* Text RTD status bits 5..0 */
#define TEXT_UTF16  0x80U           /* Text RTD status bit 7 */

/* RTD URI 1.0, Table 3: identifier codes 0x00..0x23. */
static const char *const k_prefix[] = {
    "", "http://www.", "https://www.", "http://", "https://", "tel:", "mailto:",
    "ftp://anonymous:anonymous@", "ftp://ftp.", "ftps://", "sftp://", "smb://",
    "nfs://", "ftp://", "dav://", "news:", "telnet://", "imap:", "rtsp://", "urn:",
    "pop:", "sip:", "sips:", "tftp:", "btspp://", "btl2cap://", "btgoep://",
    "tcpobex://", "irdaobex://", "file://", "urn:epc:id:", "urn:epc:tag:",
    "urn:epc:pat:", "urn:epc:raw:", "urn:epc:", "urn:nfc:",
};
#define N_PREFIX ((uint32_t)(sizeof(k_prefix) / sizeof(k_prefix[0])))

static uint32_t slen(const char *s)
{
    uint32_t n = 0U;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

static int starts_with(const char *s, const char *p)
{
    for (uint32_t i = 0U; p[i] != '\0'; i++) {
        if (s[i] != p[i]) {          /* also stops at the end of s */
            return 0;
        }
    }
    return 1;
}

static uint8_t bit_of(uint8_t hdr, uint32_t mask)
{
    return ((hdr & mask) != 0U) ? 1U : 0U;
}

/* NDEF 1.0 section 3.2.6: an Empty record has no type, ID or payload; an
 * Unknown or Unchanged record has no type; Unchanged only appears in the
 * middle or last chunk of a chunked record, which this parser rejects. */
static int tnf_ok(const ndef_record_t *rec)
{
    if (rec->tnf == NDEF_TNF_EMPTY) {
        return ((rec->type_len == 0U) && (rec->id_len == 0U) && (rec->payload_len == 0U)) ? 1 : 0;
    }
    if (rec->tnf == NDEF_TNF_UNKNOWN) {
        return (rec->type_len == 0U) ? 1 : 0;
    }
    return (rec->tnf == NDEF_TNF_UNCHANGED) ? 0 : 1;
}

int ndef_parse_record(const uint8_t *msg, uint32_t len, uint32_t *pos, ndef_record_t *rec)
{
    uint32_t p = *pos;
    if ((len < 3U) || (p > (len - 3U))) {
        return NDEF_ERR_TRUNCATED;
    }
    uint8_t hdr = msg[p];
    p++;
    if ((hdr & HDR_CF) != 0U) {
        return NDEF_ERR_CHUNKED;
    }
    rec->mb = bit_of(hdr, HDR_MB);
    rec->me = bit_of(hdr, HDR_ME);
    rec->sr = bit_of(hdr, HDR_SR);
    rec->tnf = (uint8_t)(hdr & HDR_TNF);

    rec->type_len = msg[p];
    p++;
    if (rec->sr != 0U) {
        rec->payload_len = msg[p];
        p++;
    } else {
        if ((len - p) < 4U) {
            return NDEF_ERR_TRUNCATED;
        }
        rec->payload_len = ((uint32_t)msg[p] << 24U) | ((uint32_t)msg[p + 1U] << 16U)
                         | ((uint32_t)msg[p + 2U] << 8U) | (uint32_t)msg[p + 3U];
        p += 4U;
    }
    rec->id_len = 0U;
    if ((hdr & HDR_IL) != 0U) {
        if (p >= len) {
            return NDEF_ERR_TRUNCATED;
        }
        rec->id_len = msg[p];
        p++;
    }
    if (((uint32_t)rec->type_len + (uint32_t)rec->id_len) > (len - p)) {
        return NDEF_ERR_TRUNCATED;
    }
    rec->type = &msg[p];
    p += rec->type_len;
    rec->id = &msg[p];
    p += rec->id_len;
    if (rec->payload_len > (len - p)) {
        return NDEF_ERR_TRUNCATED;
    }
    if (tnf_ok(rec) == 0) {
        return NDEF_ERR_HEADER;
    }
    rec->payload = &msg[p];
    p += rec->payload_len;
    *pos = p;
    return 0;
}

static int is_wk(const ndef_record_t *rec, char type)
{
    return ((rec->tnf == NDEF_TNF_WELL_KNOWN) && (rec->type_len == 1U) &&
            (rec->type[0] == (uint8_t)type)) ? 1 : 0;
}

int ndef_uri_to_string(const ndef_record_t *rec, char *out, uint32_t out_size)
{
    if ((is_wk(rec, 'U') == 0) || (rec->payload_len < 1U)) {
        return -1;
    }
    uint8_t code = rec->payload[0];
    const char *prefix = (code < N_PREFIX) ? k_prefix[code] : "";   /* RFU: no prefix */
    uint32_t plen = slen(prefix);
    uint32_t rest = rec->payload_len - 1U;
    if ((plen + rest + 1U) > out_size) {
        return -1;
    }
    for (uint32_t i = 0U; prefix[i] != '\0'; i++) {     /* plen characters */
        out[i] = prefix[i];
    }
    for (uint32_t i = 0U; i < rest; i++) {
        out[plen + i] = (char)rec->payload[1U + i];
    }
    uint32_t n = plen + rest;
    out[n] = '\0';
    return (int)n;
}

int ndef_text_get(const ndef_record_t *rec, char *lang, uint32_t lang_size,
                  char *text, uint32_t text_size)
{
    if ((is_wk(rec, 'T') == 0) || (rec->payload_len < 1U)) {
        return -1;
    }
    uint8_t status = rec->payload[0];
    if ((status & TEXT_UTF16) != 0U) {
        return -1;                    /* UTF-16 not supported */
    }
    uint32_t llen = (uint32_t)status & LANG_MAX;
    if (((1U + llen) > rec->payload_len) || ((llen + 1U) > lang_size)) {
        return -1;
    }
    uint32_t tlen = rec->payload_len - 1U - llen;
    if ((tlen + 1U) > text_size) {
        return -1;
    }
    for (uint32_t i = 0U; i < llen; i++) {
        lang[i] = (char)rec->payload[1U + i];
    }
    lang[llen] = '\0';
    for (uint32_t i = 0U; i < tlen; i++) {
        text[i] = (char)rec->payload[1U + llen + i];
    }
    text[tlen] = '\0';
    return (int)tlen;
}

/* One short well-known record: header, type, payload prefix byte(s), body. */
static int build_short(char type, const uint8_t *head, uint32_t head_len,
                       const char *body, uint32_t body_len, uint8_t *out, uint32_t out_size)
{
    uint32_t payload_len = head_len + body_len;
    if ((payload_len > SHORT_MAX) || ((4U + payload_len) > out_size)) {
        return -1;
    }
    out[0] = (uint8_t)(HDR_MB | HDR_ME | HDR_SR | NDEF_TNF_WELL_KNOWN);   /* 0xD1 */
    out[1] = 1U;                                      /* type length */
    out[2] = (uint8_t)payload_len;
    out[3] = (uint8_t)type;
    for (uint32_t i = 0U; i < head_len; i++) {
        out[4U + i] = head[i];
    }
    for (uint32_t i = 0U; i < body_len; i++) {
        out[4U + head_len + i] = (uint8_t)body[i];
    }
    uint32_t n = 4U + payload_len;
    return (int)n;
}

int ndef_build_uri(const char *uri, uint8_t *out, uint32_t out_size)
{
    uint8_t best = 0U;
    uint32_t best_len = 0U;
    for (uint32_t c = 1U; c < N_PREFIX; c++) {
        uint32_t l = slen(k_prefix[c]);
        if ((l > best_len) && (starts_with(uri, k_prefix[c]) != 0)) {
            best = (uint8_t)c;
            best_len = l;
        }
    }
    return build_short('U', &best, 1U, &uri[best_len], slen(uri) - best_len, out, out_size);
}

int ndef_build_text(const char *lang, const char *text, uint8_t *out, uint32_t out_size)
{
    uint8_t head[1U + LANG_MAX];
    uint32_t llen = slen(lang);
    if (llen > LANG_MAX) {
        return -1;
    }
    head[0] = (uint8_t)llen;                          /* UTF-8, language length */
    for (uint32_t i = 0U; i < llen; i++) {
        head[1U + i] = (uint8_t)lang[i];
    }
    return build_short('T', head, 1U + llen, text, slen(text), out, out_size);
}
