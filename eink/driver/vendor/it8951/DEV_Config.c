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

#if LGPIO
int GPIO_Handle;
int SPI_Handle;
#endif

/******************************************************************************
function:	GPIO Write
parameter:
Info:
******************************************************************************/
void DEV_Digital_Write(UWORD Pin, UBYTE Value)
{
#ifdef BCM
	bcm2835_gpio_write(Pin, Value);
#elif  LGPIO  
    lgGpioWrite(GPIO_Handle, Pin, Value);
#elif GPIOD
    GPIOD_Write(Pin, Value);
#endif
}

/******************************************************************************
function:	GPIO Read
parameter:
Info:
******************************************************************************/
UBYTE DEV_Digital_Read(UWORD Pin)
{
	UBYTE Read_Value = 0;
#ifdef BCM
	Read_Value = bcm2835_gpio_lev(Pin);
#elif  LGPIO  
    Read_Value = lgGpioRead(GPIO_Handle,Pin);
#elif GPIOD
    Read_Value = GPIOD_Read(Pin);
#endif
	return Read_Value;
}

/******************************************************************************
function:	SPI Write
parameter:
Info:
******************************************************************************/
void DEV_SPI_WriteByte(UBYTE Value)
{
#ifdef BCM
	bcm2835_spi_transfer(Value);
#elif  LGPIO 
    lgSpiWrite(SPI_Handle,(char*)&Value, 1);
#elif GPIOD
	DEV_HARDWARE_SPI_TransferByte(Value);
#endif
}

/******************************************************************************
function:	SPI Read
parameter:
Info:
******************************************************************************/
UBYTE DEV_SPI_ReadByte()
{
	UBYTE Read_Value = 0x00;
#ifdef BCM
	Read_Value = bcm2835_spi_transfer(0x00);
#elif  LGPIO 
    lgSpiRead(SPI_Handle, (char*)&Read_Value, 1);
#elif GPIOD
	Read_Value = DEV_HARDWARE_SPI_TransferByte(0x00);
#endif
	return Read_Value;
}

/******************************************************************************
function:	Time delay for ms
parameter:
Info:
******************************************************************************/
void DEV_Delay_ms(UDOUBLE xms)
{
#ifdef BCM
	bcm2835_delay(xms);
#elif  LGPIO  
    lguSleep(xms/1000.0);
#elif GPIOD
	UDOUBLE i;
	for(i=0; i < xms; i++) {
		usleep(1000);
	}
#endif
}


/******************************************************************************
function:	Time delay for us
parameter:
Info:
******************************************************************************/
void DEV_Delay_us(UDOUBLE xus)
{
#ifdef BCM
	bcm2835_delayMicroseconds(xus);
#elif  LGPIO 
    lguSleep(xus/1000000.0);
#elif GPIOD
	usleep(xus);
#endif
}


/**
 * GPIO Mode
**/
static int DEV_GPIO_Mode(UWORD Pin, UWORD Mode)   /* hjemmeskjerm: var void */
{
#ifdef BCM
	if(Mode == 0 || Mode == BCM2835_GPIO_FSEL_INPT) {
		bcm2835_gpio_fsel(Pin, BCM2835_GPIO_FSEL_INPT);
	} else {
		bcm2835_gpio_fsel(Pin, BCM2835_GPIO_FSEL_OUTP);
	}
#elif  LGPIO  
    /* hjemmeskjerm: returverdiene var usjekket. Kjernen kan allerede eie
     * GPIO 8 (CS) via cs-gpios i device tree; da feiler claim, CS-skrivingene
     * blir no-ops, og panelet far soppel uten at noe sier fra. Se README om
     * dtoverlay=spi0-0cs. */
    int lg_ret;
    if(Mode == 0 || Mode == LG_SET_INPUT){
        lg_ret = lgGpioClaimInput(GPIO_Handle,LFLAGS,Pin);
    }else{
        lg_ret = lgGpioClaimOutput(GPIO_Handle, LFLAGS, Pin, LG_LOW);
    }
    if (lg_ret < 0) {
        Debug("lgGpioClaim feilet for pinne %d: %d\n", Pin, lg_ret);
        return -1;
    }
#elif GPIOD
	if(Mode == 0 || Mode == GPIOD_IN) {
		GPIOD_Direction(Pin, GPIOD_IN);
		// Debug("IN Pin = %d\r\n",Pin);
	} else {
		GPIOD_Direction(Pin, GPIOD_OUT);
		// Debug("OUT Pin = %d\r\n",Pin);
	}
#endif
	return 0;   /* hjemmeskjerm */
}


/**
 * GPIO Init
**/
/* hjemmeskjerm: var `static void`, og slukte feil fra DEV_GPIO_Mode. */
static int DEV_GPIO_Init(void)
{
#ifdef BCM
	if (DEV_GPIO_Mode(EPD_RST_PIN, BCM2835_GPIO_FSEL_OUTP) != 0) return -1;
	if (DEV_GPIO_Mode(EPD_CS_PIN, BCM2835_GPIO_FSEL_OUTP) != 0) return -1;
	if (DEV_GPIO_Mode(EPD_BUSY_PIN, BCM2835_GPIO_FSEL_INPT) != 0) return -1;

	DEV_Digital_Write(EPD_CS_PIN, HIGH);

#elif LGPIO
	if (DEV_GPIO_Mode(EPD_BUSY_PIN, 0) != 0) return -1;
	if (DEV_GPIO_Mode(EPD_RST_PIN, 1) != 0) return -1;
	if (DEV_GPIO_Mode(EPD_CS_PIN, 1) != 0) return -1;

    DEV_Digital_Write(EPD_CS_PIN, 1);

#elif GPIOD
	if (DEV_GPIO_Mode(EPD_BUSY_PIN, 0) != 0) return -1;
	if (DEV_GPIO_Mode(EPD_RST_PIN, 1) != 0) return -1;
	if (DEV_GPIO_Mode(EPD_CS_PIN, 1) != 0) return -1;

    DEV_Digital_Write(EPD_CS_PIN, 1);
#endif
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

#ifdef BCM
	/* hjemmeskjerm: bcm2835 trenger root for SPI. Uten root faller
	 * bcm2835_init() tilbake til /dev/gpiomem og returnerer SUKSESS, men
	 * lar SPI-registerpekeren staa som NULL - og da segfaulter
	 * bcm2835_spi_begin() rett nedenfor. Sjekken her gjor at man far vite
	 * hva som er galt i stedet for et kraesj. */
	if (geteuid() != 0) {
		fprintf(stderr,
			"epaper: bygget med LIB=BCM, som krever root. Kjor med sudo,\n"
			"        eller bygg med LGPIO (make clean && make) og legg\n"
			"        brukeren i gruppene gpio og spi.\n");
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
	//bcm2835_spi_setClockDivider(BCM2835_SPI_CLOCK_DIVIDER_16);   //For RPi3/3B/3B+
	bcm2835_spi_setClockDivider(BCM2835_SPI_CLOCK_DIVIDER_32);   //For RPi 4
	/* SPI clock reference link：*/
	/*http://www.airspayce.com/mikem/bcm2835/group__constants.html#gaf2e0ca069b8caef24602a02e8a00884e*/

    //GPIO Config
	if (DEV_GPIO_Init() != 0) {
		return -1;
	}
/* hjemmeskjerm: dod #elif USE_WIRINGPI_LIB-gren fjernet. Den ble aldri
 * kompilert (USE_WIRINGPI_LIB defineres ikke av noen Makefile) og refererte
 * wiringPi-symboler vi ikke lenker mot. */
#elif  LGPIO
    char buffer[NUM_MAXBUF];
    FILE *fp;

    fp = popen("cat /proc/cpuinfo | grep 'Raspberry Pi 5'", "r");
    if (fp == NULL) {
        Debug("It is not possible to determine the model of the Raspberry PI\n");
        return -1;
    }

    if(fgets(buffer, sizeof(buffer), fp) != NULL)
    {
        GPIO_Handle = lgGpiochipOpen(4);
        if (GPIO_Handle < 0)
        {
            Debug( "gpiochip4 Export Failed\n");
            return -1;
        }
    }
    else
    {
        GPIO_Handle = lgGpiochipOpen(0);
        if (GPIO_Handle < 0)
        {
            Debug( "gpiochip0 Export Failed\n");
            return -1;
        }
    }
    SPI_Handle = lgSpiOpen(0, 0, 12500000, 0);
    if (SPI_Handle < 0) {   /* hjemmeskjerm: var usjekket */
        Debug("lgSpiOpen(/dev/spidev0.0) feilet: %d\n", SPI_Handle);
        return -1;
    }
    if (DEV_GPIO_Init() != 0) {
        return -1;
    }
#elif GPIOD
	printf("Write and read /dev/spidev0.0 \r\n");
    GPIOD_Export();
	if (DEV_GPIO_Init() != 0) {
		return -1;
	}
	DEV_HARDWARE_SPI_begin("/dev/spidev0.0");
    DEV_HARDWARE_SPI_setSpeed(12500000);
#endif

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
#ifdef BCM
	DEV_Digital_Write(EPD_CS_PIN, LOW);
	DEV_Digital_Write(EPD_RST_PIN, LOW);

	bcm2835_spi_end();
	bcm2835_close();
#elif LGPIO 
    // DEV_Digital_Write(EPD_CS_PIN, 0);
	// DEV_Digital_Write(EPD_RST_PIN, 0);
    // lgSpiClose(SPI_Handle);
    // lgGpiochipClose(GPIO_Handle);
#elif GPIOD
	DEV_HARDWARE_SPI_end();
	DEV_Digital_Write(EPD_CS_PIN, 0);
	DEV_Digital_Write(EPD_RST_PIN, 0);
    GPIOD_Unexport(EPD_RST_PIN);
    GPIOD_Unexport(EPD_BUSY_PIN);
    GPIOD_Unexport_GPIO();
#endif
}
