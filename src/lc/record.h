/* record.h - "Last Checkpoint" diagnostic record for an LTE-M / NB-IoT device.
 *
 * One fixed-size struct, no heap. Every field has an explicit "unknown" value
 * (the LC_*_UNKNOWN constants below). Zero is never "unknown": a zero RSRQ
 * index, CE level 0, an empty FPLMN list or a timer of 0 s are real values.
 * Call lc_record_init() before use; a memset-zero record is NOT all-unknown.
 *
 * Fields, in priority order (what explains a failed attach first):
 *   1. last EMM cause and last ESM cause (namespace, RAT, uptime stamp);
 *   2. attempts since the last success, back-off state;
 *   3. serving cell: PLMN, TAC, cell ID, RSRP, RSRQ, SNR, CE level;
 *   4. PSM T3412/T3324 and eDRX, requested versus granted;
 *   5. firmware version string and battery mV.
 */
#ifndef LC_RECORD_H
#define LC_RECORD_H

#include <stddef.h>
#include <stdint.h>

/* Unknown sentinels, one per storage type. */
#define LC_U8_UNKNOWN    0xFFU
#define LC_U16_UNKNOWN   0xFFFFU
#define LC_U32_UNKNOWN   0xFFFFFFFFUL
#define LC_I16_UNKNOWN   INT16_MIN
/* A timer the network or the UE reported as "deactivated" (GPRS Timer unit 111). */
#define LC_TIMER_DEACTIVATED 0xFFFFFFFEUL

#define LC_FW_MAX     23U  /* firmware version characters, not counting NUL */
#define LC_FPLMN_MAX  4U   /* EF_FPLMN entries kept (12 bytes = 4 entries) */
#define LC_TEXT_MAX   300  /* record_encode_text() output limit, characters */

/* Cause namespace. Plain unsigned constants (not an enum) so that they
 * have the same essential type as the uint8_t fields that hold them. */
#define LC_NS_EMM      1U            /* TS 24.301 Annex A / 9.9.3.9 */
#define LC_NS_ESM      2U            /* TS 24.301 Annex B / 9.9.4.4 */
#define LC_NS_VENDOR   3U            /* +CEREG cause_type 1: manufacturer specific */
#define LC_NS_UNKNOWN  LC_U8_UNKNOWN

/* Radio access technology (own numbering, not the 27.007 AcT numbering). */
#define LC_RAT_GSM     1U
#define LC_RAT_LTE     2U            /* E-UTRAN, category not known (27.007 AcT 7) */
#define LC_RAT_LTE_M   3U            /* E-UTRAN WB-S1 */
#define LC_RAT_NB_IOT  4U            /* E-UTRAN NB-S1 (27.007 AcT 9) */
#define LC_RAT_NR      5U
#define LC_RAT_OTHER   6U
#define LC_RAT_UNKNOWN LC_U8_UNKNOWN

/* Back-off state, derived from the last EMM cause (simplified from
 * TS 24.301 clause 5.5.1.2.5) or set by the application. */
#define LC_BO_NONE           0U      /* registered, nothing pending */
#define LC_BO_RETRY          1U      /* attempt counter / T3411 / T3402 */
#define LC_BO_CONGESTION     2U      /* #22: T3346 running */
#define LC_BO_TA_FORBIDDEN   3U      /* #12, #13, #15: TA in a forbidden-TA list */
#define LC_BO_PLMN_FORBIDDEN 4U      /* #11, #14: PLMN in a forbidden-PLMN list */
#define LC_BO_SIM_INVALID    5U      /* #3, #6, #7, #8: USIM invalid until switch-off or UICC removal */
#define LC_BO_UNKNOWN        LC_U8_UNKNOWN

/* +CEREG <stat> values (TS 27.007 V19.6.0 clause 10.1.22) are stored as
 * received, 0..13. */
#define LC_REG_HOME     1
#define LC_REG_SEARCH   2
#define LC_REG_DENIED   3
#define LC_REG_ROAMING  5

/* +CEDRXRDP <AcT-type> range, TS 27.007 V19.6.0 clause 7.41 (9 = satellite NG-RAN). */
#define LC_EDRX_ACT_MAX 9

typedef struct {
    uint8_t  ns;      /* LC_NS_* */
    uint8_t  rat;     /* LC_RAT_* at the time of the cause */
    uint16_t value;   /* cause number, LC_U16_UNKNOWN if none */
    uint32_t at_s;    /* uptime stamp, seconds */
} lc_cause_t;

typedef struct {
    uint16_t mcc;        /* 0..999, LC_U16_UNKNOWN */
    uint16_t mnc;        /* 0..999, LC_U16_UNKNOWN */
    uint8_t  mnc_digits; /* 2 or 3, LC_U8_UNKNOWN */
} lc_plmn_t;

typedef struct {
    /* 1. Causes */
    lc_cause_t emm;            /* last NAS registration cause (EMM or vendor) */
    lc_cause_t esm;            /* last ESM cause */
    uint8_t    reg_stat;       /* last +CEREG <stat>, LC_U8_UNKNOWN */
    uint32_t   reg_stat_s;     /* uptime when reg_stat was seen */

    /* 2. Attempts and back-off */
    uint16_t   attempts;       /* registration failures since the last success */
    uint8_t    backoff;        /* LC_BO_* */
    uint32_t   last_success_s; /* uptime of the last registered state */

    /* 3. Serving cell */
    uint8_t    rat;            /* LC_RAT_* */
    uint8_t    band;           /* E-UTRA band, LC_U8_UNKNOWN */
    lc_plmn_t  plmn;
    uint32_t   tac;            /* 16-bit TAC, LC_U32_UNKNOWN */
    uint32_t   cell_id;        /* 28-bit E-UTRAN cell identity, LC_U32_UNKNOWN */
    int16_t    rsrp_dbm;       /* dBm, LC_I16_UNKNOWN */
    int16_t    rsrq_ddb;       /* tenths of a dB, LC_I16_UNKNOWN */
    int16_t    snr_ddb;        /* tenths of a dB, LC_I16_UNKNOWN */
    uint8_t    ce_level;       /* 0..3, LC_U8_UNKNOWN */
    uint32_t   cell_s;         /* uptime of the last serving-cell update */

    /* SIM forbidden-PLMN list (EF_FPLMN, 6F7B). count 0 = read and empty. */
    uint8_t    fplmn_count;    /* LC_U8_UNKNOWN until read */
    lc_plmn_t  fplmn[LC_FPLMN_MAX];

    /* 4. Power saving: seconds, LC_U32_UNKNOWN or LC_TIMER_DEACTIVATED */
    uint8_t    psm_mode_req;   /* +CPSMS <mode> 0/1 */
    uint32_t   t3412_req_s;
    uint32_t   t3324_req_s;
    uint32_t   t3412_nw_s;
    uint32_t   t3324_nw_s;
    uint8_t    edrx_act;       /* +CEDRXRDP <AcT-type> 0..LC_EDRX_ACT_MAX */
    uint8_t    edrx_req;       /* 4-bit eDRX code (TS 24.008 10.5.5.32) */
    uint8_t    edrx_nw;        /* 4-bit eDRX code granted */
    uint8_t    ptw_nw;         /* 4-bit paging time window granted */

    /* 5. Device */
    char       fw[LC_FW_MAX + 1U]; /* NUL-terminated; fw[0] == '\0' = unknown */
    uint16_t   battery_mv;

    uint32_t   uptime_s;       /* uptime when the record was last touched */
} lc_record_t;

void lc_record_init(lc_record_t *r);

/* Application setters. */
void lc_record_set_fw(lc_record_t *r, const char *fw);   /* truncated to LC_FW_MAX */
void lc_record_set_battery(lc_record_t *r, uint16_t mv);
void lc_record_set_band(lc_record_t *r, uint8_t band);
void lc_record_set_ce_level(lc_record_t *r, uint8_t ce); /* > 3 stores unknown */

/* Apply an EMM cause (from +CEREG or %CEER): stores it and derives back-off. */
void lc_record_set_emm(lc_record_t *r, uint8_t ns, uint16_t cause, uint32_t now_s);
void lc_record_set_esm(lc_record_t *r, uint16_t cause, uint32_t now_s);

/* Back-off state that TS 24.301 5.5.1.2.5 implies for an EMM cause. */
uint8_t lc_backoff_for_emm(uint16_t cause);

/* Cause texts; NULL if the value is not in the table. */
const char *lc_emm_cause_text(uint16_t cause);
const char *lc_esm_cause_text(uint16_t cause);

/* Human-readable summary, ASCII (a subset of UTF-8), at most LC_TEXT_MAX
 * characters and never more than cap - 1. Segments are added in priority
 * order; a segment that would not fit is left out whole.
 * Returns the length written (NUL not counted), or -1 if out is NULL/cap 0. */
int record_encode_text(const lc_record_t *r, char *out, size_t cap);

/* Binary TLV, see record.c for the layout. */
#define LC_BIN_VERSION 1U
#define LC_BIN_MAX     224   /* upper bound of record_encode_bin() output (214 used) */

/* Returns bytes written, or -1 if cap is too small. */
int record_encode_bin(const lc_record_t *r, uint8_t *out, size_t cap);
/* Returns 0 and fills *r, or a negative LC_BIN_E* code (r is then all-unknown). */
int record_decode_bin(const uint8_t *in, size_t len, lc_record_t *r);

#define LC_BIN_ESHORT    (-1)  /* shorter than header + CRC, or length field too big */
#define LC_BIN_EMAGIC    (-2)
#define LC_BIN_EVERSION  (-3)
#define LC_BIN_ECRC      (-4)
#define LC_BIN_ETLV      (-5)  /* TLV overruns, or a known tag with a wrong length */
#define LC_BIN_EVALUE    (-6)  /* a known tag with an out-of-range value */

/* CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no xorout). */
uint16_t lc_crc16(const uint8_t *p, size_t n);

#endif /* LC_RECORD_H */
