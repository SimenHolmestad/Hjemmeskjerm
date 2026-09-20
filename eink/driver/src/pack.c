#include "pack.h"

/* Nibble-rekkefølgen er den ene tingen her som er lett å ta feil av, så den
 * er verdt å skrive ned. GUI_Paint.c i Waveshare-koden er fasiten – det er
 * den implementasjonen panelet beviselig er fornøyd med. Fra 4bpp-grenen i
 * Paint_SetPixel:
 *
 *     Addr = X * 4 / 8 + Y * WidthByte;                    // byte X/2
 *     Image[Addr] |= (Color & 0xF0) >> (7 - (X*4+3) % 8);
 *
 * Regner man ut skiftet: X med partall gir (X*4+3)%8 == 3, altså skift 4,
 * og gråtonen havner i LAV nibble. X med oddetall gir skift 0, altså HØY
 * nibble. Den venstre piksel i et bytepar ligger med andre ord i de minst
 * signifikante bitene – motsatt av det man gjetter.
 *
 * Polariteten bekreftes av EPD_IT8951_Clear_Refresh, som memsetter bufferet
 * til 0xFF for hvitt. Nibbelen er altså rett og slett gråtonen >> 4.
 */
void pack_4bpp_mirrored(const uint8_t *top_row, int32_t row_step,
                        const uint8_t lut[256],
                        uint16_t w, uint16_t h, uint8_t *dst)
{
    const uint32_t dst_stride = (uint32_t)w / 2u;

    for (uint16_t y = 0; y < h; y++) {
        /* Siste piksel i kilderaden blir panelets første – det er speilingen. */
        const uint8_t *s = top_row + (int32_t)y * row_step + (w - 1u);
        uint8_t *d = dst + (uint32_t)y * dst_stride;

        for (uint32_t i = 0; i < dst_stride; i++) {
            uint8_t lo = (uint8_t)(lut[*s--] >> 4);   /* panel-x = 2i,   partall */
            uint8_t hi = (uint8_t)(lut[*s--] >> 4);   /* panel-x = 2i+1, oddetall */
            d[i] = (uint8_t)((hi << 4) | lo);
        }
    }
}
