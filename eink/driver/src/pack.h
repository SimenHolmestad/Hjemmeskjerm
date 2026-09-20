/* 8-bits gråtone -> 4bpp pakket, slik IT8951 vil ha det. */
#ifndef PACK_H
#define PACK_H

#include <stdint.h>

/* Pakker et gråtonebilde til 4 bits per piksel og speiler det horisontalt.
 *
 * top_row   peker på den ØVERSTE raden i bildet.
 * row_step  er antall byte fra en rad til den neste nedover. Den er negativ
 *           for bunn-opp-BMP-er, som er det Pillow skriver.
 * lut       oversetter paletteindeks til gråtone (identitet for et vanlig
 *           gråtonebilde, men BMP-paletten leses uansett).
 * dst       må være w/2 * h byte og trenger ikke være nullstilt.
 *
 * Speilingen tilsvarer det demoens Epd_Mode(1) gjorde med
 * Paint_SetMirroring(MIRROR_HORIZONTAL) for 10,3"-panelet.
 * Rotasjon gjøres i Python; her forventes bildet allerede i panelets
 * liggende format.
 */
void pack_4bpp_mirrored(const uint8_t *top_row, int32_t row_step,
                        const uint8_t lut[256],
                        uint16_t w, uint16_t h, uint8_t *dst);

#endif
