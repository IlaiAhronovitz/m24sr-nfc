/* i2c.h - polled I2C2 master on PB10 (SCL) / PB11 (SDA), 100 kHz, from the
 * 8 MHz HSI. Returns 0, I2C_NACK if the address was not acknowledged (the
 * M24SR does that while busy), or a negative error. */
#pragma once
#include <stdint.h>

#define I2C_NACK      (-1)
#define I2C_ERR_BUS   (-2)
#define I2C_ERR_TIME  (-3)

/* Frees a stuck bus (9 clocks + STOP by hand), then starts the peripheral.
 * Returns the number of SCL pulses it needed (0 if the bus was idle). */
int i2c2_init(void);
int i2c2_write(uint8_t addr7, const uint8_t *buf, uint32_t len);
int i2c2_read(uint8_t addr7, uint8_t *buf, uint32_t len);

/* Counters for the results block. */
extern uint32_t g_i2c_errors, g_i2c_nacks;
