#ifndef COMMON_H
#define COMMON_H

/* Avslutningskoder. render.py skiller på dem for å vite om det er verdt
 * å prøve igjen neste runde. */
#define EXIT_OK        0
#define EXIT_USAGE     1
#define EXIT_INPUT     2   /* BMP-fila er feil eller mangler */
#define EXIT_HARDWARE  3   /* SPI/GPIO/panel, inkludert tidsavbrudd */
#define EXIT_CONFIG    4   /* f.eks. ugyldig EPAPER_VCOM */
#define EXIT_SIGNAL  130   /* avbrutt med Ctrl-C eller SIGTERM */

#ifndef EPAPER_VCOM_DEFAULT_MV
#define EPAPER_VCOM_DEFAULT_MV 1140   /* -1,14 V, fra flexkabelen på vårt panel */
#endif

#endif
