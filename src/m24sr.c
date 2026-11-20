/* m24sr.c - M24SR command layer.
 *
 * One exchange:
 *   1. write [PCB][C-APDU][CRC_A]; PCB = 0x02 | block number (I-block)
 *   2. poll: the tag NACKs its address while it works
 *   3. read the response frame of the expected length
 *   4. if it is an S-block WTX (0xF2 ..), answer with the same WTX and go to 2
 *   5. check CRC_A, the echoed PCB, and the status word
 * An error response is shorter than a data response (no data bytes), so a
 * frame whose CRC fails at the expected length is re-checked at 5 bytes.
 */
#include <stddef.h>
#include "m24sr.h"
#include "crc_a.h"

#define PCB_I        0x02U           /* ISO/IEC 14443-4 I-block, no CID/NAD */
#define PCB_DESELECT 0xC2U           /* S(DESELECT) */
#define PCB_WTX      0xF2U           /* S(WTX) */
#define POLL_MS      200U            /* longest wait for one answer */
#define APDU_HDR     5U              /* CLA INS P1 P2 Lc/Le */
#define MAX_APDU     (APDU_HDR + M24SR_CHUNK)
#define MAX_FRAME    (1U + MAX_APDU + 2U)
#define ERR_FRAME_LEN 5U             /* PCB SW1 SW2 CRC_A */
#define SW_OK        0x9000U

void m24sr_init(m24sr_t *d, const m24sr_bus_t *bus)
{
    d->bus = bus;
    d->block = 0U;
    d->last_sw = 0U;
    d->wtx_count = 0U;
    d->busy_polls = 0U;
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint32_t)p[0] << 8U) | (uint32_t)p[1]);
}

static void put_crc(uint8_t *frame, uint32_t len)
{
    uint16_t c = crc_a(frame, len);
    frame[len] = (uint8_t)(c & 0xFFU);
    frame[len + 1U] = (uint8_t)(c >> 8U);
}

static int crc_ok(const uint8_t *frame, uint32_t len)
{
    if (len < 3U) {
        return 0;
    }
    uint16_t c = crc_a(frame, len - 2U);
    return ((frame[len - 2U] == (uint8_t)(c & 0xFFU)) && (frame[len - 1U] == (uint8_t)(c >> 8U))) ? 1 : 0;
}

static int bus_result(int rc)
{
    return (rc == 0) ? 0 : M24SR_ERR_BUS;
}

/* Write, retrying while the tag NACKs (busy). */
static int bus_write(m24sr_t *d, const uint8_t *buf, uint32_t len)
{
    for (uint32_t t = 0U; t < POLL_MS; t++) {
        int rc = d->bus->write(d->bus->ctx, buf, len);
        if (rc != M24SR_NACK) {
            return bus_result(rc);
        }
        d->busy_polls++;
        d->bus->wait_ms(d->bus->ctx, 1U);
    }
    return M24SR_ERR_TIMEOUT;
}

static int bus_read(m24sr_t *d, uint8_t *buf, uint32_t len)
{
    for (uint32_t t = 0U; t < POLL_MS; t++) {
        int rc = d->bus->read(d->bus->ctx, buf, len);
        if (rc != M24SR_NACK) {
            return bus_result(rc);
        }
        d->busy_polls++;
        d->bus->wait_ms(d->bus->ctx, 1U);
    }
    return M24SR_ERR_TIMEOUT;
}

/* Reads the answer frame; answers every S(WTX) request with the same WTXM
 * (ISO/IEC 14443-4 clause 7.3) until a non-WTX frame arrives. */
static int read_answer(m24sr_t *d, uint8_t *frame, uint32_t rlen)
{
    for (;;) {
        int rc = bus_read(d, frame, rlen);
        if (rc != 0) {
            return rc;
        }
        if (frame[0] != PCB_WTX) {
            return 0;
        }
        if (crc_ok(frame, 4U) == 0) {
            return M24SR_ERR_CRC;
        }
        uint8_t wtx[4] = { PCB_WTX, frame[1], 0U, 0U };  /* accept the extension */
        put_crc(wtx, 2U);
        d->wtx_count++;
        rc = bus_write(d, wtx, sizeof wtx);
        if (rc != 0) {
            return rc;
        }
    }
}

/* Send one C-APDU; expect `data_len` data bytes before SW1 SW2.
 * On success copies the data to `data` and returns 0. */
static int exchange(m24sr_t *d, const uint8_t *apdu, uint32_t apdu_len,
                    uint8_t *data, uint32_t data_len)
{
    uint8_t frame[MAX_FRAME];
    if ((apdu_len > MAX_APDU) || (data_len > M24SR_CHUNK)) {
        return M24SR_ERR_ARG;
    }

    uint8_t pcb = (uint8_t)(PCB_I | d->block);
    frame[0] = pcb;
    for (uint32_t i = 0U; i < apdu_len; i++) {
        frame[1U + i] = apdu[i];
    }
    put_crc(frame, 1U + apdu_len);
    int rc = bus_write(d, frame, 1U + apdu_len + 2U);
    if (rc != 0) {
        return rc;
    }

    uint32_t rlen = 1U + data_len + 2U + 2U;          /* PCB data SW CRC */
    rc = read_answer(d, frame, rlen);
    if (rc != 0) {
        return rc;
    }

    uint32_t got = rlen;
    if (crc_ok(frame, got) == 0) {
        got = ERR_FRAME_LEN;                          /* error answer: no data */
        if (crc_ok(frame, got) == 0) {
            return M24SR_ERR_CRC;
        }
    }
    if (frame[0] != pcb) {
        return M24SR_ERR_FRAME;
    }
    d->block ^= 1U;
    d->last_sw = be16(&frame[got - 4U]);
    if (d->last_sw != SW_OK) {
        return M24SR_ERR_SW;
    }
    if (got != rlen) {
        return M24SR_ERR_FRAME;
    }
    for (uint32_t i = 0U; i < data_len; i++) {
        data[i] = frame[1U + i];
    }
    return 0;
}

int m24sr_get_session(m24sr_t *d)
{
    static const uint8_t cmd = 0x26U;
    int rc = d->bus->write(d->bus->ctx, &cmd, 1U);
    d->block = 0U;
    if (rc == M24SR_NACK) {
        return M24SR_NACK;
    }
    return bus_result(rc);
}

int m24sr_kill_rf_session(m24sr_t *d)
{
    static const uint8_t cmd = 0x52U;
    d->block = 0U;
    return bus_write(d, &cmd, 1U);
}

int m24sr_deselect(m24sr_t *d)
{
    uint8_t frame[3] = { PCB_DESELECT, 0U, 0U };
    put_crc(frame, 1U);
    int rc = bus_write(d, frame, sizeof frame);
    if (rc != 0) {
        return rc;
    }
    rc = bus_read(d, frame, sizeof frame);
    if (rc != 0) {
        return rc;
    }
    if ((frame[0] != PCB_DESELECT) || (crc_ok(frame, 3U) == 0)) {
        return M24SR_ERR_FRAME;
    }
    d->block = 0U;
    return 0;
}

int m24sr_select_ndef_app(m24sr_t *d)
{
    static const uint8_t apdu[] = { 0x00U, 0xA4U, 0x04U, 0x00U, 0x07U,
                                    0xD2U, 0x76U, 0x00U, 0x00U, 0x85U, 0x01U, 0x01U, 0x00U };
    return exchange(d, apdu, sizeof apdu, NULL, 0U);
}

int m24sr_select_file(m24sr_t *d, uint16_t file_id)
{
    const uint8_t apdu[] = { 0x00U, 0xA4U, 0x00U, 0x0CU, 0x02U,
                             (uint8_t)(file_id >> 8U), (uint8_t)(file_id & 0xFFU) };
    return exchange(d, apdu, sizeof apdu, NULL, 0U);
}

/* ISO/IEC 7816-4: with P1 bit 8 = 0 the offset is 15 bits. */
static int range_ok(uint16_t offset, uint32_t len)
{
    return ((offset < M24SR_OFFSET_END) && (len <= (M24SR_OFFSET_END - offset))) ? 1 : 0;
}

int m24sr_read_binary(m24sr_t *d, uint16_t offset, uint8_t *buf, uint32_t len)
{
    if (range_ok(offset, len) == 0) {
        return M24SR_ERR_ARG;
    }
    uint32_t done = 0U;
    while (done < len) {
        uint32_t left = len - done;
        uint32_t n = (left > M24SR_CHUNK) ? M24SR_CHUNK : left;
        uint32_t at = (uint32_t)offset + done;
        const uint8_t apdu[] = { 0x00U, 0xB0U, (uint8_t)(at >> 8U), (uint8_t)(at & 0xFFU),
                                 (uint8_t)n };
        int rc = exchange(d, apdu, sizeof apdu, &buf[done], n);
        if (rc != 0) {
            return rc;
        }
        done += n;
    }
    return 0;
}

int m24sr_update_binary(m24sr_t *d, uint16_t offset, const uint8_t *data, uint32_t len)
{
    uint8_t apdu[MAX_APDU];
    if (range_ok(offset, len) == 0) {
        return M24SR_ERR_ARG;
    }
    uint32_t done = 0U;
    while (done < len) {
        uint32_t left = len - done;
        uint32_t n = (left > M24SR_CHUNK) ? M24SR_CHUNK : left;
        uint32_t at = (uint32_t)offset + done;
        apdu[0] = 0x00U;
        apdu[1] = 0xD6U;
        apdu[2] = (uint8_t)(at >> 8U);
        apdu[3] = (uint8_t)(at & 0xFFU);
        apdu[4] = (uint8_t)n;
        for (uint32_t i = 0U; i < n; i++) {
            apdu[APDU_HDR + i] = data[done + i];
        }
        int rc = exchange(d, apdu, APDU_HDR + n, NULL, 0U);
        if (rc != 0) {
            return rc;
        }
        done += n;
    }
    return 0;
}

int m24sr_parse_cc(const uint8_t *cc, uint32_t len, m24sr_cc_t *out)
{
    if ((len < 15U) || (cc[7] != 0x04U) || (cc[8] != 0x06U)) {
        return M24SR_ERR_FRAME;
    }
    out->cclen        = be16(&cc[0]);
    out->version      = cc[2];
    out->mle          = be16(&cc[3]);
    out->mlc          = be16(&cc[5]);
    out->ndef_file_id = be16(&cc[9]);
    out->ndef_max     = be16(&cc[11]);
    out->read_access  = cc[13];
    out->write_access = cc[14];
    return 0;
}

int m24sr_read_ndef(m24sr_t *d, uint16_t file_id, uint8_t *buf, uint32_t buf_size)
{
    uint8_t nlen_b[2];
    int rc = m24sr_select_file(d, file_id);
    if (rc != 0) {
        return rc;
    }
    rc = m24sr_read_binary(d, 0U, nlen_b, 2U);
    if (rc != 0) {
        return rc;
    }
    uint32_t nlen = be16(nlen_b);
    if (nlen > buf_size) {
        return M24SR_ERR_ARG;
    }
    rc = m24sr_read_binary(d, 2U, buf, nlen);
    if (rc != 0) {
        return rc;
    }
    return (int)nlen;
}

int m24sr_write_ndef(m24sr_t *d, uint16_t file_id, const uint8_t *msg, uint32_t len)
{
    static const uint8_t zero[2] = { 0U, 0U };
    const uint8_t nlen_b[2] = { (uint8_t)(len >> 8U), (uint8_t)(len & 0xFFU) };
    if (range_ok(2U, len) == 0) {
        return M24SR_ERR_ARG;                         /* checked before NLEN is cleared */
    }
    int rc = m24sr_select_file(d, file_id);
    if (rc != 0) {
        return rc;
    }
    rc = m24sr_update_binary(d, 0U, zero, 2U);
    if (rc != 0) {
        return rc;
    }
    rc = m24sr_update_binary(d, 2U, msg, len);
    if (rc != 0) {
        return rc;
    }
    return m24sr_update_binary(d, 0U, nlen_b, 2U);
}
