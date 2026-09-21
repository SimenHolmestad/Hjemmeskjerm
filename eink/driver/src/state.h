/* Hurtiglager for den ramma panelet sist fikk, pakket 4bpp.
 *
 * Bufferet er egentlig panelets tilstand, så det er epaper som eier det:
 * det skrives først når hele oppdateringen har gått gjennom. Da kan det ikke
 * komme i utakt med det som faktisk står på skjermen.
 */
#ifndef STATE_H
#define STATE_H

#include <stdint.h>

/* Leser forrige ramme inn i buf, som må ha plass til w/2 * h byte.
 * 0 ved treff. Alt annet – ingen fil, feil format, andre mål – er bom, og
 * da skal hele skjermen tegnes på nytt. */
int state_load(uint8_t *buf, uint16_t w, uint16_t h);

/* Skriver buf som den nye forrige ramma. 0 ved suksess. */
int state_store(const uint8_t *buf, uint16_t w, uint16_t h);

/* Kaster hurtiglageret, slik at neste runde tegner alt. */
void state_drop(void);

#endif
