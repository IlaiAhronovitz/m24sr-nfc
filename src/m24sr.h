/* m24sr.h - M24SR dynamic NFC tag over I2C (ST datasheet DocID 023790):
 * the I2C session tokens, ISO 14443-4 style framing (I-blocks with a toggling
 * block number, S-blocks for DESELECT and WTX, CRC_A), and the ISO 7816-4
 * commands of the NFC Forum Type 4 Tag (Select, ReadBinary, UpdateBinary).
 *
 * The bus is a small interface so the whole layer runs on the host against a
 * mock tag (tests/), and on the board against the I2C2 driver. */
#pragma once
#include <stdint.h>

#define M24SR_ADDR          0x56u     /* 7-bit; device select 0xAC / 0xAD */
#define M24SR_FILE_CC       0xE103u
#define M24SR_FILE_SYSTEM   0xE101u
#define M24SR_FILE_NDEF     0x0001u   /* default; the CC file has the real one */
#define M24SR_CHUNK         64u       /* bytes per ReadBinary / UpdateBinary */

/* Bus results: 0 ok, M24SR_NACK while the tag is busy (it NACKs its address),
 * anything else is a bus error. */
#define M24SR_NACK          (-1)

typedef struct {
    int  (*write)(void *ctx, const uint8_t *buf, uint32_t len);
    int  (*read)(void *ctx, uint8_t *buf, uint32_t len);
    void (*delay_ms)(void *ctx, uint32_t ms);
    void *ctx;
} m24sr_bus_t;

typedef struct {
    const m24sr_bus_t *bus;
    uint8_t  block;          /* I-block number, 0 or 1 */
    uint16_t last_sw;        /* status word of the last command */
    uint32_t wtx_count;      /* waiting-time extensions answered */
    uint32_t busy_polls;     /* address NACKs while waiting */
} m24sr_t;

/* Error codes (negative). The ISO status word is in d->last_sw. */
#define M24SR_ERR_BUS       (-2)
#define M24SR_ERR_TIMEOUT   (-3)
#define M24SR_ERR_CRC       (-4)
#define M24SR_ERR_FRAME     (-5)
#define M24SR_ERR_SW        (-6)      /* status word other than 90 00 */
#define M24SR_ERR_ARG       (-7)

void m24sr_init(m24sr_t *d, const m24sr_bus_t *bus);

/* I2C session token: GetI2Csession (0x26) fails while an RF session is open;
 * KillRFsession (0x52) takes the token from the phone. */
int m24sr_get_session(m24sr_t *d);
int m24sr_kill_rf_session(m24sr_t *d);
/* S-block DESELECT: ends the session and gives the token back. */
int m24sr_deselect(m24sr_t *d);

int m24sr_select_ndef_app(m24sr_t *d);
int m24sr_select_file(m24sr_t *d, uint16_t file_id);
int m24sr_read_binary(m24sr_t *d, uint16_t offset, uint8_t *buf, uint32_t len);
int m24sr_update_binary(m24sr_t *d, uint16_t offset, const uint8_t *data, uint32_t len);

/* NFC Forum Type 4 Tag capability container (the first 15 bytes of E103). */
typedef struct {
    uint16_t cclen, mle, mlc, ndef_file_id, ndef_max;
    uint8_t  version, read_access, write_access;
} m24sr_cc_t;

int m24sr_parse_cc(const uint8_t *cc, uint32_t len, m24sr_cc_t *out);

/* Whole-message helpers on the NDEF file: NLEN (2 bytes) then the message.
 * read: returns the message length copied into buf. write: NLEN is set to 0
 * first and to the real length last, so a reader never sees a half message. */
int m24sr_read_ndef(m24sr_t *d, uint16_t file_id, uint8_t *buf, uint32_t buf_size);
int m24sr_write_ndef(m24sr_t *d, uint16_t file_id, const uint8_t *msg, uint32_t len);
