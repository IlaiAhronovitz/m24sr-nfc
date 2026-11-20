/* lc_test_util.h - helpers shared by test_lc.c and fuzz_at.c.
 *
 * record_equal() compares two records member by member. A memcmp() of the
 * whole struct would also compare padding bytes (CERT EXP42-C); members are
 * what the encoders and the parser read and write.
 * mem_fill() / mem_copy() / mem_move() are plain byte loops, so the tests do
 * not depend on the optional Annex K functions. */
#ifndef LC_TEST_UTIL_H
#define LC_TEST_UTIL_H

#include <stddef.h>
#include <stdint.h>
#include "../../src/lc/record.h"

static inline int cause_equal(const lc_cause_t *a, const lc_cause_t *b)
{
    return a->ns == b->ns && a->rat == b->rat && a->value == b->value && a->at_s == b->at_s;
}

static inline int plmn_equal(const lc_plmn_t *a, const lc_plmn_t *b)
{
    return a->mcc == b->mcc && a->mnc == b->mnc && a->mnc_digits == b->mnc_digits;
}

static inline int cell_equal(const lc_record_t *a, const lc_record_t *b)
{
    return a->rat == b->rat && a->band == b->band && plmn_equal(&a->plmn, &b->plmn) &&
           a->tac == b->tac && a->cell_id == b->cell_id && a->rsrp_dbm == b->rsrp_dbm &&
           a->rsrq_ddb == b->rsrq_ddb && a->snr_ddb == b->snr_ddb &&
           a->ce_level == b->ce_level && a->cell_s == b->cell_s;
}

static inline int fplmn_equal(const lc_record_t *a, const lc_record_t *b)
{
    unsigned i;

    if (a->fplmn_count != b->fplmn_count) {
        return 0;
    }
    for (i = 0; i < LC_FPLMN_MAX; i++) {
        if (!plmn_equal(&a->fplmn[i], &b->fplmn[i])) {
            return 0;
        }
    }
    return 1;
}

static inline int power_equal(const lc_record_t *a, const lc_record_t *b)
{
    return a->psm_mode_req == b->psm_mode_req && a->t3412_req_s == b->t3412_req_s &&
           a->t3324_req_s == b->t3324_req_s && a->t3412_nw_s == b->t3412_nw_s &&
           a->t3324_nw_s == b->t3324_nw_s && a->edrx_act == b->edrx_act &&
           a->edrx_req == b->edrx_req && a->edrx_nw == b->edrx_nw && a->ptw_nw == b->ptw_nw;
}

static inline int device_equal(const lc_record_t *a, const lc_record_t *b)
{
    unsigned i;

    for (i = 0; i <= LC_FW_MAX; i++) {
        if (a->fw[i] != b->fw[i]) {
            return 0;
        }
    }
    return a->battery_mv == b->battery_mv && a->uptime_s == b->uptime_s;
}

static inline int record_equal(const lc_record_t *a, const lc_record_t *b)
{
    return cause_equal(&a->emm, &b->emm) && cause_equal(&a->esm, &b->esm) &&
           a->reg_stat == b->reg_stat && a->reg_stat_s == b->reg_stat_s &&
           a->attempts == b->attempts && a->backoff == b->backoff &&
           a->last_success_s == b->last_success_s && cell_equal(a, b) &&
           fplmn_equal(a, b) && power_equal(a, b) && device_equal(a, b);
}

static inline void mem_fill(void *dst, uint8_t v, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    size_t i;

    for (i = 0; i < n; i++) {
        d[i] = v;
    }
}

static inline void mem_copy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    size_t i;

    for (i = 0; i < n; i++) {
        d[i] = s[i];
    }
}

/* Overlapping copy, either direction. */
static inline void mem_move(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    size_t i;

    if (d < s) {
        mem_copy(dst, src, n);
        return;
    }
    for (i = n; i > 0; i--) {
        d[i - 1] = s[i - 1];
    }
}

#endif /* LC_TEST_UTIL_H */
