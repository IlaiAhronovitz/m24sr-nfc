/* mock_m24sr.h - host model of the tag, for tests/test_main.c. */
#ifndef M24SR_NFC_MOCK_M24SR_H
#define M24SR_NFC_MOCK_M24SR_H

#include <stdint.h>
#include "../src/m24sr.h"

#define MOCK_LOG_MAX 16

typedef struct {
    uint32_t off;
    uint32_t len;
    uint8_t  first[2];
} mock_update_t;

typedef struct {
    m24sr_bus_t bus;
    uint8_t  cc[15];
    uint8_t  sys[18];
    uint8_t  ndef[1024];
    uint16_t selected;
    int      session;
    int      rf_session;
    int      app_selected;
    uint8_t  expect_block;
    uint8_t  resp[300];
    uint8_t  held[300];
    uint32_t resp_len;
    uint32_t held_len;
    /* knobs */
    uint32_t busy_left;      /* NACK this many accesses first */
    uint32_t busy_read_left; /* NACK this many reads (writes still pass) */
    int      wtx_next;       /* answer the next I-block with a WTX first */
    uint32_t fail_write_call;/* write call number (1-based) that returns a bus error */
    uint32_t fail_read_call; /* same for reads */
    int      corrupt_next;   /* flip the last byte (CRC) of the next answer read */
    int      flip_pcb_next;  /* answer the next I-block with the other block number */
    int      ok_no_data;     /* answer the next ReadBinary with 90 00 but no data */
    /* counters and logs */
    uint32_t write_calls;
    uint32_t read_calls;
    uint32_t crc_errors;
    uint32_t frame_errors;
    uint32_t block_errors;
    uint32_t writes;
    uint32_t ms_waited;
    uint8_t  last_tx[80];    /* the last frame written (all of it) */
    uint32_t last_tx_len;
    mock_update_t updates[MOCK_LOG_MAX];
    uint32_t n_updates;
} mock_m24sr_t;

void mock_m24sr_init(mock_m24sr_t *m);

#endif /* M24SR_NFC_MOCK_M24SR_H */
