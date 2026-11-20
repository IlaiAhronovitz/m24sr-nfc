/* at_parse.c - see at_parse.h. Every handler parses into locals, checks
 * them, then commits to the record; nothing is written through an index
 * derived from the input except bounded arrays inside the record. */
#include <stdbool.h>
#include "at_parse.h"

#define MAX_TOK 16U

typedef struct {
    const char *s;
    unsigned len;
    uint8_t quoted;
} tok_t;

typedef struct {
    tok_t t[MAX_TOK];
    unsigned n;
} toks_t;

/* What every handler may touch. */
typedef struct {
    at_parser_t *p;
    lc_record_t *r;
    uint32_t now;
} ctx_t;

/* ------------------------------------------------------------------------ */
/* Small helpers                                                            */
/* ------------------------------------------------------------------------ */

static bool starts(const char *s, size_t len, const char *pfx, size_t *after)
{
    size_t i = 0U;

    while (pfx[i] != '\0') {
        if ((i >= len) || (s[i] != pfx[i])) {
            return false;
        }
        i++;
    }
    *after = i;
    return true;
}

static bool is_space(char c)
{
    return (c == ' ') || (c == '\t');
}

static bool is_eol(char c)
{
    return (c == '\r') || (c == '\n');
}

static bool is_digit(char c)
{
    return (c >= '0') && (c <= '9');
}

/* Value of a decimal digit character; the caller has checked is_digit(). */
static uint32_t dec_val(char c)
{
    return (uint32_t)c - (uint32_t)'0';
}

static int32_t hexval(char c)
{
    int32_t v = -1;

    if (is_digit(c)) {
        v = (int32_t)dec_val(c);
    } else if ((c >= 'A') && (c <= 'F')) {
        v = ((int32_t)c - (int32_t)'A') + 10;
    } else if ((c >= 'a') && (c <= 'f')) {
        v = ((int32_t)c - (int32_t)'a') + 10;
    } else {
        /* not a hex digit */
    }
    return v;
}

/* c equals the upper-case letter upper_w, ignoring case. */
static bool same_ci(char c, char upper_w)
{
    bool eq = c == upper_w;

    if (!eq && (c >= 'a') && (c <= 'z')) {
        eq = ((int32_t)c - (int32_t)'a') == ((int32_t)upper_w - (int32_t)'A');
    }
    return eq;
}

/* One token starting at s[*pos]: blanks, then a quoted string or text up to
 * the next comma. Leaves *pos on the comma or at len. Returns false on an
 * unterminated quote or text after a closing quote. */
static bool scan_token(const char *s, size_t len, size_t *pos, tok_t *t)
{
    size_t i = *pos;
    size_t a;
    size_t b;
    uint8_t q = 0U;

    while ((i < len) && is_space(s[i])) {
        i++;
    }
    a = i;
    if ((i < len) && (s[i] == '"')) {
        q = 1U;
        i++;
        a = i;
        while ((i < len) && (s[i] != '"')) {
            i++;
        }
        if (i >= len) {
            return false;
        }
        b = i;
        i++;
        while ((i < len) && is_space(s[i])) {
            i++;
        }
        if ((i < len) && (s[i] != ',')) {
            return false;
        }
    } else {
        while ((i < len) && (s[i] != ',')) {
            i++;
        }
        b = i;
        while ((b > a) && is_space(s[b - 1U])) {
            b--;
        }
    }
    t->s = &s[a];
    t->len = (unsigned)(b - a);
    t->quoted = q;
    *pos = i;
    return true;
}

/* Splits s[0..len) on commas outside double quotes. Strips blanks and the
 * quotes. Returns false on an unterminated quote. Tokens past MAX_TOK are
 * dropped (no handler needs them). */
static bool tokenize(const char *s, size_t len, toks_t *out)
{
    size_t i = 0U;

    out->n = 0U;
    for (;;) {
        tok_t t;

        if (!scan_token(s, len, &i, &t)) {
            return false;
        }
        if (out->n < MAX_TOK) {
            out->t[out->n] = t;
            out->n++;
        }
        if (i >= len) {
            return true;
        }
        i++;                            /* the comma */
    }
}

static bool tok_empty(const toks_t *k, unsigned i)
{
    return (i >= k->n) || ((k->t[i].len == 0U) && (k->t[i].quoted == 0U));
}

/* Decimal integer in [lo, hi]. Accepts an optional '-' and nothing else. */
static bool int_in(const tok_t *t, int32_t lo, int32_t hi, int32_t *v)
{
    unsigned j = 0U;
    bool neg = false;
    int32_t acc = 0;

    if (t->len == 0U) {
        return false;
    }
    if (t->s[0] == '-') {
        neg = true;
        j = 1U;
    }
    if ((j >= t->len) || ((t->len - j) > 9U)) {
        return false;
    }
    for (; j < t->len; j++) {
        if (!is_digit(t->s[j])) {
            return false;
        }
        acc = (acc * 10) + (int32_t)dec_val(t->s[j]);
    }
    if (neg) {
        acc = -acc;
    }
    if ((acc < lo) || (acc > hi)) {
        return false;
    }
    *v = acc;
    return true;
}

static bool tok_int(const toks_t *k, unsigned i, int32_t lo, int32_t hi, int32_t *v)
{
    return (i < k->n) && int_in(&k->t[i], lo, hi, v);
}

/* Hex string token of 1..maxd digits. */
static bool tok_hex(const toks_t *k, unsigned i, unsigned maxd, uint32_t *v)
{
    const tok_t *t;
    unsigned j;
    uint32_t acc = 0U;

    if (i >= k->n) {
        return false;
    }
    t = &k->t[i];
    if ((t->len == 0U) || (t->len > maxd)) {
        return false;
    }
    for (j = 0U; j < t->len; j++) {
        int32_t h = hexval(t->s[j]);
        if (h < 0) {
            return false;
        }
        acc = (acc << 4U) | (uint32_t)h;
    }
    *v = acc;
    return true;
}

/* Bit string token of exactly nbits '0'/'1' characters. */
static bool tok_bits(const toks_t *k, unsigned i, unsigned nbits, uint8_t *v)
{
    const tok_t *t;
    unsigned j;
    uint32_t acc = 0U;

    if (i >= k->n) {
        return false;
    }
    t = &k->t[i];
    if (t->len != nbits) {
        return false;
    }
    for (j = 0U; j < nbits; j++) {
        if ((t->s[j] != '0') && (t->s[j] != '1')) {
            return false;
        }
        acc = (acc << 1U) | ((t->s[j] == '1') ? 1U : 0U);
    }
    *v = (uint8_t)acc;
    return true;
}

/* Case-insensitive "token contains word" (word in upper case). */
static bool tok_has(const toks_t *k, unsigned i, const char *w)
{
    const tok_t *t;
    unsigned a;

    if (i >= k->n) {
        return false;
    }
    t = &k->t[i];
    for (a = 0U; a < t->len; a++) {
        unsigned j = 0U;
        while ((w[j] != '\0') && ((a + j) < t->len) && same_ci(t->s[a + j], w[j])) {
            j++;
        }
        if (w[j] == '\0') {
            return true;
        }
    }
    return false;
}

uint32_t at_gprs_timer2_s(uint8_t octet)
{
    uint32_t v = (uint32_t)octet & 0x1FU;
    uint32_t s;

    switch ((uint32_t)octet >> 5U) {
    case 0U:
        s = v * 2U;
        break;
    case 2U:
        s = v * 360U;
        break;
    case 7U:
        s = LC_TIMER_DEACTIVATED;
        break;
    default:                            /* 001, and "other values: 1 minute" */
        s = v * 60U;
        break;
    }
    return s;
}

uint32_t at_gprs_timer3_s(uint8_t octet)
{
    /* Units 000..110: 10 min, 1 h, 10 h, 2 s, 30 s, 1 min, 320 h. 111 = off. */
    static const uint32_t unit_s[7] = { 600U, 3600U, 36000U, 2U, 30U, 60U, 1152000U };
    uint32_t unit = (uint32_t)octet >> 5U;

    if (unit >= 7U) {
        return LC_TIMER_DEACTIVATED;
    }
    return ((uint32_t)octet & 0x1FU) * unit_s[unit];
}

/* 27.007 <AcT> to LC_RAT_*. */
static uint8_t rat_from_act(int32_t act)
{
    uint8_t rat;

    switch (act) {
    case 0: case 1: case 3: case 8:
        rat = (uint8_t)LC_RAT_GSM;
        break;
    case 7: case 10: case 13:
        rat = (uint8_t)LC_RAT_LTE;
        break;
    case 9: case 14:
        rat = (uint8_t)LC_RAT_NB_IOT;
        break;
    case 15:
        rat = (uint8_t)LC_RAT_LTE_M;
        break;
    case 11: case 12: case 16:
        rat = (uint8_t)LC_RAT_NR;
        break;
    default:
        rat = (uint8_t)LC_RAT_OTHER;
        break;
    }
    return rat;
}

/* Keeps a finer RAT (LTE-M learnt from +CEDRXRDP) when +CEREG says "E-UTRAN". */
static void set_rat(lc_record_t *r, uint8_t rat)
{
    if ((rat == (uint8_t)LC_RAT_LTE) && (r->rat == (uint8_t)LC_RAT_LTE_M)) {
        return;
    }
    r->rat = rat;
}

/* ------------------------------------------------------------------------ */
/* +CEREG: parse into a local struct, then commit                           */
/* ------------------------------------------------------------------------ */

typedef struct {
    int32_t stat;
    int32_t act;          /* -1 = not given */
    int32_t ctype;        /* -1 = not given */
    int32_t cause;        /* -1 = not given */
    uint32_t tac;         /* LC_U32_UNKNOWN = not given */
    uint32_t ci;          /* LC_U32_UNKNOWN = not given or not available */
    uint8_t at_oct;
    uint8_t tau_oct;
    bool have_at;
    bool have_tau;
} cereg_t;

/* <tac>, <ci>. A <ci> above 28 bits (FFFFFFFF = none) is "not available". */
static bool cereg_cell(const toks_t *k, unsigned b, cereg_t *c)
{
    uint32_t v;

    c->tac = LC_U32_UNKNOWN;
    c->ci = LC_U32_UNKNOWN;
    if (!tok_empty(k, b + 1U)) {
        if (!tok_hex(k, b + 1U, 4U, &v)) {
            return false;
        }
        c->tac = v;
    }
    if (!tok_empty(k, b + 2U)) {
        if (!tok_hex(k, b + 2U, 8U, &v)) {
            return false;
        }
        c->ci = (v <= 0x0FFFFFFFU) ? v : LC_U32_UNKNOWN;
    }
    return true;
}

/* <AcT>, <cause_type>, <reject_cause>. */
static bool cereg_cause(const toks_t *k, unsigned b, cereg_t *c)
{
    c->act = -1;
    c->ctype = -1;
    c->cause = -1;
    if (!tok_empty(k, b + 3U) && !tok_int(k, b + 3U, 0, 255, &c->act)) {
        return false;
    }
    if (!tok_empty(k, b + 4U) && !tok_int(k, b + 4U, 0, 1, &c->ctype)) {
        return false;
    }
    if (!tok_empty(k, b + 5U) && !tok_int(k, b + 5U, 0, 65534, &c->cause)) {
        return false;
    }
    return !((c->ctype == 0) && (c->cause > 255));  /* an EMM cause is one octet */
}

/* <Active-Time>, <Periodic-TAU>: 8-character bit strings. */
static bool cereg_timers(const toks_t *k, unsigned b, cereg_t *c)
{
    c->at_oct = 0U;
    c->tau_oct = 0U;
    c->have_at = !tok_empty(k, b + 6U);
    if (c->have_at && !tok_bits(k, b + 6U, 8U, &c->at_oct)) {
        return false;
    }
    c->have_tau = !tok_empty(k, b + 7U);
    return !c->have_tau || tok_bits(k, b + 7U, 8U, &c->tau_oct);
}

/* Read response "<n>,<stat>,..." has an unquoted integer second; the URC
 * "<stat>[,<tac>..." has a quoted or empty second parameter. */
static bool cereg_parse(const toks_t *k, cereg_t *c)
{
    unsigned b = 0U;                    /* index of <stat> */

    if ((k->n >= 2U) && (k->t[1].quoted == 0U) && (k->t[1].len > 0U)) {
        int32_t n;
        if (!tok_int(k, 0U, 0, 9, &n)) {
            return false;
        }
        b = 1U;
    }
    return tok_int(k, b, 0, 13, &c->stat) && cereg_cell(k, b, c) &&
           cereg_cause(k, b, c) && cereg_timers(k, b, c);
}

static void cereg_commit_cause(lc_record_t *r, const cereg_t *c, int32_t prev, uint32_t now)
{
    uint8_t ns;
    bool fresh_denial;

    if ((c->ctype < 0) || (c->cause < 0)) {
        return;
    }
    ns = (c->ctype == 0) ? (uint8_t)LC_NS_EMM : (uint8_t)LC_NS_VENDOR;
    fresh_denial = (c->stat == LC_REG_DENIED) && (prev != LC_REG_DENIED);
    /* A modem repeats the last cause in every report; stamp it only when
     * it is new or comes with a fresh denial. */
    if ((r->emm.ns != ns) || (r->emm.value != (uint16_t)c->cause) || fresh_denial) {
        lc_record_set_emm(r, ns, (uint16_t)c->cause, now);
    } else if ((c->stat == LC_REG_DENIED) && (ns == (uint8_t)LC_NS_EMM)) {
        r->backoff = lc_backoff_for_emm((uint16_t)c->cause);
    } else {
        /* the same cause again: nothing new */
    }
}

static void cereg_commit_stat(lc_record_t *r, int32_t stat, int32_t prev, uint32_t now)
{
    if ((stat == LC_REG_HOME) || (stat == LC_REG_ROAMING)) {
        /* The cause stays as evidence; only the counters reset. */
        r->attempts = 0U;
        r->backoff = (uint8_t)LC_BO_NONE;
        r->last_success_s = now;
    } else if ((stat == LC_REG_DENIED) && (prev != LC_REG_DENIED)) {
        if (r->attempts == LC_U16_UNKNOWN) {
            r->attempts = 1U;
        } else if (r->attempts < 0xFFFEU) {
            r->attempts = (uint16_t)(r->attempts + 1U);
        } else {
            /* saturated */
        }
        if ((r->backoff == (uint8_t)LC_BO_UNKNOWN) || (r->backoff == (uint8_t)LC_BO_NONE)) {
            r->backoff = (uint8_t)LC_BO_RETRY;
        }
    } else {
        /* searching, not registered, or a repeated denial */
    }
    r->reg_stat = (uint8_t)stat;
    r->reg_stat_s = now;
}

static void cereg_commit(lc_record_t *r, const cereg_t *c, uint32_t now)
{
    int32_t prev = (int32_t)r->reg_stat;

    if (c->act >= 0) {
        set_rat(r, rat_from_act(c->act));
    }
    if ((c->tac != LC_U32_UNKNOWN) || (c->ci != LC_U32_UNKNOWN)) {
        if (c->tac != LC_U32_UNKNOWN) {
            r->tac = c->tac;
        }
        if (c->ci != LC_U32_UNKNOWN) {
            r->cell_id = c->ci;
        }
        r->cell_s = now;
    }
    cereg_commit_cause(r, c, prev, now);
    cereg_commit_stat(r, c->stat, prev, now);
    if (c->have_at) {
        r->t3324_nw_s = at_gprs_timer2_s(c->at_oct);
    }
    if (c->have_tau) {
        r->t3412_nw_s = at_gprs_timer3_s(c->tau_oct);
    }
    r->uptime_s = now;
}

/* ------------------------------------------------------------------------ */
/* Handlers. Each returns AT_UPDATED, AT_IGNORED or AT_MALFORMED.           */
/* ------------------------------------------------------------------------ */

static int h_cereg(const ctx_t *x, const toks_t *k)
{
    cereg_t c;

    lc_record_t *r = x->r;

    if (!cereg_parse(k, &c)) {
        return AT_MALFORMED;
    }
    cereg_commit(r, &c, x->now);
    return AT_UPDATED;
}

static int h_cesq(const ctx_t *x, const toks_t *k)
{
    int32_t rsrq;
    int32_t rsrp;
    bool changed = false;

    if (k->n < 6U) {
        return AT_MALFORMED;
    }
    if (!tok_int(k, 4U, 0, 255, &rsrq) || !tok_int(k, 5U, 0, 255, &rsrp)) {
        return AT_MALFORMED;
    }
    /* 255 = not known: keep the last known value (it is stamped by cell_s). */
    if (rsrq <= 34) {
        x->r->rsrq_ddb = (int16_t)(-200 + (5 * rsrq));    /* index 1 = -19.5 dB */
        changed = true;
    }
    if (rsrp <= 97) {
        x->r->rsrp_dbm = (int16_t)(rsrp - 141);           /* index 1 = -140 dBm */
        changed = true;
    }
    if (!changed) {
        return AT_IGNORED;
    }
    x->r->cell_s = x->now;
    x->r->uptime_s = x->now;
    return AT_UPDATED;
}

typedef struct {
    int16_t rsrp;
    int16_t rsrq;
    int16_t snr;
} meas_t;

static bool name_is(const tok_t *t, const char *n4)
{
    return (t->len >= 4U) && (t->s[0] == n4[0]) && (t->s[1] == n4[1]) &&
           (t->s[2] == n4[2]) && (t->s[3] == n4[3]);
}

/* One "NAME = value" item of %MEAS. Returns false if it is malformed. */
static bool meas_item(const tok_t *t, meas_t *m)
{
    unsigned e = 0U;
    tok_t val;
    int32_t v;

    while ((e < t->len) && (t->s[e] != '=')) {
        e++;
    }
    if (e >= t->len) {
        return false;
    }
    val.s = &t->s[e + 1U];
    val.len = t->len - e - 1U;
    val.quoted = 0U;
    while ((val.len > 0U) && is_space(val.s[0])) {
        val.s = &val.s[1];
        val.len--;
    }
    if ((val.len >= 1U) && (val.s[0] == 'N')) {
        return true;                    /* N/A, N/S */
    }
    if (!int_in(&val, -260, 400, &v)) {
        return false;
    }
    if (name_is(t, "RSRP")) {
        if ((v >= -156) && (v <= 0)) {
            m->rsrp = (int16_t)v;
        }
    } else if (name_is(t, "RSRQ")) {
        if ((v >= -60) && (v <= 3)) {
            m->rsrq = (int16_t)(v * 10);
        }
    } else if (name_is(t, "SINR")) {
        if ((v >= -128) && (v <= 40)) {
            m->snr = (int16_t)(v * 10);
        }
    } else {
        /* RSSI and others: not stored */
    }
    return true;
}

static int meas_commit(const ctx_t *x, const meas_t *m)
{
    bool changed = false;

    if (m->rsrp != LC_I16_UNKNOWN) {
        x->r->rsrp_dbm = m->rsrp;
        changed = true;
    }
    if (m->rsrq != LC_I16_UNKNOWN) {
        x->r->rsrq_ddb = m->rsrq;
        changed = true;
    }
    if (m->snr != LC_I16_UNKNOWN) {
        x->r->snr_ddb = m->snr;
        changed = true;
    }
    if (!changed) {
        return AT_IGNORED;
    }
    x->r->cell_s = x->now;
    x->r->uptime_s = x->now;
    return AT_UPDATED;
}

/* "%MEAS: Signal Quality: RSRP = -112, RSRQ = -14, SINR = 3, RSSI = -80" */
static int h_meas(const ctx_t *x, const char *s, size_t len)
{
    static const char sq[] = "Signal Quality:";
    toks_t k;
    size_t a;
    size_t st = 0U;
    unsigned i;
    meas_t m = { LC_I16_UNKNOWN, LC_I16_UNKNOWN, LC_I16_UNKNOWN };

    while ((st < len) && is_space(s[st])) {
        st++;
    }
    if (!starts(&s[st], len - st, sq, &a)) {
        return AT_IGNORED;              /* other %MEAS forms: not used */
    }
    st += a;
    if (!tokenize(&s[st], len - st, &k)) {
        return AT_MALFORMED;
    }
    for (i = 0U; i < k.n; i++) {
        if (!meas_item(&k.t[i], &m)) {
            return AT_MALFORMED;
        }
    }
    return meas_commit(x, &m);
}

static int h_cedrxrdp(const ctx_t *x, const toks_t *k)
{
    lc_record_t *r = x->r;
    int32_t act;
    uint8_t req = LC_U8_UNKNOWN;
    uint8_t nw = LC_U8_UNKNOWN;
    uint8_t ptw = LC_U8_UNKNOWN;

    if (!tok_int(k, 0U, 0, LC_EDRX_ACT_MAX, &act)) {
        return AT_MALFORMED;
    }
    if (!tok_empty(k, 1U) && !tok_bits(k, 1U, 4U, &req)) {
        return AT_MALFORMED;
    }
    if (!tok_empty(k, 2U) && !tok_bits(k, 2U, 4U, &nw)) {
        return AT_MALFORMED;
    }
    if (!tok_empty(k, 3U) && !tok_bits(k, 3U, 4U, &ptw)) {
        return AT_MALFORMED;
    }
    r->edrx_act = (uint8_t)act;
    r->edrx_req = req;
    r->edrx_nw = nw;
    r->ptw_nw = ptw;
    /* (Satellite) WB-S1 refines "E-UTRAN" to LTE-M; (satellite) NB-S1 is NB-IoT. */
    if (((act == 4) || (act == 7)) &&
        ((r->rat == (uint8_t)LC_RAT_LTE) || (r->rat == (uint8_t)LC_RAT_UNKNOWN))) {
        r->rat = (uint8_t)LC_RAT_LTE_M;
    } else if ((act == 5) || (act == 6)) {
        r->rat = (uint8_t)LC_RAT_NB_IOT;
    } else {
        /* other AcT-types: the RAT stays */
    }
    r->uptime_s = x->now;
    return AT_UPDATED;
}

static int h_cpsms(const ctx_t *x, const toks_t *k)
{
    int32_t mode;
    uint8_t tau = 0U;
    uint8_t at = 0U;
    bool have_tau;
    bool have_at;

    if ((k->n < 1U) || !tok_int(k, 0U, 0, 1, &mode)) {
        return AT_MALFORMED;
    }
    have_tau = !tok_empty(k, 3U);
    if (have_tau && !tok_bits(k, 3U, 8U, &tau)) {
        return AT_MALFORMED;
    }
    have_at = !tok_empty(k, 4U);
    if (have_at && !tok_bits(k, 4U, 8U, &at)) {
        return AT_MALFORMED;
    }
    x->r->psm_mode_req = (uint8_t)mode;
    x->r->t3412_req_s = have_tau ? at_gprs_timer3_s(tau) : LC_U32_UNKNOWN;
    x->r->t3324_req_s = have_at ? at_gprs_timer2_s(at) : LC_U32_UNKNOWN;
    x->r->uptime_s = x->now;
    return AT_UPDATED;
}

/* Field by field: a struct copy would also copy padding bytes, and the
 * record is compared bytewise after a binary round trip. */
static void plmn_put(lc_plmn_t *dst, const lc_plmn_t *src)
{
    dst->mcc = src->mcc;
    dst->mnc = src->mnc;
    dst->mnc_digits = src->mnc_digits;
}

/* "MCCMNC" digits -> PLMN. */
static bool plmn_from_digits(const tok_t *t, lc_plmn_t *p)
{
    unsigned i;
    uint32_t mcc = 0U;
    uint32_t mnc = 0U;

    if ((t->len != 5U) && (t->len != 6U)) {
        return false;
    }
    for (i = 0U; i < t->len; i++) {
        if (!is_digit(t->s[i])) {
            return false;
        }
        if (i < 3U) {
            mcc = (mcc * 10U) + dec_val(t->s[i]);
        } else {
            mnc = (mnc * 10U) + dec_val(t->s[i]);
        }
    }
    p->mcc = (uint16_t)mcc;
    p->mnc = (uint16_t)mnc;
    p->mnc_digits = (uint8_t)(t->len - 3U);
    return true;
}

static int h_cops(const ctx_t *x, const toks_t *k)
{
    int32_t mode;
    int32_t fmt;
    int32_t act = -1;
    lc_plmn_t p;
    lc_record_t *r = x->r;

    if (!tok_int(k, 0U, 0, 4, &mode)) {
        return AT_MALFORMED;
    }
    if (k->n < 3U) {
        return AT_IGNORED;              /* no operator selected */
    }
    if (!tok_int(k, 1U, 0, 2, &fmt)) {
        return AT_MALFORMED;
    }
    if (fmt != 2) {
        return AT_IGNORED;              /* names carry no MCC/MNC */
    }
    if (!plmn_from_digits(&k->t[2], &p)) {
        return AT_MALFORMED;
    }
    if (!tok_empty(k, 3U) && !tok_int(k, 3U, 0, 255, &act)) {
        return AT_MALFORMED;
    }
    plmn_put(&r->plmn, &p);
    if (act >= 0) {
        set_rat(r, rat_from_act(act));
    }
    r->cell_s = x->now;
    r->uptime_s = x->now;
    return AT_UPDATED;
}

#define BCD_EMPTY  0    /* FFFFFF: an unused EF_FPLMN slot */
#define BCD_PLMN   1
#define BCD_BAD    (-1)

/* One 3-byte PLMN, BCD as in TS 24.008 10.5.1.3: MCC2|MCC1, MNC3|MCC3,
 * MNC2|MNC1 (high|low nibble), MNC3 = F for a 2-digit MNC. Only FFFFFF is an
 * empty slot; any other non-decimal digit is BCD_BAD. */
static int bcd_plmn(const char *hex, lc_plmn_t *p)
{
    uint32_t d[6];                      /* MCC2 MCC1 MNC3 MCC3 MNC2 MNC1 */
    unsigned j;
    bool all_f = true;

    for (j = 0U; j < 6U; j++) {
        int32_t h = hexval(hex[j]);
        if (h < 0) {
            return BCD_BAD;
        }
        d[j] = (uint32_t)h;
        all_f = all_f && (d[j] == 0xFU);
    }
    if (all_f) {
        return BCD_EMPTY;
    }
    if ((d[1] > 9U) || (d[0] > 9U) || (d[3] > 9U) || (d[5] > 9U) || (d[4] > 9U) ||
        ((d[2] > 9U) && (d[2] != 0xFU))) {
        return BCD_BAD;
    }
    p->mcc = (uint16_t)((d[1] * 100U) + (d[0] * 10U) + d[3]);
    if (d[2] == 0xFU) {
        p->mnc = (uint16_t)((d[5] * 10U) + d[4]);
        p->mnc_digits = 2U;
    } else {
        p->mnc = (uint16_t)((d[5] * 100U) + (d[4] * 10U) + d[2]);
        p->mnc_digits = 3U;
    }
    return BCD_PLMN;
}

static void fplmn_store(lc_record_t *r, const lc_plmn_t *list, unsigned n)
{
    unsigned i;

    r->fplmn_count = (uint8_t)n;
    for (i = 0U; i < (unsigned)LC_FPLMN_MAX; i++) {
        if (i < n) {
            plmn_put(&r->fplmn[i], &list[i]);
        } else {                        /* no stale entries from a longer read */
            r->fplmn[i].mcc = LC_U16_UNKNOWN;
            r->fplmn[i].mnc = LC_U16_UNKNOWN;
            r->fplmn[i].mnc_digits = LC_U8_UNKNOWN;
        }
    }
}

/* EF_FPLMN content, TS 31.102 4.2.16: 3 bytes per PLMN. The whole answer is
 * checked before anything is stored: one bad entry leaves the record as it
 * was. Entries past LC_FPLMN_MAX are checked but not kept. */
static int h_fplmn_hex(const ctx_t *x, const char *hex, unsigned hlen)
{
    lc_plmn_t list[LC_FPLMN_MAX];
    lc_plmn_t one;
    unsigned i;
    unsigned n = 0U;

    if (((hlen % 6U) != 0U) || (hlen > (2U * 255U))) {
        return AT_MALFORMED;
    }
    one.mcc = LC_U16_UNKNOWN;           /* set by bcd_plmn(); quiets gcc 5 */
    one.mnc = LC_U16_UNKNOWN;
    one.mnc_digits = LC_U8_UNKNOWN;
    for (i = 0U; i < hlen; i += 6U) {
        int rc = bcd_plmn(&hex[i], &one);
        if (rc == BCD_BAD) {
            return AT_MALFORMED;
        }
        if ((rc == BCD_PLMN) && (n < (unsigned)LC_FPLMN_MAX)) {
            plmn_put(&list[n], &one);
            n++;
        }
    }
    lc_record_t *r = x->r;

    fplmn_store(r, list, n);
    r->uptime_s = x->now;
    return AT_UPDATED;
}

/* 90 00, or 91 xx (normal ending with extra information). */
static bool sw_ok(int32_t sw1, int32_t sw2)
{
    return ((sw1 == 0x90) && (sw2 == 0)) || (sw1 == 0x91);
}

static int h_crsm(const ctx_t *x, const toks_t *k)
{
    int32_t sw1;
    int32_t sw2;

    if (x->p->pending != AT_PEND_FPLMN) {
        return AT_IGNORED;
    }
    x->p->pending = AT_PEND_NONE;
    if (!tok_int(k, 0U, 0, 255, &sw1) || !tok_int(k, 1U, 0, 255, &sw2)) {
        return AT_MALFORMED;
    }
    if (!sw_ok(sw1, sw2)) {
        return AT_IGNORED;              /* SIM refused: nothing learnt */
    }
    if (k->n < 3U) {
        return AT_MALFORMED;
    }
    return h_fplmn_hex(x, k->t[2].s, k->t[2].len);
}

/* Two hex digits at s[0..1] as a byte, or -1. */
static int32_t hex_byte(const char *s)
{
    int32_t hi = hexval(s[0]);
    int32_t lo = hexval(s[1]);

    if ((hi < 0) || (lo < 0)) {
        return -1;
    }
    return (hi * 16) + lo;
}

static int h_csim(const ctx_t *x, const toks_t *k)
{
    int32_t n;
    const tok_t *t;
    int32_t s1;
    int32_t s2;

    if (x->p->pending != AT_PEND_FPLMN) {
        return AT_IGNORED;
    }
    x->p->pending = AT_PEND_NONE;
    if ((k->n < 2U) || !tok_int(k, 0U, 4, 512, &n)) {
        return AT_MALFORMED;
    }
    t = &k->t[1];
    if (((int32_t)t->len != n) || (t->len < 4U)) {
        return AT_MALFORMED;
    }
    s1 = hex_byte(&t->s[t->len - 4U]);
    s2 = hex_byte(&t->s[t->len - 2U]);
    if ((s1 < 0) || (s2 < 0)) {
        return AT_MALFORMED;
    }
    if (!sw_ok(s1, s2)) {
        return AT_IGNORED;
    }
    return h_fplmn_hex(x, t->s, t->len - 4U);
}

/* %CEER, Murata Type 1SC AT Command Reference Guide rev 1.1, 3.3.35,
 * Table 3-40 (p. 70):
 *   read: %CEER: <mode>[,<module>,<procedure>,<failure>[,[<reject_cause>][,[<error_info>]
 *                [,<EARFCN>,<pci>,<oper>,<tac>]]]]
 *   URC:  %CEER: <module>,<procedure>,<failure>[,[<reject_cause>][,[<error_info>]...]]
 * <module> is "protocol layer/entity", <reject_cause> is "<module> specific
 * text/integer". UNVERIFIED: the manual lists no <module> values. This code
 * assumes a module string containing "EMM" or "ESM" carries a TS 24.301
 * cause; any other module is ignored. Check against a bench capture. */
static int h_ceer(const ctx_t *x, const toks_t *k)
{
    unsigned b = 0U;
    int32_t mode;
    int32_t cause;
    bool esm;
    bool emm;
    lc_record_t *r = x->r;

    if ((k->n >= 1U) && (k->t[0].quoted == 0U) && tok_int(k, 0U, 0, 1, &mode)) {
        if (k->n == 1U) {
            return AT_IGNORED;          /* "%CEER: 0": no stored failure */
        }
        b = 1U;
    }
    if (k->n < (b + 3U)) {
        return AT_MALFORMED;
    }
    if (tok_empty(k, b + 3U) || !tok_int(k, b + 3U, 0, 65534, &cause)) {
        return AT_IGNORED;              /* no numeric cause */
    }
    esm = tok_has(k, b, "ESM");
    emm = tok_has(k, b, "EMM");
    if ((esm || emm) && (cause > 255)) {
        return AT_MALFORMED;            /* a TS 24.301 cause is one octet */
    }
    if (esm) {
        lc_record_set_esm(r, (uint16_t)cause, x->now);
        return AT_UPDATED;
    }
    if (emm) {
        if ((r->emm.ns != (uint8_t)LC_NS_EMM) || (r->emm.value != (uint16_t)cause)) {
            lc_record_set_emm(r, (uint8_t)LC_NS_EMM, (uint16_t)cause, x->now);
        }
        return AT_UPDATED;
    }
    return AT_IGNORED;
}

/* ------------------------------------------------------------------------ */
/* Dispatch                                                                 */
/* ------------------------------------------------------------------------ */

void at_parser_init(at_parser_t *p)
{
    p->lines = 0U;
    p->updated = 0U;
    p->too_long = 0U;
    p->unknown = 0U;
    p->malformed = 0U;
    p->pending = AT_PEND_NONE;
    p->discarding = 0U;
    p->len = 0U;
}

void at_parse_expect(at_parser_t *p, uint8_t what)
{
    p->pending = what;
}

/* A command echo. 28539 = 0x6F7B, EF_FPLMN; 176 = READ BINARY. */
static bool is_echo(at_parser_t *p, const char *s, size_t len)
{
    size_t a;
    size_t b;

    if (!starts(s, len, "AT", &a) && !starts(s, len, "at", &a)) {
        return false;
    }
    if (starts(&s[a], len - a, "+CRSM=176,28539,", &b)) {
        p->pending = AT_PEND_FPLMN;
    }
    return true;
}

/* A final result code: it ends any pending read. */
static bool is_final(const char *s, size_t len)
{
    size_t a;

    return ((len == 2U) && (s[0] == 'O') && (s[1] == 'K')) ||
           ((len == 5U) && starts(s, len, "ERROR", &a)) ||
           starts(s, len, "+CME ERROR:", &a) || starts(s, len, "+CMS ERROR:", &a);
}

typedef int (*handler_fn)(const ctx_t *x, const toks_t *k);

typedef struct {
    const char *prefix;
    handler_fn fn;
} handler_t;

/* The responses with comma-separated parameters after the prefix. */
static int dispatch_tokens(const ctx_t *x, const char *s, size_t len)
{
    static const handler_t handlers[] = {
        { "+CEREG:", h_cereg }, { "+CESQ:", h_cesq }, { "+CEDRXRDP:", h_cedrxrdp },
        { "+CPSMS:", h_cpsms }, { "+COPS:", h_cops }, { "+CRSM:", h_crsm },
        { "+CSIM:", h_csim }, { "%CEER:", h_ceer },
    };
    size_t a;
    unsigned i;

    for (i = 0U; i < (sizeof(handlers) / sizeof(handlers[0])); i++) {
        if (starts(s, len, handlers[i].prefix, &a)) {
            toks_t k;
            if (!tokenize(&s[a], len - a, &k)) {
                return AT_MALFORMED;
            }
            return handlers[i].fn(x, &k);
        }
    }
    return AT_UNKNOWN;
}

static int dispatch(const ctx_t *x, const char *s, size_t len)
{
    size_t a;
    at_parser_t *p = x->p;

    if (is_echo(p, s, len)) {
        return AT_IGNORED;
    }
    if (is_final(s, len)) {
        x->p->pending = AT_PEND_NONE;
        return AT_IGNORED;
    }
    if (starts(s, len, "%MEAS:", &a)) {
        return h_meas(x, &s[a], len - a);
    }
    return dispatch_tokens(x, s, len);
}

static void count_result(at_parser_t *p, int rc)
{
    if (rc == AT_UPDATED) {
        p->updated++;
    } else if (rc == AT_UNKNOWN) {
        p->unknown++;
    } else if (rc == AT_MALFORMED) {
        p->malformed++;
    } else {
        /* AT_IGNORED: not counted */
    }
}

int at_parse_line(at_parser_t *p, lc_record_t *r, const char *line, size_t len, uint32_t now_s)
{
    size_t n = len;
    size_t st = 0U;
    ctx_t x;
    int rc;

    if ((p == NULL) || (r == NULL) || ((line == NULL) && (len != 0U))) {
        return AT_MALFORMED;
    }
    while ((n > 0U) && is_eol(line[n - 1U])) {
        n--;
    }
    if (n > AT_LINE_MAX) {
        p->lines++;
        p->too_long++;
        return AT_TOO_LONG;
    }
    while ((st < n) && (is_space(line[st]) || is_eol(line[st]))) {
        st++;
    }
    if (st == n) {
        return AT_IGNORED;
    }
    p->lines++;
    x.p = p;
    x.r = r;
    x.now = now_s;
    rc = dispatch(&x, &line[st], n - st);
    count_result(p, rc);
    return rc;
}

/* One byte of the stream: a line ends at CR or LF. */
static void feed_byte(at_parser_t *p, lc_record_t *r, char c, uint32_t now_s)
{
    if (is_eol(c)) {
        if (p->discarding != 0U) {
            p->discarding = 0U;
            p->lines++;
            p->too_long++;
        } else if (p->len != 0U) {
            (void)at_parse_line(p, r, p->buf, p->len, now_s);
        } else {
            /* an empty line */
        }
        p->len = 0U;
    } else if (p->discarding != 0U) {
        /* inside an over-long line: drop */
    } else if (p->len < AT_LINE_MAX) {
        p->buf[p->len] = c;
        p->len++;
    } else {
        p->discarding = 1U;
        p->len = 0U;
    }
}

void at_parse_feed(at_parser_t *p, lc_record_t *r, const char *data, size_t n, uint32_t now_s)
{
    size_t i;

    if ((p == NULL) || (r == NULL) || (data == NULL)) {
        return;
    }
    for (i = 0U; i < n; i++) {
        feed_byte(p, r, data[i], now_s);
    }
}
