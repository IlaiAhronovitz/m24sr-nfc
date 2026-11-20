/* crc_a.c - bitwise CRC_A (ISO/IEC 14443-3 Annex B). */
#include "crc_a.h"

uint16_t crc_a(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0x6363u;
    while (len--) {
        crc ^= *data++;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0x8408u) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}
