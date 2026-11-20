/* crc_a.h - CRC_A of ISO/IEC 14443-3 Type A, as the M24SR frames use it.
 * Initial value 0x6363, polynomial x^16 + x^12 + x^5 + 1, reflected (0x8408),
 * no final XOR. Sent least significant byte first. Host-testable. */
#pragma once
#include <stdint.h>

uint16_t crc_a(const uint8_t *data, uint32_t len);
