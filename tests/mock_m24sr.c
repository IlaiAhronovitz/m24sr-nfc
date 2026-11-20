/* mock_m24sr.c - a host-side M24SR that answers the frames the driver sends.
 * It checks CRC_A and the I-block number, keeps the CC, system and NDEF files,
 * and can be told to be busy (NACK its address), to ask for a WTX first, or
 * to inject bus errors and corrupted answers. */
#include <stddef.h>
#include "mock_m24sr.h"
#include "../src/crc_a.h"
#include "../src/m24sr.h"

static void copy_bytes(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    for (uint32_t i = 0U; i < n; i++) {
        dst[i] = src[i];
    }
}

static int same_bytes(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    for (uint32_t i = 0U; i < n; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

static void add_crc(uint8_t *f, uint32_t len)
{
    uint16_t c = crc_a(f, len);
    f[len] = (uint8_t)(c & 0xFFU);
    f[len + 1U] = (uint8_t)(c >> 8U);
}

static void respond(mock_m24sr_t *m, uint8_t pcb, const uint8_t *data, uint32_t n, uint16_t sw)
{
    uint32_t k = 0U;
    if (m->flip_pcb_next != 0) {
        m->flip_pcb_next = 0;
        pcb ^= 1U;
    }
    m->resp[k] = pcb;
    k++;
    for (uint32_t i = 0U; i < n; i++) {
        m->resp[k] = data[i];
        k++;
    }
    m->resp[k] = (uint8_t)(sw >> 8U);
    m->resp[k + 1U] = (uint8_t)(sw & 0xFFU);
    k += 2U;
    add_crc(m->resp, k);
    m->resp_len = k + 2U;
}

static uint8_t *file_ptr(mock_m24sr_t *m, uint32_t *size)
{
    switch (m->selected) {
    case 0xE103: *size = sizeof m->cc;   return m->cc;
    case 0xE101: *size = sizeof m->sys;  return m->sys;
    case 0x0001: *size = sizeof m->ndef; return m->ndef;
    default:     *size = 0;              return NULL;
    }
}

static uint32_t be16(const uint8_t *p)
{
    return ((uint32_t)p[0] << 8U) | (uint32_t)p[1];
}

static void select_file(mock_m24sr_t *m, uint8_t pcb, const uint8_t *a)
{
    uint16_t fid = (uint16_t)be16(&a[5]);
    if ((m->app_selected == 0) || ((fid != 0xE103U) && (fid != 0xE101U) && (fid != 0x0001U))) {
        respond(m, pcb, NULL, 0U, 0x6A82U);
    } else {
        m->selected = fid;
        respond(m, pcb, NULL, 0U, 0x9000U);
    }
}

static void read_binary(mock_m24sr_t *m, uint8_t pcb, const uint8_t *a)
{
    uint32_t size;
    uint32_t off = be16(&a[2]);
    uint32_t le = a[4];
    const uint8_t *f = file_ptr(m, &size);
    if (m->ok_no_data != 0) {
        m->ok_no_data = 0;
        respond(m, pcb, NULL, 0U, 0x9000U);
    } else if ((f == NULL) || ((off + le) > size)) {
        respond(m, pcb, NULL, 0U, 0x6A82U);
    } else {
        respond(m, pcb, &f[off], le, 0x9000U);
    }
}

static void update_binary(mock_m24sr_t *m, uint8_t pcb, const uint8_t *a, uint32_t n)
{
    uint32_t size;
    uint32_t off = be16(&a[2]);
    uint32_t lc = a[4];
    uint8_t *f = file_ptr(m, &size);
    if (m->n_updates < MOCK_LOG_MAX) {
        mock_update_t *u = &m->updates[m->n_updates];
        u->off = off;
        u->len = lc;
        u->first[0] = (lc > 0U) ? a[5] : 0U;
        u->first[1] = (lc > 1U) ? a[6] : 0U;
        m->n_updates++;
    }
    if ((f == NULL) || (m->selected == 0xE103U) || ((off + lc) > size) || (n != (5U + lc))) {
        respond(m, pcb, NULL, 0U, 0x6A82U);
    } else {
        copy_bytes(&f[off], &a[5], lc);
        m->writes++;
        respond(m, pcb, NULL, 0U, 0x9000U);
    }
}

static void handle_apdu(mock_m24sr_t *m, uint8_t pcb, const uint8_t *a, uint32_t n)
{
    static const uint8_t aid[] = { 0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01 };
    if ((n >= 12U) && (a[1] == 0xA4U) && (a[2] == 0x04U) && (a[4] == 7U) && (same_bytes(&a[5], aid, 7U) != 0)) {
        m->app_selected = 1;
        m->selected = 0;
        respond(m, pcb, NULL, 0U, 0x9000U);
    } else if ((n == 7U) && (a[1] == 0xA4U) && (a[2] == 0x00U) && (a[3] == 0x0CU)) {
        select_file(m, pcb, a);
    } else if ((n == 5U) && (a[1] == 0xB0U)) {
        read_binary(m, pcb, a);
    } else if ((n >= 5U) && (a[1] == 0xD6U)) {
        update_binary(m, pcb, a, n);
    } else {
        respond(m, pcb, NULL, 0U, 0x6D00U);
    }
}

/* GetI2Csession / KillRFsession: one byte, no CRC. */
static int session_cmd(mock_m24sr_t *m, uint8_t cmd)
{
    if ((cmd == 0x26U) && (m->rf_session != 0)) {
        return M24SR_NACK;
    }
    m->rf_session = 0;
    m->session = 1;
    m->expect_block = 0;
    return 0;
}

static void park_behind_wtx(mock_m24sr_t *m)
{
    m->wtx_next = 0;
    copy_bytes(m->held, m->resp, m->resp_len);
    m->held_len = m->resp_len;
    m->resp[0] = 0xF2U;
    m->resp[1] = 0x0BU;
    add_crc(m->resp, 2U);
    m->resp_len = 4U;
}

static int mock_write(void *ctx, const uint8_t *buf, uint32_t len)
{
    mock_m24sr_t *m = ctx;
    if (m->busy_left > 0U) {
        m->busy_left--;
        return M24SR_NACK;
    }
    m->write_calls++;
    if (m->write_calls == m->fail_write_call) {
        return -2;
    }
    m->last_tx_len = (len <= sizeof m->last_tx) ? len : (uint32_t)sizeof m->last_tx;
    copy_bytes(m->last_tx, buf, m->last_tx_len);
    if ((len == 1U) && ((buf[0] == 0x26U) || (buf[0] == 0x52U))) {
        return session_cmd(m, buf[0]);
    }
    if ((m->session == 0) || (len < 3U)) {
        return M24SR_NACK;
    }
    uint16_t c = crc_a(buf, len - 2U);
    if ((buf[len - 2U] != (uint8_t)(c & 0xFFU)) || (buf[len - 1U] != (uint8_t)(c >> 8U))) {
        m->crc_errors++;
        return 0;                                    /* a real tag stays silent */
    }
    if (buf[0] == 0xC2U) {                           /* DESELECT */
        m->resp[0] = (m->flip_pcb_next != 0) ? 0xC3U : 0xC2U;
        m->flip_pcb_next = 0;
        add_crc(m->resp, 1U);
        m->resp_len = 3U;
        m->session = 0;
        return 0;
    }
    if (buf[0] == 0xF2U) {                           /* host accepted the WTX */
        copy_bytes(m->resp, m->held, m->held_len);
        m->resp_len = m->held_len;
        return 0;
    }
    if ((buf[0] & 0xFEU) != 0x02U) {
        m->frame_errors++;
        return 0;
    }
    if ((buf[0] & 1U) != m->expect_block) {
        m->block_errors++;
    }
    m->expect_block ^= 1U;
    handle_apdu(m, buf[0], &buf[1], len - 3U);
    if (m->wtx_next != 0) {
        park_behind_wtx(m);
    }
    return 0;
}

static int mock_read(void *ctx, uint8_t *buf, uint32_t len)
{
    mock_m24sr_t *m = ctx;
    if (m->busy_left > 0U) {
        m->busy_left--;
        return M24SR_NACK;
    }
    if (m->busy_read_left > 0U) {
        m->busy_read_left--;
        return M24SR_NACK;
    }
    m->read_calls++;
    if (m->read_calls == m->fail_read_call) {
        return -2;
    }
    for (uint32_t i = 0U; i < len; i++) {
        buf[i] = (i < m->resp_len) ? m->resp[i] : 0xFFU;
    }
    if ((m->corrupt_next != 0) && (m->resp_len > 0U) && (m->resp_len <= len)) {
        m->corrupt_next = 0;
        buf[m->resp_len - 1U] ^= 0x01U;
    }
    return 0;
}

static void mock_delay(void *ctx, uint32_t ms)
{
    mock_m24sr_t *m = ctx;
    m->ms_waited += ms;
}

void mock_m24sr_init(mock_m24sr_t *m)
{
    static const uint8_t cc[15] = { 0x00, 0x0F, 0x20, 0x00, 0xF6, 0x00, 0xF6,
                                    0x04, 0x06, 0x00, 0x01, 0x20, 0x00, 0x00, 0x00 };
    static const mock_m24sr_t zero;
    *m = zero;
    copy_bytes(m->cc, cc, sizeof cc);
    m->bus.write = mock_write;
    m->bus.read = mock_read;
    m->bus.wait_ms = mock_delay;
    m->bus.ctx = m;
}
