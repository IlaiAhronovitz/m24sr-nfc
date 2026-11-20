/* mock_m24sr.h - host model of the tag, for tests/test_main.c. */
#pragma once
#include <stdint.h>
#include "../src/m24sr.h"

typedef struct {
    m24sr_bus_t bus;
    uint8_t  cc[15], sys[18], ndef[1024];
    uint16_t selected;
    int      session, rf_session, app_selected;
    uint8_t  expect_block;
    uint8_t  resp[300], held[300];
    uint32_t resp_len, held_len;
    /* knobs */
    uint32_t busy_left;      /* NACK this many accesses first */
    int      wtx_next;       /* answer the next I-block with a WTX first */
    /* counters */
    uint32_t crc_errors, frame_errors, block_errors, writes, ms_waited;
} mock_m24sr_t;

void mock_m24sr_init(mock_m24sr_t *m);
