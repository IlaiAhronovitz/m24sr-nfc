/* i2c.c - STM32F1 I2C master, polled, following the reception sequences of
 * RM0008 section 26.3.3 for N = 1, N = 2 and N > 2. The F1 block needs them:
 * ACK and STOP must be programmed at exact points relative to ADDR and BTF,
 * and an interrupt between "clear ADDR" and "set STOP" makes it read one byte
 * too many (the device errata sheet: "some software events must be managed
 * before the current byte is being transferred"). Those windows run with
 * IRQs off. */
#include "regs.h"
#include "system.h"
#include "i2c.h"

#define T_MS     10U
#define SCL_PIN  10U                                 /* PB10 */
#define SDA_PIN  11U                                 /* PB11 */

/* Every peripheral access below goes through the fixed-address register
 * blocks of regs.h: docs/deviations/target.md, record T-1. */
/* NOLINTBEGIN(clang-analyzer-core.FixedAddressDereference) */

uint32_t g_i2c_errors;
uint32_t g_i2c_nacks;

static void pin_mode(gpio_t *g, unsigned pin, unsigned mode)
{
    volatile uint32_t *cr = (pin < 8U) ? &g->CRL : &g->CRH;
    unsigned shift = (pin % 8U) * 4U;
    *cr = (*cr & ~(0xFU << shift)) | (mode << shift);
}

/* Clock SCL until the slave lets SDA go, then make a STOP by hand. */
static int bus_recover(void)
{
    int pulses = 0;
    GPIOB->BSRR = BIT32(SCL_PIN) | BIT32(SDA_PIN);   /* both released (high) */
    pin_mode(GPIOB, SCL_PIN, GPIO_OUT_OD);
    pin_mode(GPIOB, SDA_PIN, GPIO_OUT_OD);
    delay_us(10U);
    while (((GPIOB->IDR & BIT32(SDA_PIN)) == 0U) && (pulses < 9)) {
        GPIOB->BRR = BIT32(SCL_PIN);
        delay_us(5U);
        GPIOB->BSRR = BIT32(SCL_PIN);
        delay_us(5U);
        pulses++;
    }
    GPIOB->BRR = BIT32(SDA_PIN);                     /* STOP: SDA low -> high, SCL high */
    delay_us(5U);
    GPIOB->BSRR = BIT32(SDA_PIN);
    delay_us(5U);
    return pulses;
}

int i2c2_init(void)
{
    RCC_APB2ENR |= APB2ENR_IOPBEN;
    RCC_APB1ENR |= APB1ENR_I2C2EN;
    (void)RCC_APB1ENR;

    int pulses = bus_recover();
    pin_mode(GPIOB, SCL_PIN, GPIO_AF_OD);
    pin_mode(GPIOB, SDA_PIN, GPIO_AF_OD);

    RCC_APB1RSTR |= APB1RSTR_I2C2RST;                /* clean peripheral state */
    RCC_APB1RSTR &= ~APB1RSTR_I2C2RST;
    I2C2->CR1 = I2C_CR1_SWRST;
    I2C2->CR1 = 0U;
    I2C2->CR2 = 8U;                                  /* PCLK1 = 8 MHz */
    I2C2->CCR = 40U;                                 /* Sm: 8 MHz / (2 x 40) = 100 kHz */
    I2C2->TRISE = 9U;                                /* 1000 ns x 8 MHz + 1 */
    I2C2->CR1 = I2C_CR1_PE;
    return pulses;
}

static int wait_sr1(uint32_t mask)
{
    uint32_t t0 = ms_now();
    for (;;) {
        uint32_t sr1 = I2C2->SR1;
        if ((sr1 & mask) != 0U) {
            return 0;
        }
        if ((sr1 & (I2C_SR1_BERR | I2C_SR1_ARLO)) != 0U) {
            return I2C_ERR_BUS;
        }
        if ((sr1 & I2C_SR1_AF) != 0U) {
            return I2C_NACK;
        }
        if ((ms_now() - t0) > T_MS) {
            return I2C_ERR_TIME;
        }
    }
}

static int fail(int rc)
{
    I2C2->SR1 = 0U;                                  /* clear AF, BERR, ARLO */
    I2C2->CR1 |= I2C_CR1_STOP;
    uint32_t t0 = ms_now();
    while (((I2C2->CR1 & I2C_CR1_STOP) != 0U) && ((ms_now() - t0) <= T_MS)) {}
    I2C2->CR1 &= ~(I2C_CR1_POS | I2C_CR1_ACK);
    if (rc == I2C_NACK) {
        g_i2c_nacks++;
    } else {
        g_i2c_errors++;
    }
    if (rc != I2C_NACK) {
        (void)i2c2_init();                           /* bus errors: start over */
    }
    return rc;
}

static int start_addr(uint8_t addr7, int read)
{
    uint32_t t0 = ms_now();
    while ((I2C2->SR2 & I2C_SR2_BUSY) != 0U) {
        if ((ms_now() - t0) > T_MS) {
            return I2C_ERR_TIME;
        }
    }
    I2C2->CR1 |= I2C_CR1_START;
    int rc = wait_sr1(I2C_SR1_SB);
    if (rc != 0) {
        return rc;
    }
    I2C2->DR = ((uint32_t)addr7 << 1U) | ((read != 0) ? 1U : 0U);
    return wait_sr1(I2C_SR1_ADDR);
}

static void stop_wait(void)
{
    uint32_t t0 = ms_now();
    while (((I2C2->CR1 & I2C_CR1_STOP) != 0U) && ((ms_now() - t0) <= T_MS)) {}
}

int i2c2_write(uint8_t addr7, const uint8_t *buf, uint32_t len)
{
    int rc = start_addr(addr7, 0);
    if (rc != 0) {
        return fail(rc);
    }
    (void)I2C2->SR1;
    (void)I2C2->SR2;                                 /* clears ADDR */
    for (uint32_t i = 0U; i < len; i++) {
        rc = wait_sr1(I2C_SR1_TXE);
        if (rc != 0) {
            return fail(rc);
        }
        I2C2->DR = buf[i];
    }
    rc = wait_sr1(I2C_SR1_BTF);
    if (rc != 0) {
        return fail(rc);
    }
    I2C2->CR1 |= I2C_CR1_STOP;
    stop_wait();
    return 0;
}

/* RM0008 26.3.3, N = 1. */
static int read_one(uint8_t addr7, uint8_t *buf)
{
    int rc = start_addr(addr7, 1);
    if (rc != 0) {
        return fail(rc);
    }
    I2C2->CR1 &= ~I2C_CR1_ACK;
    irq_off();
    (void)I2C2->SR1;
    (void)I2C2->SR2;
    I2C2->CR1 |= I2C_CR1_STOP;
    irq_on();
    rc = wait_sr1(I2C_SR1_RXNE);
    if (rc != 0) {
        return fail(rc);
    }
    buf[0] = (uint8_t)I2C2->DR;
    stop_wait();
    return 0;
}

/* RM0008 26.3.3, N = 2 (POS set). */
static int read_two(uint8_t addr7, uint8_t *buf)
{
    I2C2->CR1 |= I2C_CR1_ACK | I2C_CR1_POS;
    int rc = start_addr(addr7, 1);
    if (rc != 0) {
        return fail(rc);
    }
    irq_off();
    (void)I2C2->SR1;
    (void)I2C2->SR2;
    I2C2->CR1 &= ~I2C_CR1_ACK;
    irq_on();
    rc = wait_sr1(I2C_SR1_BTF);
    if (rc != 0) {
        return fail(rc);
    }
    irq_off();
    I2C2->CR1 |= I2C_CR1_STOP;
    buf[0] = (uint8_t)I2C2->DR;
    irq_on();
    buf[1] = (uint8_t)I2C2->DR;
    stop_wait();
    I2C2->CR1 &= ~I2C_CR1_POS;
    return 0;
}

/* RM0008 26.3.3, N > 2: the last three bytes are taken around BTF. */
static int read_many(uint8_t addr7, uint8_t *buf, uint32_t len)
{
    I2C2->CR1 |= I2C_CR1_ACK;
    int rc = start_addr(addr7, 1);
    if (rc != 0) {
        return fail(rc);
    }
    (void)I2C2->SR1;
    (void)I2C2->SR2;
    uint32_t i = 0U;
    while ((len - i) > 3U) {
        rc = wait_sr1(I2C_SR1_RXNE);
        if (rc != 0) {
            return fail(rc);
        }
        buf[i] = (uint8_t)I2C2->DR;
        i++;
    }
    rc = wait_sr1(I2C_SR1_BTF);                      /* N-2 in DR, N-1 in shift */
    if (rc != 0) {
        return fail(rc);
    }
    I2C2->CR1 &= ~I2C_CR1_ACK;
    irq_off();
    buf[i] = (uint8_t)I2C2->DR;                      /* N-2 */
    irq_on();
    i++;
    rc = wait_sr1(I2C_SR1_BTF);                      /* N-1 in DR, N in shift */
    if (rc != 0) {
        return fail(rc);
    }
    irq_off();
    I2C2->CR1 |= I2C_CR1_STOP;
    buf[i] = (uint8_t)I2C2->DR;                      /* N-1 */
    irq_on();
    i++;
    rc = wait_sr1(I2C_SR1_RXNE);
    if (rc != 0) {
        return fail(rc);
    }
    buf[i] = (uint8_t)I2C2->DR;                      /* N */
    stop_wait();
    return 0;
}

int i2c2_read(uint8_t addr7, uint8_t *buf, uint32_t len)
{
    int rc;
    if (len == 0U) {
        rc = 0;
    } else if (len == 1U) {
        rc = read_one(addr7, buf);
    } else if (len == 2U) {
        rc = read_two(addr7, buf);
    } else {
        rc = read_many(addr7, buf, len);
    }
    return rc;
}

/* NOLINTEND(clang-analyzer-core.FixedAddressDereference) */
