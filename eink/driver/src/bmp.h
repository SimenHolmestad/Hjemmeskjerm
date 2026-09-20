/* Minimal, streng BMP-leser.
 *
 * Vi eier begge ender av kjeden: render.py lager bildet med Pillow, og den
 * skriver 8-bits palett-BMP, bunn-opp, ukomprimert. Derfor godtar leseren
 * bare det, og sier tydelig fra om noe annet dukker opp. Alternativet –
 * Waveshare sin GUI_BMPfile.c – kan lese seks bitdybder, men gjør det via
 * ett funksjonskall per piksel og lekker minne underveis.
 */
#ifndef BMP_H
#define BMP_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t       *buf;       /* hele fila, eid av strukturen */
    const uint8_t *top_row;   /* øverste rad i bildet */
    int32_t        row_step;  /* byte til neste rad ned; negativ ved bunn-opp */
    uint16_t       w, h;
    uint8_t        lut[256];  /* paletteindeks -> gråtone */
} bmp_image_t;

/* 0 ved suksess. Ved feil: -1, og err fylles med en forklaring. */
int  bmp_load(const char *path, bmp_image_t *img, char *err, size_t errlen);
void bmp_free(bmp_image_t *img);

#endif
