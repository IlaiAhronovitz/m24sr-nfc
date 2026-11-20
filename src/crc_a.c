/* crc_a.c - bitwise CRC_A (ISO/IEC 14443-3 Annex B). */
#include "crc_a.h"

#define CRC_A_PRESET 0x6363U
#define CRC_A_POLY   0x8408U        /* x^16 + x^12 + x^5 + 1, reflected */

uint16_t crc_a(const uint8_t *data, uint32_t len)
{
    uint16_t crc = CRC_A_PRESET;
    for (uint32_t i = 0U; i < len; i++) {
        crc ^= data[i];
        for (uint32_t bit = 0U; bit < 8U; bit++) {
            if ((crc & 1U) != 0U) {
                crc = (uint16_t)(((uint32_t)crc >> 1U) ^ CRC_A_POLY);
            } else {
                crc = (uint16_t)(crc >> 1U);
            }
        }
    }
    return crc;
}
