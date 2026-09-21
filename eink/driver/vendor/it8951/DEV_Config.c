/*****************************************************************************
* | File      	:   DEV_Config.c
* | Author      :   Waveshare team
* | Function    :   Hardware underlying interface
* | Info        :
*----------------
* |	This version:   V3.0
* | Date        :   2019-09-17
* | Info        :   
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documnetation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of theex Software, and to permit persons to  whom the Software is
# furished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#
******************************************************************************/
#include "DEV_Config.h"
#include <fcntl.h>

/* hjemmeskjerm: settes av Makefile. bcm2835 krever en toerpotens. */
#ifndef EPAPER_SPI_DIVIDER
#define EPAPER_SPI_DIVIDER 16
#endif
_Static_assert(EPAPER_SPI_DIVIDER >= 2 && EPAPER_SPI_DIVIDER <= 65536,
               "EPAPER_SPI_DIVIDER maa vaere mellom 2 og 65536");
_Static_assert((EPAPER_SPI_DIVIDER & (EPAPER_SPI_DIVIDER - 1)) == 0,
               "EPAPER_SPI_DIVIDER maa vaere en toerpotens");


/******************************************************************************
function:	GPIO Write
parameter:
Info:
******************************************************************************/
void DEV_Digital_Write(UWORD Pin, UBYTE Value)
{
	bcm2835_gpio_write(Pin, Value);
}

/******************************************************************************
function:	GPIO Read
parameter:
Info:
******************************************************************************/
UBYTE DEV_Digital_Read(UWORD Pin)
{
	UBYTE Read_Value = 0;
	Read_Value = bcm2835_gpio_lev(Pin);
	return Read_Value;
}

/******************************************************************************
function:	SPI Write
parameter:
Info:
******************************************************************************/
void DEV_SPI_WriteByte(UBYTE Value)
{
	bcm2835_spi_transfer(Value);
}

/* hjemmeskjerm: ny. bcm2835_spi_transfer setter TA og venter paa DONE per
 * byte; writenb holder FIFO-en foret gjennom hele blokka. Ren skrivevei. */
void DEV_SPI_WriteBytes(const UBYTE *Buf, UDOUBLE Len)
{
	/* const-cast: signaturen ble const char* i libbcm2835 1.60. */
	bcm2835_spi_writenb((char *)Buf, (uint32_t)Len);
}

/******************************************************************************
function:	SPI Read
parameter:
Info:
******************************************************************************/
UBYTE DEV_SPI_ReadByte()
{
	UBYTE Read_Value = 0x00;
	Read_Value = bcm2835_spi_transfer(0x00);
	return Read_Value;
}

/******************************************************************************
function:	Time delay for ms
parameter:
Info:
******************************************************************************/
void DEV_Delay_ms(UDOUBLE xms)
{
	bcm2835_delay(xms);
}


/******************************************************************************
function:	Time delay for us
parameter:
Info:
******************************************************************************/
void DEV_Delay_us(UDOUBLE xus)
{
	bcm2835_delayMicroseconds(xus);
}


/**
 * GPIO Mode
**/
static int DEV_GPIO_Mode(UWORD Pin, UWORD Mode)   /* hjemmeskjerm: var void */
{
	if(Mode == 0 || Mode == BCM2835_GPIO_FSEL_INPT) {
		bcm2835_gpio_fsel(Pin, BCM2835_GPIO_FSEL_INPT);
	} else {
		bcm2835_gpio_fsel(Pin, BCM2835_GPIO_FSEL_OUTP);
	}
	return 0;   /* hjemmeskjerm */
}


/**
 * GPIO Init
**/
/* hjemmeskjerm: var `static void`, og slukte feil fra DEV_GPIO_Mode. */
static int DEV_GPIO_Init(void)
{
	if (DEV_GPIO_Mode(EPD_RST_PIN, BCM2835_GPIO_FSEL_OUTP) != 0) return -1;
	if (DEV_GPIO_Mode(EPD_CS_PIN, BCM2835_GPIO_FSEL_OUTP) != 0) return -1;
	if (DEV_GPIO_Mode(EPD_BUSY_PIN, BCM2835_GPIO_FSEL_INPT) != 0) return -1;

	DEV_Digital_Write(EPD_CS_PIN, HIGH);

	return 0;
}



/******************************************************************************
function:	Module Initialize, the library and initialize the pins, SPI protocol
parameter:
Info:
******************************************************************************/
UBYTE DEV_Module_Init(void)
{
    Debug("/***********************************/ \r\n");

	/* hjemmeskjerm: bcm2835 trenger root for SPI. Uten root faller
	 * bcm2835_init() tilbake til /dev/gpiomem og returnerer SUKSESS, men
	 * lar SPI-registerpekeren staa som NULL - og da segfaulter
	 * bcm2835_spi_begin() rett nedenfor. Sjekken her gjor at man far vite
	 * hva som er galt i stedet for et kraesj. */
	if (geteuid() != 0) {
		fprintf(stderr,
			"epaper: dette programmet krever root, fordi bcm2835 trenger\n"
			"        /dev/mem for SPI. Kjor med sudo.\n");
		return 1;
	}
	if(!bcm2835_init()) {
		Debug("bcm2835 init failed  !!! \r\n");
		return 1;
	} else {
		Debug("bcm2835 init success !!! \r\n");
	}

	bcm2835_spi_begin();                                         //Start spi interface, set spi pin for the reuse function
	bcm2835_spi_setBitOrder(BCM2835_SPI_BIT_ORDER_MSBFIRST);     //High first transmission
	bcm2835_spi_setDataMode(BCM2835_SPI_MODE0);                  //spi mode 0
	/* hjemmeskjerm: `make SPI_DIVIDER=...`. Deler core clock, som er
	 * brett-avhengig, saa verdien hoerer til brettet - se README. */
	bcm2835_spi_setClockDivider(EPAPER_SPI_DIVIDER);
	Debug("SPI clock divider: %d\r\n", EPAPER_SPI_DIVIDER);
	/* SPI clock reference link：*/
	/*http://www.airspayce.com/mikem/bcm2835/group__constants.html#gaf2e0ca069b8caef24602a02e8a00884e*/

    //GPIO Config
	if (DEV_GPIO_Init() != 0) {
		return -1;
	}
/* hjemmeskjerm: dod #elif USE_WIRINGPI_LIB-gren fjernet. Den ble aldri
 * kompilert (USE_WIRINGPI_LIB defineres ikke av noen Makefile) og refererte
 * wiringPi-symboler vi ikke lenker mot. */

    Debug("/***********************************/ \r\n");
	return 0;
}



/******************************************************************************
function:	Module exits, closes SPI and BCM2835 library
parameter:
Info:
******************************************************************************/
void DEV_Module_Exit(void)
{
	DEV_Digital_Write(EPD_CS_PIN, LOW);
	DEV_Digital_Write(EPD_RST_PIN, LOW);

	bcm2835_spi_end();
	bcm2835_close();
}
