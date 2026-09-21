/* Stubb av bcm2835.h, KUN for syntakssjekk på en maskin uten libbcm2835.
 * Lenker ingenting; brukes av `make check`. */
#ifndef __BCM2835_STUB_H
#define __BCM2835_STUB_H

#include <stdint.h>

#define HIGH 0x1
#define LOW  0x0
#define BCM2835_GPIO_FSEL_INPT 0
#define BCM2835_GPIO_FSEL_OUTP 1
#define BCM2835_SPI_BIT_ORDER_MSBFIRST 1
#define BCM2835_SPI_MODE0 0
#define BCM2835_SPI_CLOCK_DIVIDER_16 16
#define BCM2835_SPI_CLOCK_DIVIDER_32 32

int  bcm2835_init(void);
int  bcm2835_close(void);
void bcm2835_delay(unsigned int millis);
void bcm2835_delayMicroseconds(uint64_t micros);
void bcm2835_gpio_fsel(uint8_t pin, uint8_t mode);
uint8_t bcm2835_gpio_lev(uint8_t pin);
void bcm2835_gpio_write(uint8_t pin, uint8_t on);
int  bcm2835_spi_begin(void);
void bcm2835_spi_end(void);
void bcm2835_spi_setBitOrder(uint8_t order);
void bcm2835_spi_setDataMode(uint8_t mode);
void bcm2835_spi_setClockDivider(uint16_t divider);
uint8_t bcm2835_spi_transfer(uint8_t value);
void bcm2835_spi_writenb(const char *buf, uint32_t len);

#endif
