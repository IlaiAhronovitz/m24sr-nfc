/* ndef.c - NDEF record parser and builder (short records only on the build
 * side; the parser also accepts long records and ID fields). */
#include "ndef.h"

/* RTD URI 1.0, Table 3: identifier codes 0x00..0x23. */
static const char *const k_prefix[] = {
    "", "http://www.", "https://www.", "http://", "https://", "tel:", "mailto:",
    "ftp://anonymous:anonymous@", "ftp://ftp.", "ftps://", "sftp://", "smb://",
    "nfs://", "ftp://", "dav://", "news:", "telnet://", "imap:", "rtsp://", "urn:",
    "pop:", "sip:", "sips:", "tftp:", "btspp://", "btl2cap://", "btgoep://",
    "tcpobex://", "irdaobex://", "file://", "urn:epc:id:", "urn:epc:tag:",
    "urn:epc:pat:", "urn:epc:raw:", "urn:epc:", "urn:nfc:",
};
#define N_PREFIX (sizeof k_prefix / sizeof k_prefix[0])

static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

static int starts_with(const char *s, const char *p)
{
    while (*p) {
        if (*s++ != *p++) return 0;
    }
    return 1;
}

int ndef_parse_record(const uint8_t *msg, uint32_t len, uint32_t *pos, ndef_record_t *rec)
{
    uint32_t p = *pos;
    if (p + 3u > len) return -1;
    uint8_t hdr = msg[p++];
    if (hdr & 0x20u) return -2;                       /* CF: chunked */
    rec->mb = (hdr >> 7) & 1u;
    rec->me = (hdr >> 6) & 1u;
    rec->sr = (hdr >> 4) & 1u;
    rec->tnf = hdr & 7u;
    uint8_t il = (hdr >> 3) & 1u;

    rec->type_len = msg[p++];
    if (rec->sr) {
        rec->payload_len = msg[p++];
    } else {
        if (p + 4u > len) return -1;
        rec->payload_len = ((uint32_t)msg[p] << 24) | ((uint32_t)msg[p + 1] << 16)
                         | ((uint32_t)msg[p + 2] << 8) | msg[p + 3];
        p += 4u;
    }
    rec->id_len = 0;
    if (il) {
        if (p + 1u > len) return -1;
        rec->id_len = msg[p++];
    }
    if (p + rec->type_len + rec->id_len > len) return -1;
    rec->type = &msg[p];
    p += rec->type_len;
    rec->id = &msg[p];
    p += rec->id_len;
    if (rec->payload_len > len - p) return -1;
    rec->payload = &msg[p];
    p += rec->payload_len;
    *pos = p;
    return 0;
}

static int is_wk(const ndef_record_t *rec, char type)
{
    return rec->tnf == NDEF_TNF_WELL_KNOWN && rec->type_len == 1u && rec->type[0] == (uint8_t)type;
}

int ndef_uri_to_string(const ndef_record_t *rec, char *out, uint32_t out_size)
{
    if (!is_wk(rec, 'U') || rec->payload_len < 1u) return -1;
    uint8_t code = rec->payload[0];
    const char *prefix = code < N_PREFIX ? k_prefix[code] : "";
    uint32_t plen = slen(prefix), rest = rec->payload_len - 1u;
    if (plen + rest + 1u > out_size) return -1;
    uint32_t n = 0;
    for (uint32_t i = 0; i < plen; i++) out[n++] = prefix[i];
    for (uint32_t i = 0; i < rest; i++) out[n++] = (char)rec->payload[1 + i];
    out[n] = '\0';
    return (int)n;
}

int ndef_text_get(const ndef_record_t *rec, char *lang, uint32_t lang_size,
                  char *text, uint32_t text_size)
{
    if (!is_wk(rec, 'T') || rec->payload_len < 1u) return -1;
    uint8_t status = rec->payload[0];
    if (status & 0x80u) return -1;                    /* UTF-16 not supported */
    uint32_t llen = status & 0x3Fu;
    if (1u + llen > rec->payload_len || llen + 1u > lang_size) return -1;
    uint32_t tlen = rec->payload_len - 1u - llen;
    if (tlen + 1u > text_size) return -1;
    for (uint32_t i = 0; i < llen; i++) lang[i] = (char)rec->payload[1 + i];
    lang[llen] = '\0';
    for (uint32_t i = 0; i < tlen; i++) text[i] = (char)rec->payload[1 + llen + i];
    text[tlen] = '\0';
    return (int)tlen;
}

/* One short well-known record: header, type, payload prefix byte(s), body. */
static int build_short(char type, const uint8_t *head, uint32_t head_len,
                       const char *body, uint32_t body_len, uint8_t *out, uint32_t out_size)
{
    uint32_t payload_len = head_len + body_len;
    if (payload_len > 255u || 4u + payload_len > out_size) return -1;
    uint32_t n = 0;
    out[n++] = 0xD1u;                                 /* MB ME SR, TNF well-known */
    out[n++] = 1u;                                    /* type length */
    out[n++] = (uint8_t)payload_len;
    out[n++] = (uint8_t)type;
    for (uint32_t i = 0; i < head_len; i++) out[n++] = head[i];
    for (uint32_t i = 0; i < body_len; i++) out[n++] = (uint8_t)body[i];
    return (int)n;
}

int ndef_build_uri(const char *uri, uint8_t *out, uint32_t out_size)
{
    uint8_t best = 0;
    uint32_t best_len = 0;
    for (uint8_t c = 1; c < N_PREFIX; c++) {
        uint32_t l = slen(k_prefix[c]);
        if (l > best_len && starts_with(uri, k_prefix[c])) {
            best = c;
            best_len = l;
        }
    }
    return build_short('U', &best, 1u, uri + best_len, slen(uri) - best_len, out, out_size);
}

int ndef_build_text(const char *lang, const char *text, uint8_t *out, uint32_t out_size)
{
    uint8_t head[1 + 63];
    uint32_t llen = slen(lang);
    if (llen > 63u) return -1;
    head[0] = (uint8_t)llen;                          /* UTF-8, language length */
    for (uint32_t i = 0; i < llen; i++) head[1 + i] = (uint8_t)lang[i];
    return build_short('T', head, 1u + llen, text, slen(text), out, out_size);
}
