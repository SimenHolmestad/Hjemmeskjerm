/*****************************************************************************
* | File        :   epd_host.h
* | Function    :   hjemmeskjerm: kroken vendret driverkode har inn i vår kode
*
* Denne fila er IKKE fra Waveshare. Den finnes for at driveren skal kunne logge
* og gi opp uten å måtte inkludere noe fra src/.
******************************************************************************/
#ifndef __EPD_HOST_H
#define __EPD_HOST_H

/* Settes av src/main.c. Er den 0, sier driveren ingenting. */
extern int Debug_Enabled;

/* Skriver til stderr og avslutter prosessen med kode 3 (hardware-feil).
 * Definert i src/main.c. Brukes når driveren står fast og ikke har noen
 * fornuftig måte å rapportere feilen oppover på. */
void EPD_Host_Fatal(const char *fmt, ...)
    __attribute__((noreturn, format(printf, 1, 2)));

/* BUSY sjekkes før hver eneste overføring, så timeouten må være romslig nok
 * til at et travelt panel ikke feiler, men kort nok til at en frakoblet HAT
 * ikke henger prosessen for alltid. */
#define EPD_BUSY_TIMEOUT_MS      5000
/* En full INIT-oppdatering på 10,3" tar flere sekunder. */
#define EPD_DISPLAY_TIMEOUT_MS  60000

#endif
