/* Stubb av lgpio.h, KUN for syntakssjekk på en maskin uten liblgpio
 * (typisk utviklings-Mac-en). Den lenker ingenting og brukes aldri av
 * det ekte bygget - se `make check` i Makefile.
 *
 * Signaturene er hentet fra lgpio sitt offentlige API. Stemmer de ikke
 * lenger, feiler `make check` og ikke det ekte bygget, så det er trygt. */
#ifndef __LGPIO_STUB_H
#define __LGPIO_STUB_H

#define LG_SET_INPUT 0
#define LG_LOW       0
#define LG_HIGH      1

int lgGpiochipOpen(int gpioDev);
int lgGpiochipClose(int handle);
int lgGpioClaimInput(int handle, int lFlags, int gpio);
int lgGpioClaimOutput(int handle, int lFlags, int gpio, int level);
int lgGpioRead(int handle, int gpio);
int lgGpioWrite(int handle, int gpio, int level);
int lgSpiOpen(int spiDev, int spiChan, int spiBaud, int spiFlags);
int lgSpiClose(int handle);
int lgSpiRead(int handle, char *rxBuf, int count);
int lgSpiWrite(int handle, const char *txBuf, int count);
int lgSpiXfer(int handle, const char *txBuf, char *rxBuf, int count);
void lguSleep(double sleepSecs);

#endif
