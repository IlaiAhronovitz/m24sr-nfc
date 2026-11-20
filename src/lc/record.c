/* record.c - Last Checkpoint record: init, setters, cause tables, text and
 * binary encoders. Freestanding: no libc calls, no heap. */
#include <stdbool.h>
#include "record.h"

/* Unsigned forms of the header limits, for comparisons with sizes. */
#define FW_MAX       ((size_t)LC_FW_MAX)
#define FPLMN_MAX    ((unsigned)LC_FPLMN_MAX)
#define TEXT_LIMIT   ((size_t)LC_TEXT_MAX)

/* ------------------------------------------------------------------------ */
/* Init and setters                                                         */
/* ------------------------------------------------------------------------ */

static void cause_init(lc_cause_t *c)
{
    c->ns = LC_NS_UNKNOWN;
    c->rat = LC_RAT_UNKNOWN;
    c->value = LC_U16_UNKNOWN;
    c->at_s = LC_U32_UNKNOWN;
}

static void plmn_init(lc_plmn_t *p)
{
    p->mcc = LC_U16_UNKNOWN;
    p->mnc = LC_U16_UNKNOWN;
    p->mnc_digits = LC_U8_UNKNOWN;
}

/* Every byte to 0xFF, padding bytes too, so that two equal records compare
 * equal bytewise. */
static void fill_ff(lc_record_t *r)
{
    uint8_t *b = (uint8_t *)r;
    size_t i;

    for (i = 0U; i < sizeof *r; i++) {
        b[i] = 0xFFU;
    }
}

void lc_record_init(lc_record_t *r)
{
    size_t i;

    fill_ff(r);
    cause_init(&r->emm);
    cause_init(&r->esm);
    r->reg_stat = LC_U8_UNKNOWN;
    r->reg_stat_s = LC_U32_UNKNOWN;
    r->attempts = LC_U16_UNKNOWN;
    r->backoff = LC_BO_UNKNOWN;
    r->last_success_s = LC_U32_UNKNOWN;
    r->rat = LC_RAT_UNKNOWN;
    r->band = LC_U8_UNKNOWN;
    plmn_init(&r->plmn);
    r->tac = LC_U32_UNKNOWN;
    r->cell_id = LC_U32_UNKNOWN;
    r->rsrp_dbm = LC_I16_UNKNOWN;
    r->rsrq_ddb = LC_I16_UNKNOWN;
    r->snr_ddb = LC_I16_UNKNOWN;
    r->ce_level = LC_U8_UNKNOWN;
    r->cell_s = LC_U32_UNKNOWN;
    r->fplmn_count = LC_U8_UNKNOWN;
    for (i = 0U; i < FPLMN_MAX; i++) {
        plmn_init(&r->fplmn[i]);
    }
    r->psm_mode_req = LC_U8_UNKNOWN;
    r->t3412_req_s = LC_U32_UNKNOWN;
    r->t3324_req_s = LC_U32_UNKNOWN;
    r->t3412_nw_s = LC_U32_UNKNOWN;
    r->t3324_nw_s = LC_U32_UNKNOWN;
    r->edrx_act = LC_U8_UNKNOWN;
    r->edrx_req = LC_U8_UNKNOWN;
    r->edrx_nw = LC_U8_UNKNOWN;
    r->ptw_nw = LC_U8_UNKNOWN;
    for (i = 0U; i <= FW_MAX; i++) {
        r->fw[i] = '\0';
    }
    r->battery_mv = LC_U16_UNKNOWN;
    r->uptime_s = LC_U32_UNKNOWN;
}

void lc_record_set_fw(lc_record_t *r, const char *fw)
{
    size_t i = 0U;

    if (fw != NULL) {
        while ((i < FW_MAX) && (fw[i] != '\0')) {
            char c = fw[i];
            if ((c < ' ') || (c > '~')) {
                c = '?';                /* printable ASCII only */
            }
            r->fw[i] = c;
            i++;
        }
    }
    while (i <= FW_MAX) {
        r->fw[i] = '\0';
        i++;
    }
}

void lc_record_set_battery(lc_record_t *r, uint16_t mv)
{
    r->battery_mv = mv;
}

void lc_record_set_band(lc_record_t *r, uint8_t band)
{
    r->band = band;
}

void lc_record_set_ce_level(lc_record_t *r, uint8_t ce)
{
    r->ce_level = (ce <= 3U) ? ce : LC_U8_UNKNOWN;
}

uint8_t lc_backoff_for_emm(uint16_t cause)
{
    uint8_t bo;

    switch (cause) {
    case 3U:
    case 6U:
    case 7U:
    case 8U:
        bo = LC_BO_SIM_INVALID;
        break;
    case 11U:
    case 14U:
        bo = LC_BO_PLMN_FORBIDDEN;
        break;
    case 12U:
    case 13U:
    case 15U:
        bo = LC_BO_TA_FORBIDDEN;
        break;
    case 22U:
        bo = LC_BO_CONGESTION;
        break;
    default:
        bo = LC_BO_RETRY;
        break;
    }
    return bo;
}

void lc_record_set_emm(lc_record_t *r, uint8_t ns, uint16_t cause, uint32_t now_s)
{
    r->emm.ns = ns;
    r->emm.value = cause;
    r->emm.rat = r->rat;
    r->emm.at_s = now_s;
    if (ns == LC_NS_EMM) {
        r->backoff = lc_backoff_for_emm(cause);
    } else {
        r->backoff = LC_BO_RETRY;
    }
    r->uptime_s = now_s;
}

void lc_record_set_esm(lc_record_t *r, uint16_t cause, uint32_t now_s)
{
    r->esm.ns = LC_NS_ESM;
    r->esm.value = cause;
    r->esm.rat = r->rat;
    r->esm.at_s = now_s;
    r->uptime_s = now_s;
}

/* ------------------------------------------------------------------------ */
/* Cause tables                                                             */
/* ------------------------------------------------------------------------ */

typedef struct {
    uint8_t code;
    const char *text;
} cause_text_t;

static const char *lookup(const cause_text_t *t, size_t n, uint16_t code)
{
    size_t i;

    for (i = 0U; i < n; i++) {
        if (t[i].code == code) {
            return t[i].text;
        }
    }
    return NULL;
}

const char *lc_emm_cause_text(uint16_t cause)
{
    /* EMM cause values: 3GPP TS 24.301 V19.8.0 (ETSI TS 124 301 V19.8.0,
     * 2026-09), Table 9.9.3.9.1; the values are defined in Annex A. Every
     * value of that table is listed. The words are the spec's; only the
     * capitals are changed to sentence case. Checked 2026-11-20. Any other
     * value received is to be treated as #111 (the summary still prints the
     * number). */
    static const cause_text_t k_emm[] = {
        {   2, "IMSI unknown in HSS" },
        {   3, "Illegal UE" },
        {   5, "IMEI not accepted" },
        {   6, "Illegal ME" },
        {   7, "EPS services not allowed" },
        {   8, "EPS services and non-EPS services not allowed" },
        {   9, "UE identity cannot be derived by the network" },
        {  10, "Implicitly detached" },
        {  11, "PLMN not allowed" },
        {  12, "Tracking area not allowed" },
        {  13, "Roaming not allowed in this tracking area" },
        {  14, "EPS services not allowed in this PLMN" },
        {  15, "No suitable cells in tracking area" },
        {  16, "MSC temporarily not reachable" },
        {  17, "Network failure" },
        {  18, "CS domain not available" },
        {  19, "ESM failure" },
        {  20, "MAC failure" },
        {  21, "Synch failure" },
        {  22, "Congestion" },
        {  23, "UE security capabilities mismatch" },
        {  24, "Security mode rejected, unspecified" },
        {  25, "Not authorized for this CSG" },
        {  26, "Non-EPS authentication unacceptable" },
        {  31, "Redirection to 5GCN required" },
        {  35, "Requested service option not authorized in this PLMN" },
        {  36, "IAB-node operation not authorized" },
        {  39, "CS service temporarily not available" },
        {  40, "No EPS bearer context activated" },
        {  42, "Severe network failure" },
        {  78, "PLMN not allowed to operate at the present UE location" },
        {  80, "Disaster roaming for the determined PLMN with disaster condition not allowed" },
        {  83, "Procedure cannot be completed due to unavailable feeder link while MME is operating in S&F mode" },
        {  95, "Semantically incorrect message" },
        {  96, "Invalid mandatory information" },
        {  97, "Message type non-existent or not implemented" },
        {  98, "Message type not compatible with the protocol state" },
        {  99, "Information element non-existent or not implemented" },
        { 100, "Conditional IE error" },
        { 101, "Message not compatible with the protocol state" },
        { 111, "Protocol error, unspecified" },
    };

    return lookup(k_emm, (sizeof k_emm) / (sizeof k_emm[0]), cause);
}

const char *lc_esm_cause_text(uint16_t cause)
{
    /* ESM cause values: 3GPP TS 24.301 V19.8.0, Table 9.9.4.4.1; the values
     * are defined in Annex B. Every value of that table is listed except #46,
     * which the table marks "Unused". Words as in the spec, sentence case. */
    static const cause_text_t k_esm[] = {
        {   8, "Operator determined barring" },
        {  26, "Insufficient resources" },
        {  27, "Missing or unknown APN" },
        {  28, "Unknown PDN type" },
        {  29, "User authentication or authorization failed" },
        {  30, "Request rejected by Serving GW or PDN GW" },
        {  31, "Request rejected, unspecified" },
        {  32, "Service option not supported" },
        {  33, "Requested service option not subscribed" },
        {  34, "Service option temporarily out of order" },
        {  35, "PTI already in use" },
        {  36, "Regular deactivation" },
        {  37, "EPS QoS not accepted" },
        {  38, "Network failure" },
        {  39, "Reactivation requested" },
        {  41, "Semantic error in the TFT operation" },
        {  42, "Syntactical error in the TFT operation" },
        {  43, "Invalid EPS bearer identity" },
        {  44, "Semantic errors in packet filter(s)" },
        {  45, "Syntactical errors in packet filter(s)" },
        {  47, "PTI mismatch" },
        {  49, "Last PDN disconnection not allowed" },
        {  50, "PDN type IPv4 only allowed" },
        {  51, "PDN type IPv6 only allowed" },
        {  52, "Single address bearers only allowed" },
        {  53, "ESM information not received" },
        {  54, "PDN connection does not exist" },
        {  55, "Multiple PDN connections for a given APN not allowed" },
        {  56, "Collision with network initiated request" },
        {  57, "PDN type IPv4v6 only allowed" },
        {  58, "PDN type non IP only allowed" },
        {  59, "Unsupported QCI value" },
        {  60, "Bearer handling not supported" },
        {  61, "PDN type Ethernet only allowed" },
        {  65, "Maximum number of EPS bearers reached" },
        {  66, "Requested APN not supported in current RAT and PLMN combination" },
        {  81, "Invalid PTI value" },
        {  95, "Semantically incorrect message" },
        {  96, "Invalid mandatory information" },
        {  97, "Message type non-existent or not implemented" },
        {  98, "Message type not compatible with the protocol state" },
        {  99, "Information element non-existent or not implemented" },
        { 100, "Conditional IE error" },
        { 101, "Message not compatible with the protocol state" },
        { 111, "Protocol error, unspecified" },
        { 112, "APN restriction value incompatible with active EPS bearer context" },
        { 113, "Multiple accesses to a PDN connection not allowed" },
    };

    return lookup(k_esm, (sizeof k_esm) / (sizeof k_esm[0]), cause);
}

/* ------------------------------------------------------------------------ */
/* Text summary                                                             */
/* ------------------------------------------------------------------------ */

#define SEG_MAX 128U

typedef struct {
    char buf[SEG_MAX];
    size_t len;
    bool overflow;
} seg_t;

static void s_ch(seg_t *s, char c)
{
    if ((s->len + 1U) < SEG_MAX) {
        s->buf[s->len] = c;
        s->len++;
    } else {
        s->overflow = true;
    }
}

static void s_str(seg_t *s, const char *p)
{
    size_t i;

    for (i = 0U; p[i] != '\0'; i++) {
        s_ch(s, p[i]);
    }
}

/* Decimal, at least min_digits digits (zero-padded, at most 10). */
static void s_uint(seg_t *s, uint32_t v, size_t min_digits)
{
    char tmp[10];
    size_t n = 0U;
    uint32_t rest = v;

    do {
        tmp[n] = (char)('0' + (rest % 10U));
        n++;
        rest /= 10U;
    } while ((rest != 0U) && (n < sizeof tmp));
    while ((n < min_digits) && (n < sizeof tmp)) {
        tmp[n] = '0';
        n++;
    }
    while (n > 0U) {
        n--;
        s_ch(s, tmp[n]);
    }
}

/* |v| as uint32_t, valid for INT32_MIN too. */
static uint32_t magnitude(int32_t v)
{
    return (v < 0) ? ((uint32_t)(-(v + 1)) + 1U) : (uint32_t)v;
}

static void s_int(seg_t *s, int32_t v)
{
    if (v < 0) {
        s_ch(s, '-');
    }
    s_uint(s, magnitude(v), 1U);
}

static void s_hex(seg_t *s, uint32_t v, unsigned digits)
{
    static const char hx[] = "0123456789ABCDEF";
    unsigned d = digits;

    while (d > 0U) {
        d--;
        s_ch(s, hx[(v >> (4U * d)) & 0xFU]);
    }
}

/* Tenths: -145 -> "-14.5", 30 -> "3". */
static void s_tenths(seg_t *s, int32_t v)
{
    uint32_t a = magnitude(v);

    if (v < 0) {
        s_ch(s, '-');
    }
    s_uint(s, a / 10U, 1U);
    if ((a % 10U) != 0U) {
        s_ch(s, '.');
        s_ch(s, (char)('0' + (a % 10U)));
    }
}

/* Hundredths: 8192 -> "81.92", 512 -> "5.12". */
static void s_hundredths(seg_t *s, uint32_t v)
{
    s_uint(s, v / 100U, 1U);
    if ((v % 100U) != 0U) {
        s_ch(s, '.');
        s_uint(s, v % 100U, 2U);
    }
}

/* "MCC-MNC". With the MNC digit count unknown, an MNC below 100 could be
 * 2 or 3 digits ("01" or "001"): it is printed with 2 digits and a '?'. */
static void s_plmn(seg_t *s, const lc_plmn_t *p)
{
    if (p->mcc == LC_U16_UNKNOWN) {
        s_ch(s, '-');
    } else {
        s_uint(s, p->mcc, 3U);
    }
    s_ch(s, '-');
    if (p->mnc == LC_U16_UNKNOWN) {
        s_ch(s, '-');
    } else {
        s_uint(s, p->mnc, (p->mnc_digits == 3U) ? 3U : 2U);
        if ((p->mnc_digits != 2U) && (p->mnc_digits != 3U) && (p->mnc < 100U)) {
            s_ch(s, '?');
        }
    }
}

static void s_timer(seg_t *s, uint32_t v)
{
    if (v == LC_U32_UNKNOWN) {
        s_ch(s, '-');
    } else if (v == LC_TIMER_DEACTIVATED) {
        s_str(s, "off");
    } else {
        s_uint(s, v, 1U);
    }
}

typedef struct {
    char *out;
    size_t limit;   /* characters allowed */
    size_t len;
} text_t;

/* Appends " | " + segment if the whole of it fits. */
static void commit(text_t *t, const seg_t *s)
{
    size_t need;
    size_t i;

    if ((s->len == 0U) || s->overflow) {
        return;
    }
    need = s->len + ((t->len != 0U) ? 3U : 0U);
    if ((t->len + need) > t->limit) {
        return;
    }
    if (t->len != 0U) {
        t->out[t->len] = ' ';
        t->out[t->len + 1U] = '|';
        t->out[t->len + 2U] = ' ';
        t->len += 3U;
    }
    for (i = 0U; i < s->len; i++) {
        t->out[t->len] = s->buf[i];
        t->len++;
    }
    t->out[t->len] = '\0';
}

static const char *rat_name(uint8_t rat)
{
    const char *name;

    switch (rat) {
    case LC_RAT_GSM:    name = "GSM"; break;
    case LC_RAT_LTE:    name = "LTE"; break;
    case LC_RAT_LTE_M:  name = "LTE-M"; break;
    case LC_RAT_NB_IOT: name = "NB-IoT"; break;
    case LC_RAT_NR:     name = "NR"; break;
    case LC_RAT_OTHER:  name = "other RAT"; break;
    default:            name = NULL; break;
    }
    return name;
}

/* Headline text for a +CEREG <stat> (TS 27.007 V19.6.0 clause 10.1.22),
 * and whether the attempt counter belongs after it. */
static const char *stat_name(uint8_t stat, bool *show_attempts)
{
    const char *name;

    *show_attempts = true;
    switch (stat) {
    case 1U:  name = "REGISTERED home"; *show_attempts = false; break;
    case 5U:  name = "REGISTERED roaming"; *show_attempts = false; break;
    case 3U:  name = "REGISTRATION DENIED"; break;
    case 2U:  name = "SEARCHING"; break;
    case 0U:  name = "NOT REGISTERED"; break;
    case 4U:  name = "REG STATUS UNKNOWN"; break;
    case 8U:  name = "EMERGENCY ONLY"; *show_attempts = false; break;
    case 11U: name = "ATTACHED FOR RLOS"; *show_attempts = false; break;
    case 12U: name = "REGISTERED disaster roaming"; *show_attempts = false; break;
    case 13U: name = "DISASTER CONDITION"; break;
    case LC_U8_UNKNOWN: name = "NO REGISTRATION STATUS"; break;
    default:  name = NULL; *show_attempts = false; break;
    }
    return name;
}

static void seg_headline(seg_t *s, const lc_record_t *r)
{
    bool show_attempts;
    const char *name = stat_name(r->reg_stat, &show_attempts);

    if (name != NULL) {
        s_str(s, name);
    } else {
        s_str(s, "REG STAT ");
        s_uint(s, r->reg_stat, 1U);
    }
    if (show_attempts && (r->attempts != LC_U16_UNKNOWN) && (r->attempts > 0U)) {
        s_str(s, " x");
        s_uint(s, r->attempts, 1U);
    }
}

static void seg_cause(seg_t *s, const lc_cause_t *c, bool esm_slot)
{
    const char *txt = NULL;

    if (c->value == LC_U16_UNKNOWN) {
        return;
    }
    switch (c->ns) {
    case LC_NS_EMM:
        s_str(s, "EMM #");
        txt = lc_emm_cause_text(c->value);
        break;
    case LC_NS_ESM:
        s_str(s, "ESM #");
        txt = lc_esm_cause_text(c->value);
        break;
    case LC_NS_VENDOR:
        s_str(s, "vendor cause #");
        break;
    default:
        s_str(s, esm_slot ? "ESM? #" : "cause #");
        break;
    }
    s_uint(s, c->value, 1U);
    if (txt != NULL) {
        s_ch(s, ' ');
        s_str(s, txt);
    }
}

static void s_sep(seg_t *s)
{
    if (s->len != 0U) {
        s_ch(s, ' ');
    }
}

/* " <label><tenths> dB" for a known value. */
static void s_db(seg_t *s, const char *label, int16_t v)
{
    if (v != LC_I16_UNKNOWN) {
        s_sep(s);
        s_str(s, label);
        s_tenths(s, v);
        s_str(s, " dB");
    }
}

static void seg_radio(seg_t *s, const lc_record_t *r)
{
    const char *rn = rat_name(r->rat);

    if (rn != NULL) {
        s_str(s, rn);
    }
    if (r->band != LC_U8_UNKNOWN) {
        s_sep(s);
        s_ch(s, 'B');
        s_uint(s, r->band, 1U);
    }
    if (r->rsrp_dbm != LC_I16_UNKNOWN) {
        s_sep(s);
        s_str(s, "RSRP ");
        s_int(s, r->rsrp_dbm);
        s_str(s, " dBm");
    }
    s_db(s, "RSRQ ", r->rsrq_ddb);
    s_db(s, "SNR ", r->snr_ddb);
    if (r->ce_level != LC_U8_UNKNOWN) {
        s_sep(s);
        s_str(s, "CE");
        s_uint(s, r->ce_level, 1U);
    }
}

static void seg_cell(seg_t *s, const lc_record_t *r)
{
    if ((r->plmn.mcc != LC_U16_UNKNOWN) || (r->plmn.mnc != LC_U16_UNKNOWN)) {
        s_str(s, "PLMN ");
        s_plmn(s, &r->plmn);
    }
    if (r->tac != LC_U32_UNKNOWN) {
        s_sep(s);
        s_str(s, "TAC ");
        s_hex(s, r->tac, 4U);
    }
    if (r->cell_id != LC_U32_UNKNOWN) {
        s_sep(s);
        s_str(s, "CI ");
        s_hex(s, r->cell_id, 7U);
    }
}

/* "<t3412>/<t3324> s" */
static void s_timer_pair(seg_t *s, uint32_t t3412, uint32_t t3324)
{
    s_timer(s, t3412);
    s_ch(s, '/');
    s_timer(s, t3324);
    s_str(s, " s");
}

static void seg_psm(seg_t *s, const lc_record_t *r)
{
    bool req = (r->t3412_req_s != LC_U32_UNKNOWN) || (r->t3324_req_s != LC_U32_UNKNOWN);
    bool nw = (r->t3412_nw_s != LC_U32_UNKNOWN) || (r->t3324_nw_s != LC_U32_UNKNOWN);

    if (!req && !nw && (r->psm_mode_req == LC_U8_UNKNOWN)) {
        return;
    }
    s_str(s, "PSM req ");
    if (r->psm_mode_req == 0U) {
        s_str(s, "off");
    } else if (req) {
        s_timer_pair(s, r->t3412_req_s, r->t3324_req_s);
    } else {
        s_ch(s, '-');
    }
    s_str(s, ", granted ");
    if (nw) {
        s_timer_pair(s, r->t3412_nw_s, r->t3324_nw_s);
    } else {
        s_ch(s, '-');
    }
}

/* +CEDRXRDP <AcT-type> values (TS 27.007 V19.6.0 clause 7.41) that use the
 * S1-mode eDRX table: 4/7 = (satellite) WB-S1, 5/6 = (satellite) NB-S1. */
static bool edrx_wb(uint8_t act) { return (act == 4U) || (act == 7U); }
static bool edrx_nb(uint8_t act) { return (act == 5U) || (act == 6U); }

/* S1-mode eDRX cycle in hundredths of a second: 3GPP TS 24.008 V19.5.0
 * (ETSI TS 124 008 V19.5.0, 2026-02), Table 10.5.5.32, "S1 mode".
 *   WB-S1: 1110 and 1111 are NB-S1 only and are read as 1101 (NOTE 6).
 *   NB-S1: 0000 and 0001 mean "IE not included", i.e. no eDRX (NOTE 4,
 *          returns 0); 0100, 0110, 0111 and 1000 are read as 0010 (NOTE 5).
 * Returns 0 for no eDRX or an AcT-type that uses another table. */
static uint32_t edrx_cs(uint8_t act, uint8_t code)
{
    static const uint32_t s1[16] = {
        512U, 1024U, 2048U, 4096U, 6144U, 8192U, 10240U, 12288U,
        14336U, 16384U, 32768U, 65536U, 131072U, 262144U, 524288U, 1048576U
    };
    uint8_t c = code;

    if (c > 15U) {
        return 0U;
    }
    if (edrx_wb(act)) {
        return s1[(c >= 14U) ? 13U : c];
    }
    if (!edrx_nb(act) || (c < 2U)) {
        return 0U;
    }
    if ((c == 4U) || ((c >= 6U) && (c <= 8U))) {
        c = 2U;
    }
    return s1[c];
}

/* A duration for the S1-mode AcT-types, "n/a" where the code means no eDRX,
 * and the raw 4-bit code for GERAN/UTRAN/NR, whose tables are not decoded. */
static void s_edrx(seg_t *s, uint8_t act, uint8_t code)
{
    unsigned bit;

    if (code > 15U) {
        s_ch(s, '-');
        return;
    }
    if (edrx_wb(act) || edrx_nb(act)) {
        uint32_t cs = edrx_cs(act, code);
        if (cs == 0U) {
            s_str(s, "n/a");
        } else {
            s_hundredths(s, cs);
            s_str(s, " s");
        }
        return;
    }
    s_str(s, "code ");
    for (bit = 4U; bit > 0U; bit--) {
        s_ch(s, ((((uint32_t)code >> (bit - 1U)) & 1U) != 0U) ? '1' : '0');
    }
}

static void seg_edrx(seg_t *s, const lc_record_t *r)
{
    if (r->edrx_act == LC_U8_UNKNOWN) {
        return;
    }
    if (r->edrx_act == 0U) {
        s_str(s, "eDRX off");
        return;
    }
    s_str(s, "eDRX req ");
    s_edrx(s, r->edrx_act, r->edrx_req);
    s_str(s, ", granted ");
    s_edrx(s, r->edrx_act, r->edrx_nw);
}

static void seg_fplmn(seg_t *s, const lc_record_t *r)
{
    unsigned i;

    if (r->fplmn_count == LC_U8_UNKNOWN) {
        return;
    }
    s_str(s, "FPLMN");
    if (r->fplmn_count == 0U) {
        s_str(s, " none");
    }
    for (i = 0U; (i < r->fplmn_count) && (i < FPLMN_MAX); i++) {
        s_ch(s, ' ');
        s_plmn(s, &r->fplmn[i]);
    }
}

static void seg_fw(seg_t *s, const lc_record_t *r)
{
    if (r->fw[0] != '\0') {
        s_str(s, "fw ");
        s_str(s, r->fw);
    }
}

static void seg_battery(seg_t *s, const lc_record_t *r)
{
    uint32_t cv;

    if (r->battery_mv == LC_U16_UNKNOWN) {
        return;
    }
    cv = ((uint32_t)r->battery_mv + 5U) / 10U;   /* centivolts, rounded */
    s_uint(s, cv / 100U, 1U);
    s_ch(s, '.');
    s_uint(s, cv % 100U, 2U);
    s_str(s, " V");
}

static void seg_uptime(seg_t *s, const lc_record_t *r)
{
    if (r->uptime_s == LC_U32_UNKNOWN) {
        return;
    }
    s_str(s, "uptime ");
    s_uint(s, r->uptime_s / 3600U, 1U);
    s_ch(s, 'h');
    s_uint(s, (r->uptime_s / 60U) % 60U, 2U);
    s_ch(s, 'm');
}

typedef void (*seg_fn)(seg_t *, const lc_record_t *);

static void seg_emm(seg_t *s, const lc_record_t *r) { seg_cause(s, &r->emm, false); }
static void seg_esm(seg_t *s, const lc_record_t *r) { seg_cause(s, &r->esm, true); }

int record_encode_text(const lc_record_t *r, char *out, size_t cap)
{
    /* Output order. The headline and the causes come first, so they are the
     * last to be dropped when the line is full. */
    static const seg_fn order[] = {
        seg_headline, seg_emm, seg_esm, seg_radio, seg_cell, seg_psm,
        seg_edrx, seg_fplmn, seg_fw, seg_battery, seg_uptime
    };
    text_t t;
    size_t i;

    if ((out == NULL) || (cap == 0U)) {
        return -1;
    }
    t.out = out;
    t.len = 0U;
    t.limit = ((cap - 1U) < TEXT_LIMIT) ? (cap - 1U) : TEXT_LIMIT;
    out[0] = '\0';
    if (r == NULL) {
        return 0;
    }
    for (i = 0U; i < ((sizeof order) / (sizeof order[0])); i++) {
        seg_t s;
        s.len = 0U;
        s.overflow = false;
        order[i](&s, r);
        commit(&t, &s);
    }
    return (int)t.len;
}

/* ------------------------------------------------------------------------ */
/* Binary TLV                                                               */
/* ------------------------------------------------------------------------ */
/*
 * Layout (all integers big-endian):
 *   'L' 'C' | version (1) | TLV area length (2) | TLVs ... | CRC16 (2)
 * CRC-16/CCITT-FALSE over every byte before it. A TLV is
 *   object (1) | resource (1) | length (1) | value
 * Fields that are unknown are left out. Readers skip tags they do not know.
 *
 * Where a field overlaps an OMA LwM2M object (OMA LwM2M registry, prod branch),
 * object/resource are that object's IDs:
 *   3/3  Firmware Version (string)       3/7  Power Source Voltage (mV, u16)
 *   4/2  Radio Signal Strength = RSRP (dBm, s16)
 *   4/3  Link Quality = RSRQ (UNIT DIFFERS: 0.1 dB, s16)
 *   4/8  Cell ID (u32)    4/9 SMNC (u16)    4/10 SMCC (u16)
 *   4/11 SignalSNR (UNIT DIFFERS: 0.1 dB, s16)
 *   4/12 LAC, used for the E-UTRAN TAC (u16)
 *   4/13 Coverage Enhancement Level (u8)
 *   10/4 PSM Timer = requested T3412 (s, u32)
 *   10/5 Active Timer = requested T3324 (s, u32)
 * LwM2M 10/8 and 10/9 (eDRX octet = PTW | eDRX) are not used: +CEDRXRDP
 * gives no requested PTW, so eDRX lives in the extension object below.
 * Extension object 0xF0, fields no LwM2M object carries:
 *   F0/01 EMM cause: ns u8, rat u8, value u16, at_s u32
 *   F0/02 ESM cause: same layout
 *   F0/03 registration: stat u8, at_s u32
 *   F0/04 attempts u16       F0/05 back-off u8     F0/06 last success s u32
 *   F0/07 serving RAT u8     F0/08 band u8         F0/09 MNC digits u8
 *   F0/0A cell update s u32  F0/0B FPLMN: count u8, then count x (mcc u16, mnc u16, digits u8)
 *   F0/0C PSM mode req u8    F0/0D T3412 granted u32   F0/0E T3324 granted u32
 *   F0/0F eDRX: act u8, req u8, nw u8, ptw u8 (0xFF inside = unknown)
 *   F0/10 record uptime u32
 *
 * A tag is (object << 8) | resource.
 */
#define TAG_FW          0x0303U   /* 3/3 */
#define TAG_BATT        0x0307U   /* 3/7 */
#define TAG_RSRP        0x0402U   /* 4/2 */
#define TAG_RSRQ        0x0403U   /* 4/3 */
#define TAG_CI          0x0408U   /* 4/8 */
#define TAG_MNC         0x0409U   /* 4/9 */
#define TAG_MCC         0x040AU   /* 4/10 */
#define TAG_SNR         0x040BU   /* 4/11 */
#define TAG_TAC         0x040CU   /* 4/12 */
#define TAG_CE          0x040DU   /* 4/13 */
#define TAG_T3412_REQ   0x0A04U   /* 10/4 */
#define TAG_T3324_REQ   0x0A05U   /* 10/5 */
#define TAG_EMM         0xF001U
#define TAG_ESM         0xF002U
#define TAG_REG         0xF003U
#define TAG_ATTEMPTS    0xF004U
#define TAG_BACKOFF     0xF005U
#define TAG_LAST_OK     0xF006U
#define TAG_RAT         0xF007U
#define TAG_BAND        0xF008U
#define TAG_MNC_DIGITS  0xF009U
#define TAG_CELL_S      0xF00AU
#define TAG_FPLMN       0xF00BU
#define TAG_PSM_MODE    0xF00CU
#define TAG_T3412_NW    0xF00DU
#define TAG_T3324_NW    0xF00EU
#define TAG_EDRX        0xF00FU
#define TAG_UPTIME      0xF010U

#define HDR_LEN    5U
#define CRC_LEN    2U
#define TLV_HDR    3U   /* object, resource, length */
#define CAUSE_LEN  8U
#define REG_LEN    5U
#define EDRX_LEN   4U
#define PLMN_LEN   5U   /* one FPLMN entry: mcc u16, mnc u16, digits u8 */

uint16_t lc_crc16(const uint8_t *p, size_t n)
{
    uint16_t crc = 0xFFFFU;
    size_t i;
    unsigned b;

    for (i = 0U; i < n; i++) {
        crc ^= (uint16_t)((uint16_t)p[i] << 8U);
        for (b = 0U; b < 8U; b++) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)(((uint32_t)crc << 1U) ^ 0x1021U);
            } else {
                crc = (uint16_t)((uint32_t)crc << 1U);
            }
        }
    }
    return crc;
}

typedef struct {
    uint8_t *p;
    size_t cap;
    size_t len;
    bool err;
} wbuf_t;

static void w_u8(wbuf_t *w, uint8_t v)
{
    if (w->len < w->cap) {
        w->p[w->len] = v;
        w->len++;
    } else {
        w->err = true;
    }
}

static void w_u16(wbuf_t *w, uint16_t v)
{
    w_u8(w, (uint8_t)(v >> 8U));
    w_u8(w, (uint8_t)v);
}

static void w_u32(wbuf_t *w, uint32_t v)
{
    w_u16(w, (uint16_t)(v >> 16U));
    w_u16(w, (uint16_t)v);
}

static void w_tag(wbuf_t *w, uint16_t tag, uint8_t len)
{
    w_u16(w, tag);
    w_u8(w, len);
}

static void w_tlv_u8(wbuf_t *w, uint16_t tag, uint8_t v, bool known)
{
    if (known) {
        w_tag(w, tag, 1U);
        w_u8(w, v);
    }
}

static void w_tlv_u16(wbuf_t *w, uint16_t tag, uint16_t v, bool known)
{
    if (known) {
        w_tag(w, tag, 2U);
        w_u16(w, v);
    }
}

static void w_tlv_u32(wbuf_t *w, uint16_t tag, uint32_t v, bool known)
{
    if (known) {
        w_tag(w, tag, 4U);
        w_u32(w, v);
    }
}

static void w_cause(wbuf_t *w, uint16_t tag, const lc_cause_t *c)
{
    if ((c->ns == LC_NS_UNKNOWN) && (c->value == LC_U16_UNKNOWN) &&
        (c->rat == LC_RAT_UNKNOWN) && (c->at_s == LC_U32_UNKNOWN)) {
        return;
    }
    w_tag(w, tag, (uint8_t)CAUSE_LEN);
    w_u8(w, c->ns);
    w_u8(w, c->rat);
    w_u16(w, c->value);
    w_u32(w, c->at_s);
}

static void w_plmn(wbuf_t *w, const lc_plmn_t *p)
{
    w_u16(w, p->mcc);
    w_u16(w, p->mnc);
    w_u8(w, p->mnc_digits);
}

static void w_fw(wbuf_t *w, const lc_record_t *r)
{
    size_t fwlen = 0U;

    while ((fwlen < FW_MAX) && (r->fw[fwlen] != '\0')) {
        fwlen++;
    }
    if (fwlen != 0U) {
        size_t i;
        w_tag(w, TAG_FW, (uint8_t)fwlen);
        for (i = 0U; i < fwlen; i++) {
            w_u8(w, (uint8_t)r->fw[i]);
        }
    }
}

/* The fields that reuse LwM2M object/resource IDs. */
static void w_lwm2m(wbuf_t *w, const lc_record_t *r)
{
    w_fw(w, r);
    w_tlv_u16(w, TAG_BATT, r->battery_mv, r->battery_mv != LC_U16_UNKNOWN);
    w_tlv_u16(w, TAG_RSRP, (uint16_t)r->rsrp_dbm, r->rsrp_dbm != LC_I16_UNKNOWN);
    w_tlv_u16(w, TAG_RSRQ, (uint16_t)r->rsrq_ddb, r->rsrq_ddb != LC_I16_UNKNOWN);
    w_tlv_u32(w, TAG_CI, r->cell_id, r->cell_id != LC_U32_UNKNOWN);
    w_tlv_u16(w, TAG_MNC, r->plmn.mnc, r->plmn.mnc != LC_U16_UNKNOWN);
    w_tlv_u16(w, TAG_MCC, r->plmn.mcc, r->plmn.mcc != LC_U16_UNKNOWN);
    w_tlv_u16(w, TAG_SNR, (uint16_t)r->snr_ddb, r->snr_ddb != LC_I16_UNKNOWN);
    w_tlv_u16(w, TAG_TAC, (uint16_t)r->tac, r->tac <= 0xFFFFU);
    w_tlv_u8(w, TAG_CE, r->ce_level, r->ce_level != LC_U8_UNKNOWN);
    w_tlv_u32(w, TAG_T3412_REQ, r->t3412_req_s, r->t3412_req_s != LC_U32_UNKNOWN);
    w_tlv_u32(w, TAG_T3324_REQ, r->t3324_req_s, r->t3324_req_s != LC_U32_UNKNOWN);
}

static void w_fplmn(wbuf_t *w, const lc_record_t *r)
{
    unsigned nf;
    unsigned i;

    if (r->fplmn_count == LC_U8_UNKNOWN) {
        return;
    }
    nf = (r->fplmn_count <= FPLMN_MAX) ? r->fplmn_count : FPLMN_MAX;
    w_tag(w, TAG_FPLMN, (uint8_t)(1U + (PLMN_LEN * nf)));
    w_u8(w, (uint8_t)nf);
    for (i = 0U; i < nf; i++) {
        w_plmn(w, &r->fplmn[i]);
    }
}

static void w_edrx(wbuf_t *w, const lc_record_t *r)
{
    if ((r->edrx_act != LC_U8_UNKNOWN) || (r->edrx_req != LC_U8_UNKNOWN) ||
        (r->edrx_nw != LC_U8_UNKNOWN) || (r->ptw_nw != LC_U8_UNKNOWN)) {
        w_tag(w, TAG_EDRX, (uint8_t)EDRX_LEN);
        w_u8(w, r->edrx_act);
        w_u8(w, r->edrx_req);
        w_u8(w, r->edrx_nw);
        w_u8(w, r->ptw_nw);
    }
}

/* The fields of extension object 0xF0. */
static void w_extension(wbuf_t *w, const lc_record_t *r)
{
    w_cause(w, TAG_EMM, &r->emm);
    w_cause(w, TAG_ESM, &r->esm);
    if ((r->reg_stat != LC_U8_UNKNOWN) || (r->reg_stat_s != LC_U32_UNKNOWN)) {
        w_tag(w, TAG_REG, (uint8_t)REG_LEN);
        w_u8(w, r->reg_stat);
        w_u32(w, r->reg_stat_s);
    }
    w_tlv_u16(w, TAG_ATTEMPTS, r->attempts, r->attempts != LC_U16_UNKNOWN);
    w_tlv_u8(w, TAG_BACKOFF, r->backoff, r->backoff != LC_BO_UNKNOWN);
    w_tlv_u32(w, TAG_LAST_OK, r->last_success_s, r->last_success_s != LC_U32_UNKNOWN);
    w_tlv_u8(w, TAG_RAT, r->rat, r->rat != LC_RAT_UNKNOWN);
    w_tlv_u8(w, TAG_BAND, r->band, r->band != LC_U8_UNKNOWN);
    w_tlv_u8(w, TAG_MNC_DIGITS, r->plmn.mnc_digits, r->plmn.mnc_digits != LC_U8_UNKNOWN);
    w_tlv_u32(w, TAG_CELL_S, r->cell_s, r->cell_s != LC_U32_UNKNOWN);
    w_fplmn(w, r);
    w_tlv_u8(w, TAG_PSM_MODE, r->psm_mode_req, r->psm_mode_req != LC_U8_UNKNOWN);
    w_tlv_u32(w, TAG_T3412_NW, r->t3412_nw_s, r->t3412_nw_s != LC_U32_UNKNOWN);
    w_tlv_u32(w, TAG_T3324_NW, r->t3324_nw_s, r->t3324_nw_s != LC_U32_UNKNOWN);
    w_edrx(w, r);
    w_tlv_u32(w, TAG_UPTIME, r->uptime_s, r->uptime_s != LC_U32_UNKNOWN);
}

int record_encode_bin(const lc_record_t *r, uint8_t *out, size_t cap)
{
    wbuf_t w;
    size_t body;

    if ((r == NULL) || (out == NULL)) {
        return -1;
    }
    w.p = out;
    w.cap = cap;
    w.len = 0U;
    w.err = false;
    w_u8(&w, (uint8_t)'L');
    w_u8(&w, (uint8_t)'C');
    w_u8(&w, LC_BIN_VERSION);
    w_u16(&w, 0U);                      /* length, patched below */
    w_lwm2m(&w, r);
    w_extension(&w, r);

    if (w.err || ((w.len + CRC_LEN) > cap)) {
        return -1;
    }
    body = w.len - HDR_LEN;
    out[3] = (uint8_t)(body >> 8U);
    out[4] = (uint8_t)body;
    w_u16(&w, lc_crc16(out, w.len));
    return w.err ? -1 : (int)w.len;
}

static uint16_t r_u16(const uint8_t *p)
{
    return (uint16_t)(((uint32_t)p[0] << 8U) | (uint32_t)p[1]);
}

static uint32_t r_u32(const uint8_t *p)
{
    return ((uint32_t)r_u16(p) << 16U) | (uint32_t)r_u16(&p[2]);
}

static bool ok_ns(uint8_t v)
{
    return ((v >= LC_NS_EMM) && (v <= LC_NS_VENDOR)) || (v == LC_NS_UNKNOWN);
}

static bool ok_rat(uint8_t v)
{
    return ((v >= LC_RAT_GSM) && (v <= LC_RAT_OTHER)) || (v == LC_RAT_UNKNOWN);
}

static bool ok_nib(uint8_t v)
{
    return (v <= 15U) || (v == LC_U8_UNKNOWN);
}

static bool ok_plmn(const lc_plmn_t *p)
{
    return ((p->mcc <= 999U) || (p->mcc == LC_U16_UNKNOWN)) &&
           ((p->mnc <= 999U) || (p->mnc == LC_U16_UNKNOWN)) &&
           ((p->mnc_digits == 2U) || (p->mnc_digits == 3U) || (p->mnc_digits == LC_U8_UNKNOWN)) &&
           !((p->mnc_digits == 2U) && (p->mnc != LC_U16_UNKNOWN) && (p->mnc > 99U));
}

/* TLV handlers: each returns 0, LC_BIN_ETLV (length) or LC_BIN_EVALUE. A
 * length error is reported before a value error. */

static int tlv_fw(lc_record_t *r, const uint8_t *v, size_t len)
{
    size_t i;

    if ((len < 1U) || (len > FW_MAX)) {
        return LC_BIN_ETLV;
    }
    for (i = 0U; i < len; i++) {
        if ((v[i] < 0x20U) || (v[i] > 0x7EU)) {
            return LC_BIN_EVALUE;
        }
        r->fw[i] = (char)v[i];
    }
    for (; i <= FW_MAX; i++) {
        r->fw[i] = '\0';
    }
    return 0;
}

static int tlv_cause(lc_cause_t *c, const uint8_t *v, size_t len)
{
    if (len != CAUSE_LEN) {
        return LC_BIN_ETLV;
    }
    c->ns = v[0];
    c->rat = v[1];
    c->value = r_u16(&v[2]);
    c->at_s = r_u32(&v[4]);
    return (ok_ns(c->ns) && ok_rat(c->rat)) ? 0 : LC_BIN_EVALUE;
}

static int tlv_reg(lc_record_t *r, const uint8_t *v, size_t len)
{
    if (len != REG_LEN) {
        return LC_BIN_ETLV;
    }
    r->reg_stat = v[0];
    r->reg_stat_s = r_u32(&v[1]);
    return 0;
}

static int tlv_fplmn(lc_record_t *r, const uint8_t *v, size_t len)
{
    unsigned n;
    unsigned i;

    if (len < 1U) {
        return LC_BIN_ETLV;
    }
    n = v[0];
    if (n > FPLMN_MAX) {
        return LC_BIN_EVALUE;
    }
    if (len != (1U + (PLMN_LEN * n))) {
        return LC_BIN_ETLV;
    }
    r->fplmn_count = (uint8_t)n;
    for (i = n; i < FPLMN_MAX; i++) {     /* a repeated, shorter list */
        plmn_init(&r->fplmn[i]);          /* leaves no stale entries  */
    }
    for (i = 0U; i < n; i++) {
        const uint8_t *e = &v[1U + (PLMN_LEN * i)];
        lc_plmn_t *p = &r->fplmn[i];
        p->mcc = r_u16(e);
        p->mnc = r_u16(&e[2]);
        p->mnc_digits = e[4];
        if (!ok_plmn(p) || (p->mcc == LC_U16_UNKNOWN)) {
            return LC_BIN_EVALUE;
        }
    }
    return 0;
}

static int tlv_edrx(lc_record_t *r, const uint8_t *v, size_t len)
{
    if (len != EDRX_LEN) {
        return LC_BIN_ETLV;
    }
    if (((v[0] > (uint8_t)LC_EDRX_ACT_MAX) && (v[0] != LC_U8_UNKNOWN)) ||
        !ok_nib(v[1]) || !ok_nib(v[2]) || !ok_nib(v[3])) {
        return LC_BIN_EVALUE;
    }
    r->edrx_act = v[0];
    r->edrx_req = v[1];
    r->edrx_nw = v[2];
    r->ptw_nw = v[3];
    return 0;
}

/* Width of a fixed-width scalar tag; 0 for a tag that is not one. */
static size_t scalar_width(uint16_t tag)
{
    size_t width;

    switch (tag) {
    case TAG_CE: case TAG_BACKOFF: case TAG_RAT: case TAG_BAND:
    case TAG_MNC_DIGITS: case TAG_PSM_MODE:
        width = 1U;
        break;
    case TAG_BATT: case TAG_RSRP: case TAG_RSRQ: case TAG_MNC: case TAG_MCC:
    case TAG_SNR: case TAG_TAC: case TAG_ATTEMPTS:
        width = 2U;
        break;
    case TAG_CI: case TAG_T3412_REQ: case TAG_T3324_REQ: case TAG_LAST_OK:
    case TAG_CELL_S: case TAG_T3412_NW: case TAG_T3324_NW: case TAG_UPTIME:
        width = 4U;
        break;
    default:
        width = 0U;
        break;
    }
    return width;
}

static int set_u8(lc_record_t *r, uint16_t tag, uint8_t v)
{
    bool ok = true;

    switch (tag) {
    case TAG_CE:
        ok = v <= 3U;
        if (ok) { r->ce_level = v; }
        break;
    case TAG_BACKOFF:
        ok = v <= LC_BO_SIM_INVALID;
        if (ok) { r->backoff = v; }
        break;
    case TAG_RAT:
        ok = ok_rat(v) && (v != LC_RAT_UNKNOWN);
        if (ok) { r->rat = v; }
        break;
    case TAG_MNC_DIGITS:
        ok = (v == 2U) || (v == 3U);
        if (ok) { r->plmn.mnc_digits = v; }
        break;
    case TAG_PSM_MODE:
        ok = v <= 1U;
        if (ok) { r->psm_mode_req = v; }
        break;
    default:                            /* TAG_BAND: any value */
        r->band = v;
        break;
    }
    return ok ? 0 : LC_BIN_EVALUE;
}

static int set_u16(lc_record_t *r, uint16_t tag, uint16_t v)
{
    bool ok = true;

    switch (tag) {
    case TAG_BATT:     r->battery_mv = v; break;
    case TAG_RSRP:     r->rsrp_dbm = (int16_t)v; break;
    case TAG_RSRQ:     r->rsrq_ddb = (int16_t)v; break;
    case TAG_SNR:      r->snr_ddb = (int16_t)v; break;
    case TAG_MNC:
        ok = v <= 999U;
        if (ok) { r->plmn.mnc = v; }
        break;
    case TAG_MCC:
        ok = v <= 999U;
        if (ok) { r->plmn.mcc = v; }
        break;
    case TAG_TAC:      r->tac = v; break;
    default:           r->attempts = v; break;   /* TAG_ATTEMPTS */
    }
    return ok ? 0 : LC_BIN_EVALUE;
}

static int set_u32(lc_record_t *r, uint16_t tag, uint32_t v)
{
    bool ok = true;

    switch (tag) {
    case TAG_CI:
        ok = v <= 0x0FFFFFFFU;          /* 28-bit ECI */
        if (ok) { r->cell_id = v; }
        break;
    case TAG_T3412_REQ: r->t3412_req_s = v; break;
    case TAG_T3324_REQ: r->t3324_req_s = v; break;
    case TAG_LAST_OK:   r->last_success_s = v; break;
    case TAG_CELL_S:    r->cell_s = v; break;
    case TAG_T3412_NW:  r->t3412_nw_s = v; break;
    case TAG_T3324_NW:  r->t3324_nw_s = v; break;
    default:            r->uptime_s = v; break;  /* TAG_UPTIME */
    }
    return ok ? 0 : LC_BIN_EVALUE;
}

/* A fixed-width scalar TLV, or an unknown tag (skipped). */
static int tlv_scalar(lc_record_t *r, uint16_t tag, const uint8_t *v, size_t len)
{
    size_t want = scalar_width(tag);
    int rc;

    if (want == 0U) {
        rc = 0;                         /* unknown tag: skip */
    } else if (len != want) {
        rc = LC_BIN_ETLV;
    } else if (want == 1U) {
        rc = set_u8(r, tag, v[0]);
    } else if (want == 2U) {
        rc = set_u16(r, tag, r_u16(v));
    } else {
        rc = set_u32(r, tag, r_u32(v));
    }
    return rc;
}

/* Applies one TLV. Returns 0, LC_BIN_ETLV (length) or LC_BIN_EVALUE. */
static int apply_tlv(lc_record_t *r, uint16_t tag, const uint8_t *v, size_t len)
{
    int rc;

    switch (tag) {
    case TAG_FW:    rc = tlv_fw(r, v, len); break;
    case TAG_EMM:   rc = tlv_cause(&r->emm, v, len); break;
    case TAG_ESM:   rc = tlv_cause(&r->esm, v, len); break;
    case TAG_REG:   rc = tlv_reg(r, v, len); break;
    case TAG_FPLMN: rc = tlv_fplmn(r, v, len); break;
    case TAG_EDRX:  rc = tlv_edrx(r, v, len); break;
    default:        rc = tlv_scalar(r, tag, v, len); break;
    }
    return rc;
}

/* Header, length and CRC. On success *end is the offset of the CRC, which
 * is at most len - CRC_LEN. */
static int check_frame(const uint8_t *in, size_t len, size_t *end)
{
    size_t body;

    if ((in == NULL) || (len < (HDR_LEN + CRC_LEN))) {
        return LC_BIN_ESHORT;
    }
    if ((in[0] != (uint8_t)'L') || (in[1] != (uint8_t)'C')) {
        return LC_BIN_EMAGIC;
    }
    if (in[2] != LC_BIN_VERSION) {
        return LC_BIN_EVERSION;
    }
    body = r_u16(&in[3]);
    if ((HDR_LEN + body + CRC_LEN) > len) {
        return LC_BIN_ESHORT;
    }
    *end = HDR_LEN + body;
    if (lc_crc16(in, *end) != r_u16(&in[*end])) {
        return LC_BIN_ECRC;
    }
    return 0;
}

/* Walks the TLVs in in[HDR_LEN..end). Every read stays below end: a TLV
 * header needs 3 bytes left, and its value tlen <= end - pos bytes. */
static int walk_tlvs(const uint8_t *in, size_t end, lc_record_t *r)
{
    size_t pos = HDR_LEN;
    int rc = 0;

    while ((rc == 0) && (pos < end)) {
        if ((end - pos) < TLV_HDR) {
            rc = LC_BIN_ETLV;
        } else {
            uint16_t tag = r_u16(&in[pos]);
            size_t tlen = in[pos + 2U];
            pos += TLV_HDR;
            if (tlen > (end - pos)) {
                rc = LC_BIN_ETLV;
            } else {
                rc = apply_tlv(r, tag, &in[pos], tlen);
                pos += tlen;
            }
        }
    }
    /* MCC, MNC and the digit count arrive in separate TLVs. */
    if ((rc == 0) && !ok_plmn(&r->plmn)) {
        rc = LC_BIN_EVALUE;
    }
    return rc;
}

int record_decode_bin(const uint8_t *in, size_t len, lc_record_t *r)
{
    size_t end = 0U;
    int rc;

    if (r == NULL) {
        return LC_BIN_ESHORT;
    }
    lc_record_init(r);
    rc = check_frame(in, len, &end);
    if (rc == 0) {
        rc = walk_tlvs(in, end, r);
        if (rc != 0) {
            lc_record_init(r);          /* a rejected frame leaves nothing */
        }
    }
    return rc;
}
