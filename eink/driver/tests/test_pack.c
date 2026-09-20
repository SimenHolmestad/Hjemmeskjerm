/* Verter-test av pakkelogikken. Krever ingen skjerm og kjører fint på
 * utviklingsmaskina: `make test` i eink/driver. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pack.h"

static int feil = 0;

static void sjekk(int ok, const char *hva)
{
    printf("%-58s %s\n", hva, ok ? "ok" : "FEIL");
    if (!ok) feil++;
}

static void identitets_lut(uint8_t lut[256])
{
    for (int i = 0; i < 256; i++) lut[i] = (uint8_t)i;
}

/* Uavhengig referanse: skrevet med en annen løkkestruktur enn pack.c, slik at
 * en feil i den ene ikke gjentas i den andre. Går forover gjennom kilderaden
 * og bakover gjennom målraden. */
static void referanse(const uint8_t *top_row, int32_t row_step,
                      const uint8_t lut[256],
                      uint16_t w, uint16_t h, uint8_t *dst)
{
    const uint32_t dst_stride = (uint32_t)w / 2u;
    for (uint16_t y = 0; y < h; y++) {
        const uint8_t *s = top_row + (int32_t)y * row_step;
        uint8_t *d = dst + (uint32_t)y * dst_stride;
        for (uint32_t k = 0; k < dst_stride; k++) {
            uint8_t a = (uint8_t)(lut[s[2 * k]] >> 4);       /* bildets x = 2k   */
            uint8_t b = (uint8_t)(lut[s[2 * k + 1]] >> 4);   /* bildets x = 2k+1 */
            d[dst_stride - 1 - k] = (uint8_t)((a << 4) | b);
        }
    }
}

int main(void)
{
    uint8_t lut[256];
    identitets_lut(lut);

    /* --- 1. Liten håndregnet case ------------------------------------- */
    {
        /* 4x2, bunn-opp (slik Pillow skriver). Nederste rad først i minnet. */
        uint8_t bmp[2][4] = {
            { 0x00, 0x10, 0x20, 0x30 },   /* dette er bildets NEDERSTE rad */
            { 0x40, 0x50, 0x60, 0x70 },   /* dette er bildets ØVERSTE rad  */
        };
        const uint8_t *top = &bmp[1][0];
        int32_t step = -4;

        uint8_t ut[2 * 2];
        pack_4bpp_mirrored(top, step, lut, 4, 2, ut);

        /* Øverste panelrad kommer fra { 40 50 60 70 }, speilet til
         * { 70 60 50 40 }. Panel-x 0 = 0x7 -> lav nibble av byte 0.
         * Panel-x 1 = 0x6 -> høy nibble av byte 0.  => 0x67
         * Panel-x 2 = 0x5 -> lav nibble av byte 1.
         * Panel-x 3 = 0x4 -> høy nibble av byte 1.  => 0x45 */
        sjekk(ut[0] == 0x67, "handregnet: ovre rad, byte 0 == 0x67");
        sjekk(ut[1] == 0x45, "handregnet: ovre rad, byte 1 == 0x45");
        /* Nederste panelrad fra { 00 10 20 30 } speilet: 3,2,1,0 */
        sjekk(ut[2] == 0x23, "handregnet: nedre rad, byte 0 == 0x23");
        sjekk(ut[3] == 0x01, "handregnet: nedre rad, byte 1 == 0x01");
    }

    /* --- 2. Mot uavhengig referanse, i full panelstørrelse ------------- */
    {
        const uint16_t w = 1872, h = 1404;
        const uint32_t stride = w;
        uint8_t *bmp = malloc((size_t)stride * h);
        uint8_t *a = malloc((size_t)w / 2 * h);
        uint8_t *b = malloc((size_t)w / 2 * h);
        if (!bmp || !a || !b) { printf("tom for minne\n"); return 1; }

        unsigned seed = 12345;
        for (size_t i = 0; i < (size_t)stride * h; i++) {
            seed = seed * 1103515245u + 12345u;
            bmp[i] = (uint8_t)(seed >> 16);
        }

        const uint8_t *top = bmp + (size_t)(h - 1) * stride;
        pack_4bpp_mirrored(top, -(int32_t)stride, lut, w, h, a);
        referanse(top, -(int32_t)stride, lut, w, h, b);
        sjekk(memcmp(a, b, (size_t)w / 2 * h) == 0,
              "full 1872x1404 stemmer med uavhengig referanse");

        free(bmp); free(a); free(b);
    }

    /* --- 3. Hvitt bilde skal gi 0xFF, som Clear_Refresh bruker --------- */
    {
        uint8_t rad[8];
        memset(rad, 0xFF, sizeof rad);
        uint8_t ut[4];
        pack_4bpp_mirrored(rad, 8, lut, 8, 1, ut);
        int alle = 1;
        for (int i = 0; i < 4; i++) if (ut[i] != 0xFF) alle = 0;
        sjekk(alle, "hvitt (0xFF) pakkes til 0xFF, som i Clear_Refresh");

        memset(rad, 0x00, sizeof rad);
        pack_4bpp_mirrored(rad, 8, lut, 8, 1, ut);
        alle = 1;
        for (int i = 0; i < 4; i++) if (ut[i] != 0x00) alle = 0;
        sjekk(alle, "svart (0x00) pakkes til 0x00");
    }

    /* --- 4. Én svart pikselkolonne helt til venstre i bildet ----------- *
     * Dette er testen som faktisk skiller riktig fra feil nibble-rekkefølge.
     * Bildets x=0 skal etter speilingen havne på panelets siste piksel,
     * altså i HØY nibble av siste byte. Er nibblene byttet om, havner den
     * i lav nibble i stedet – en forskyvning på én piksel, som er nesten
     * usynlig på et foto, men entydig her. */
    {
        const uint16_t w = 16;
        uint8_t rad[16];
        memset(rad, 0xFF, sizeof rad);
        rad[0] = 0x00;                       /* svart kolonne i bildets x=0 */

        uint8_t ut[8];
        pack_4bpp_mirrored(rad, w, lut, w, 1, ut);

        sjekk(ut[7] == 0x0F, "bildets x=0 havner i HOY nibble av siste byte");
        for (int i = 0; i < 7; i++) {
            if (ut[i] != 0xFF) { sjekk(0, "resten av raden skal vaere hvit"); break; }
        }
    }

    printf("\n%s\n", feil ? "TESTER FEILET" : "alle tester ok");
    return feil ? 1 : 0;
}
