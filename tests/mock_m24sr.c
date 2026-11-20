/* mock_m24sr.c - a host-side M24SR that answers the frames the driver sends.
 * It checks CRC_A and the I-block number, keeps the CC, system and NDEF files,
 * and can be told to be busy (NACK its address) or to ask for a WTX first. */
#include <string.h>
#include "mock_m24sr.h"
#include "../src/crc_a.h"
#include "../src/m24sr.h"

static void add_crc(uint8_t *f, uint32_t len)
{
    uint16_t c = crc_a(f, len);
    f[len] = (uint8_t)(c & 0xFFu);
    f[len + 1u] = (uint8_t)(c >> 8);
}

static void respond(mock_m24sr_t *m, uint8_t pcb, const uint8_t *data, uint32_t n, uint16_t sw)
{
    uint32_t k = 0;
    m->resp[k++] = pcb;
    for (uint32_t i = 0; i < n; i++) m->resp[k++] = data[i];
    m->resp[k++] = (uint8_t)(sw >> 8);
    m->resp[k++] = (uint8_t)(sw & 0xFFu);
    add_crc(m->resp, k);
    m->resp_len = k + 2u;
}

static uint8_t *file_ptr(mock_m24sr_t *m, uint32_t *size)
{
    switch (m->selected) {
    case 0xE103: *size = sizeof m->cc;   return m->cc;
    case 0xE101: *size = sizeof m->sys;  return m->sys;
    case 0x0001: *size = sizeof m->ndef; return m->ndef;
    default:     *size = 0;              return 0;
    }
}

static void handle_apdu(mock_m24sr_t *m, uint8_t pcb, const uint8_t *a, uint32_t n)
{
    static const uint8_t aid[] = { 0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01 };
    if (n >= 12u && a[1] == 0xA4 && a[2] == 0x04 && a[4] == 7u && !memcmp(&a[5], aid, 7)) {
        m->app_selected = 1;
        m->selected = 0;
        respond(m, pcb, 0, 0, 0x9000u);
    } else if (n == 7u && a[1] == 0xA4 && a[2] == 0x00 && a[3] == 0x0C) {
        uint16_t fid = (uint16_t)((a[5] << 8) | a[6]);
        if (!m->app_selected || (fid != 0xE103 && fid != 0xE101 && fid != 0x0001)) {
            respond(m, pcb, 0, 0, 0x6A82u);
        } else {
            m->selected = fid;
            respond(m, pcb, 0, 0, 0x9000u);
        }
    } else if (n == 5u && a[1] == 0xB0) {
        uint32_t size, off = (uint32_t)((a[2] << 8) | a[3]), le = a[4];
        uint8_t *f = file_ptr(m, &size);
        if (!f || off + le > size) respond(m, pcb, 0, 0, 0x6A82u);
        else respond(m, pcb, f + off, le, 0x9000u);
    } else if (n >= 5u && a[1] == 0xD6) {
        uint32_t size, off = (uint32_t)((a[2] << 8) | a[3]), lc = a[4];
        uint8_t *f = file_ptr(m, &size);
        if (!f || m->selected == 0xE103 || off + lc > size || n != 5u + lc) {
            respond(m, pcb, 0, 0, 0x6A82u);
        } else {
            memcpy(f + off, &a[5], lc);
            m->writes++;
            respond(m, pcb, 0, 0, 0x9000u);
        }
    } else {
        respond(m, pcb, 0, 0, 0x6D00u);
    }
}

static int mock_write(void *ctx, const uint8_t *buf, uint32_t len)
{
    mock_m24sr_t *m = ctx;
    if (m->busy_left) { m->busy_left--; return M24SR_NACK; }
    if (len == 1u && buf[0] == 0x26u) {
        if (m->rf_session) return M24SR_NACK;
        m->session = 1;
        m->expect_block = 0;
        return 0;
    }
    if (len == 1u && buf[0] == 0x52u) {
        m->rf_session = 0;
        m->session = 1;
        m->expect_block = 0;
        return 0;
    }
    if (!m->session || len < 3u) return M24SR_NACK;
    uint16_t c = crc_a(buf, len - 2u);
    if (buf[len - 2u] != (uint8_t)(c & 0xFFu) || buf[len - 1u] != (uint8_t)(c >> 8)) {
        m->crc_errors++;
        return 0;                                    /* a real tag stays silent */
    }
    if (buf[0] == 0xC2u) {                           /* DESELECT */
        m->resp[0] = 0xC2u;
        add_crc(m->resp, 1u);
        m->resp_len = 3u;
        m->session = 0;
        return 0;
    }
    if (buf[0] == 0xF2u) {                           /* host accepted the WTX */
        memcpy(m->resp, m->held, m->held_len);
        m->resp_len = m->held_len;
        return 0;
    }
    if ((buf[0] & 0xFEu) != 0x02u) { m->frame_errors++; return 0; }
    if ((buf[0] & 1u) != m->expect_block) m->block_errors++;
    m->expect_block ^= 1u;
    handle_apdu(m, buf[0], buf + 1, len - 3u);
    if (m->wtx_next) {                               /* park the answer behind a WTX */
        m->wtx_next = 0;
        memcpy(m->held, m->resp, m->resp_len);
        m->held_len = m->resp_len;
        m->resp[0] = 0xF2u;
        m->resp[1] = 0x0Bu;
        add_crc(m->resp, 2u);
        m->resp_len = 4u;
    }
    return 0;
}

static int mock_read(void *ctx, uint8_t *buf, uint32_t len)
{
    mock_m24sr_t *m = ctx;
    if (m->busy_left) { m->busy_left--; return M24SR_NACK; }
    for (uint32_t i = 0; i < len; i++) buf[i] = i < m->resp_len ? m->resp[i] : 0xFFu;
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
    memset(m, 0, sizeof *m);
    memcpy(m->cc, cc, sizeof cc);
    m->bus.write = mock_write;
    m->bus.read = mock_read;
    m->bus.delay_ms = mock_delay;
    m->bus.ctx = m;
}
