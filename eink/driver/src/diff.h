/* Finner hvilke deler av panelet som har endret seg siden forrige ramme. */
#ifndef DIFF_H
#define DIFF_H

#include <stdint.h>

typedef struct { uint16_t x, y, w, h; } rect_t;

/* Rutestørrelse i piksler. IT8951 krever at Area_X og Area_W er delelig med 4
 * i 4bpp – HostAreaPackedPixelWrite_4bp regner (Area_W*4/8)/2 ord per rad – og
 * 16 gir litt margin samtidig som 1872 går opp i det. */
#define DIFF_TILE 16

/* Standardverdi for hvor mange rektangler skjermen deles i. Kan overstyres
 * per kjøring med --max-rects. Er den større enn SAMTIDIGE i main.c, tegnes
 * rektanglene i flere porsjoner, og hver porsjon er ett blink. */
#define DIFF_MAKS_REKT 8

/* Hardt tak. diff_rects skriver aldri flere enn dette uansett hva den blir
 * bedt om, så `ut` trenger aldri være større. */
#define DIFF_TAK 64

/* Dekker rektanglene mer enn dette av skjermen, tegner vi alt. */
#define DIFF_FULL_PROSENT 50

/* Sammenligner to pakkede 4bpp-buffere i panelets koordinater – det pack.c
 * lager – og skriver rektanglene som skiller seg til `ut`.
 *
 * Returnerer antall rektangler, 0 om bildene er like. Aldri mer enn
 * `maks_rekt`, og aldri mer enn DIFF_TAK; blir det flere, slås de nærmeste
 * sammen to og to til det er få nok igjen.
 * Dekker resultatet mer enn `full_prosent` av skjermen, returneres i stedet
 * ett rektangel som dekker hele panelet.
 *
 * `w` må gå opp i DIFF_TILE. Gjør den ikke det, returneres ett rektangel
 * som dekker hele panelet.
 */
int diff_rects(const uint8_t *na, const uint8_t *forrige,
               uint16_t w, uint16_t h,
               rect_t *ut, int maks_rekt, unsigned full_prosent);

/* Klipper rektangelet ut av det pakkede bufferet og legger det tett i `ut`:
 * r->h rader à r->w/2 byte. Det er slik HostAreaPackedPixelWrite_4bp leser
 * kilden – den regner seg fram til bredden selv og forventer ingen luft
 * mellom radene. `ut` må ha plass til r->w/2 * r->h byte. */
void diff_slice(const uint8_t *pakket, uint16_t w, const rect_t *r, uint8_t *ut);

#endif
