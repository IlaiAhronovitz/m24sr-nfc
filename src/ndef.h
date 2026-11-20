/* ndef.h - NDEF records (NFC Forum NDEF 1.0, RTD URI 1.0, RTD Text 1.0).
 * Parse and build short records of the well-known types URI ("U") and
 * Text ("T"); other records are reported by type only. Host-testable. */
#ifndef M24SR_NFC_NDEF_H
#define M24SR_NFC_NDEF_H

#include <stdint.h>

/* TNF values, NDEF 1.0 section 3.2.6. */
#define NDEF_TNF_EMPTY      0U
#define NDEF_TNF_WELL_KNOWN 1U
#define NDEF_TNF_MIME       2U
#define NDEF_TNF_UNKNOWN    5U
#define NDEF_TNF_UNCHANGED  6U

/* ndef_parse_record() results */
#define NDEF_ERR_TRUNCATED  (-1)
#define NDEF_ERR_CHUNKED    (-2)  /* CF set: chunked records are not supported */
#define NDEF_ERR_HEADER     (-3)  /* lengths or TNF not allowed by NDEF 1.0 3.2.6 */

typedef struct {
    uint8_t        tnf;
    uint8_t        mb;               /* message begin */
    uint8_t        me;               /* message end */
    uint8_t        sr;               /* short record */
    const uint8_t *type;
    uint8_t        type_len;
    const uint8_t *id;
    uint8_t        id_len;
    const uint8_t *payload;
    uint32_t       payload_len;
} ndef_record_t;

/* Parse the record starting at msg[*pos]; on success advance *pos past it.
 * Returns 0, or a negative NDEF_ERR_* code. */
int ndef_parse_record(const uint8_t *msg, uint32_t len, uint32_t *pos, ndef_record_t *rec);

/* Well-known URI record: expand the prefix code into out (NUL-terminated).
 * Returns the string length, or -1 if rec is not a URI or out is too small. */
int ndef_uri_to_string(const ndef_record_t *rec, char *out, uint32_t out_size);

/* Well-known Text record: copy the language code and the UTF-8 text.
 * Returns the text length, or -1 if rec is not a UTF-8 Text record. */
int ndef_text_get(const ndef_record_t *rec, char *lang, uint32_t lang_size,
                  char *text, uint32_t text_size);

/* Build a one-record message (MB=ME=1, SR=1). The URI is given in full; the
 * longest known prefix is replaced by its code. Returns the message length,
 * or -1 if it does not fit in out (or is too long for a short record). */
int ndef_build_uri(const char *uri, uint8_t *out, uint32_t out_size);
int ndef_build_text(const char *lang, const char *text, uint8_t *out, uint32_t out_size);

#endif /* M24SR_NFC_NDEF_H */
