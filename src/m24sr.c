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
#include "m24sr.h"
#include "crc_a.h"

#define PCB_I        0x02u
#define PCB_DESELECT 0xC2u
#define PCB_WTX      0xF2u
#define POLL_MS      200u            /* longest wait for one answer */
#define MAX_APDU     (5u + M24SR_CHUNK)
#define MAX_FRAME    (1u + MAX_APDU + 2u)

void m24sr_init(m24sr_t *d, const m24sr_bus_t *bus)
{
    d->bus = bus;
    d->block = 0;
    d->last_sw = 0;
    d->wtx_count = 0;
    d->busy_polls = 0;
}

static void put_crc(uint8_t *frame, uint32_t len)
{
    uint16_t c = crc_a(frame, len);
    frame[len] = (uint8_t)(c & 0xFFu);
    frame[len + 1u] = (uint8_t)(c >> 8);
}

static int crc_ok(const uint8_t *frame, uint32_t len)
{
    if (len < 3u) return 0;
    uint16_t c = crc_a(frame, len - 2u);
    return frame[len - 2u] == (uint8_t)(c & 0xFFu) && frame[len - 1u] == (uint8_t)(c >> 8);
}

/* Write, retrying while the tag NACKs (busy). */
static int bus_write(m24sr_t *d, const uint8_t *buf, uint32_t len)
{
    for (uint32_t t = 0; t < POLL_MS; t++) {
        int rc = d->bus->write(d->bus->ctx, buf, len);
        if (rc != M24SR_NACK) return rc == 0 ? 0 : M24SR_ERR_BUS;
        d->busy_polls++;
        d->bus->delay_ms(d->bus->ctx, 1u);
    }
    return M24SR_ERR_TIMEOUT;
}

static int bus_read(m24sr_t *d, uint8_t *buf, uint32_t len)
{
    for (uint32_t t = 0; t < POLL_MS; t++) {
        int rc = d->bus->read(d->bus->ctx, buf, len);
        if (rc != M24SR_NACK) return rc == 0 ? 0 : M24SR_ERR_BUS;
        d->busy_polls++;
        d->bus->delay_ms(d->bus->ctx, 1u);
    }
    return M24SR_ERR_TIMEOUT;
}

/* Send one C-APDU; expect `data_len` data bytes before SW1 SW2.
 * On success copies the data to `data` and returns 0. */
static int exchange(m24sr_t *d, const uint8_t *apdu, uint32_t apdu_len,
                    uint8_t *data, uint32_t data_len)
{
    uint8_t frame[MAX_FRAME];
    if (apdu_len > MAX_APDU || data_len > M24SR_CHUNK) return M24SR_ERR_ARG;

    uint8_t pcb = (uint8_t)(PCB_I | d->block);
    frame[0] = pcb;
    for (uint32_t i = 0; i < apdu_len; i++) frame[1 + i] = apdu[i];
    put_crc(frame, 1u + apdu_len);
    int rc = bus_write(d, frame, 1u + apdu_len + 2u);
    if (rc) return rc;

    uint32_t rlen = 1u + data_len + 2u + 2u;          /* PCB data SW CRC */
    for (;;) {
        rc = bus_read(d, frame, rlen);
        if (rc) return rc;
        if (frame[0] != PCB_WTX) break;
        if (!crc_ok(frame, 4u)) return M24SR_ERR_CRC;
        uint8_t wtx[4] = { PCB_WTX, frame[1], 0, 0 };  /* accept the extension */
        put_crc(wtx, 2u);
        d->wtx_count++;
        rc = bus_write(d, wtx, sizeof wtx);
        if (rc) return rc;
    }

    uint32_t got = rlen;
    if (!crc_ok(frame, got)) {
        got = 5u;                                     /* PCB SW1 SW2 CRC: error answer */
        if (!crc_ok(frame, got)) return M24SR_ERR_CRC;
    }
    if (frame[0] != pcb) return M24SR_ERR_FRAME;
    d->block ^= 1u;
    d->last_sw = (uint16_t)((frame[got - 4u] << 8) | frame[got - 3u]);
    if (d->last_sw != 0x9000u) return M24SR_ERR_SW;
    if (got != rlen) return M24SR_ERR_FRAME;
    for (uint32_t i = 0; i < data_len; i++) data[i] = frame[1 + i];
    return 0;
}

int m24sr_get_session(m24sr_t *d)
{
    static const uint8_t cmd = 0x26u;
    int rc = d->bus->write(d->bus->ctx, &cmd, 1u);
    d->block = 0;
    return rc == 0 ? 0 : (rc == M24SR_NACK ? M24SR_NACK : M24SR_ERR_BUS);
}

int m24sr_kill_rf_session(m24sr_t *d)
{
    static const uint8_t cmd = 0x52u;
    d->block = 0;
    return bus_write(d, &cmd, 1u);
}

int m24sr_deselect(m24sr_t *d)
{
    uint8_t frame[3] = { PCB_DESELECT, 0, 0 };
    put_crc(frame, 1u);
    int rc = bus_write(d, frame, sizeof frame);
    if (rc) return rc;
    rc = bus_read(d, frame, sizeof frame);
    if (rc) return rc;
    if (frame[0] != PCB_DESELECT || !crc_ok(frame, 3u)) return M24SR_ERR_FRAME;
    d->block = 0;
    return 0;
}

int m24sr_select_ndef_app(m24sr_t *d)
{
    static const uint8_t apdu[] = { 0x00, 0xA4, 0x04, 0x00, 0x07,
                                    0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01, 0x00 };
    return exchange(d, apdu, sizeof apdu, 0, 0);
}

int m24sr_select_file(m24sr_t *d, uint16_t file_id)
{
    uint8_t apdu[] = { 0x00, 0xA4, 0x00, 0x0C, 0x02,
                       (uint8_t)(file_id >> 8), (uint8_t)(file_id & 0xFFu) };
    return exchange(d, apdu, sizeof apdu, 0, 0);
}

int m24sr_read_binary(m24sr_t *d, uint16_t offset, uint8_t *buf, uint32_t len)
{
    while (len) {
        uint32_t n = len > M24SR_CHUNK ? M24SR_CHUNK : len;
        uint8_t apdu[] = { 0x00, 0xB0, (uint8_t)(offset >> 8), (uint8_t)(offset & 0xFFu),
                           (uint8_t)n };
        int rc = exchange(d, apdu, sizeof apdu, buf, n);
        if (rc) return rc;
        offset = (uint16_t)(offset + n);
        buf += n;
        len -= n;
    }
    return 0;
}

int m24sr_update_binary(m24sr_t *d, uint16_t offset, const uint8_t *data, uint32_t len)
{
    uint8_t apdu[MAX_APDU];
    while (len) {
        uint32_t n = len > M24SR_CHUNK ? M24SR_CHUNK : len;
        apdu[0] = 0x00;
        apdu[1] = 0xD6;
        apdu[2] = (uint8_t)(offset >> 8);
        apdu[3] = (uint8_t)(offset & 0xFFu);
        apdu[4] = (uint8_t)n;
        for (uint32_t i = 0; i < n; i++) apdu[5 + i] = data[i];
        int rc = exchange(d, apdu, 5u + n, 0, 0);
        if (rc) return rc;
        offset = (uint16_t)(offset + n);
        data += n;
        len -= n;
    }
    return 0;
}

int m24sr_parse_cc(const uint8_t *cc, uint32_t len, m24sr_cc_t *out)
{
    if (len < 15u || cc[7] != 0x04u || cc[8] != 0x06u) return M24SR_ERR_FRAME;
    out->cclen        = (uint16_t)((cc[0] << 8) | cc[1]);
    out->version      = cc[2];
    out->mle          = (uint16_t)((cc[3] << 8) | cc[4]);
    out->mlc          = (uint16_t)((cc[5] << 8) | cc[6]);
    out->ndef_file_id = (uint16_t)((cc[9] << 8) | cc[10]);
    out->ndef_max     = (uint16_t)((cc[11] << 8) | cc[12]);
    out->read_access  = cc[13];
    out->write_access = cc[14];
    return 0;
}

int m24sr_read_ndef(m24sr_t *d, uint16_t file_id, uint8_t *buf, uint32_t buf_size)
{
    uint8_t nlen_b[2];
    int rc = m24sr_select_file(d, file_id);
    if (rc) return rc;
    rc = m24sr_read_binary(d, 0, nlen_b, 2u);
    if (rc) return rc;
    uint32_t nlen = (uint32_t)((nlen_b[0] << 8) | nlen_b[1]);
    if (nlen > buf_size) return M24SR_ERR_ARG;
    rc = m24sr_read_binary(d, 2u, buf, nlen);
    return rc ? rc : (int)nlen;
}

int m24sr_write_ndef(m24sr_t *d, uint16_t file_id, const uint8_t *msg, uint32_t len)
{
    static const uint8_t zero[2] = { 0, 0 };
    uint8_t nlen_b[2] = { (uint8_t)(len >> 8), (uint8_t)(len & 0xFFu) };
    int rc = m24sr_select_file(d, file_id);
    if (rc) return rc;
    rc = m24sr_update_binary(d, 0, zero, 2u);
    if (rc) return rc;
    rc = m24sr_update_binary(d, 2u, msg, len);
    if (rc) return rc;
    return m24sr_update_binary(d, 0, nlen_b, 2u);
}
