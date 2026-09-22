/* Verts-test av diffen. Krever ingen skjerm og kjører fint på
 * utviklingsmaskina: `make test` i eink/driver. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "diff.h"

#define W 1872
#define H 1404
#define STRIDE (W / 2)

static int feil = 0;

static void sjekk(int ok, const char *hva)
{
    printf("%-58s %s\n", hva, ok ? "ok" : "FEIL");
    if (!ok) feil++;
}

/* Setter en piksel svart. x er en pikselkoordinat i panelets rutenett. */
static void sett_svart(uint8_t *buf, uint16_t x, uint16_t y)
{
    buf[(size_t)y * STRIDE + x / 2] = 0x00;
}

/* IT8951 regner (Area_W*4/8)/2 ord per rad i 4bpp, så både x og w må være
 * delelig med 4. Står det feil her, blir bildet skjevt, ikke borte. */
static int justering_ok(const rect_t *r, int n)
{
    for (int i = 0; i < n; i++) {
        if (r[i].x % 4 || r[i].w % 4) return 0;
        if (r[i].w == 0 || r[i].h == 0) return 0;
        if ((uint32_t)r[i].x + r[i].w > W) return 0;
        if ((uint32_t)r[i].y + r[i].h > H) return 0;
    }
    return 1;
}

/* To rektangler skal aldri overlappe: da ville det samme omraadet blitt tegnet
 * to ganger, og hver oppdatering koster en hel bolgeform. */
static int ingen_overlapp(const rect_t *r, int n)
{
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if ((uint32_t)r[i].x < (uint32_t)r[j].x + r[j].w
             && (uint32_t)r[j].x < (uint32_t)r[i].x + r[i].w
             && (uint32_t)r[i].y < (uint32_t)r[j].y + r[j].h
             && (uint32_t)r[j].y < (uint32_t)r[i].y + r[i].h) return 0;
        }
    }
    return 1;
}

/* Hver byte som faktisk skiller seg skal ligge inne i et av rektanglene.
 * Dette er den egentlige testen: rektanglene kan gjerne være for store,
 * men aldri for små. */
static int alt_dekket(const uint8_t *na, const uint8_t *forrige,
                      const rect_t *r, int n)
{
    for (uint16_t y = 0; y < H; y++) {
        for (uint32_t bx = 0; bx < STRIDE; bx++) {
            size_t i = (size_t)y * STRIDE + bx;
            if (na[i] == forrige[i]) continue;

            int dekket = 0;
            for (int k = 0; k < n && !dekket; k++) {
                dekket = y >= r[k].y && y < (uint32_t)r[k].y + r[k].h
                      && bx >= (uint32_t)r[k].x / 2
                      && bx < (uint32_t)(r[k].x + r[k].w) / 2;
            }
            if (!dekket) return 0;
        }
    }
    return 1;
}

int main(void)
{
    uint8_t *forrige = malloc((size_t)STRIDE * H);
    uint8_t *na      = malloc((size_t)STRIDE * H);
    if (!forrige || !na) { printf("tom for minne\n"); return 1; }

    rect_t r[DIFF_TAK];

    /* --- 1. Like buffere ---------------------------------------------- */
    memset(forrige, 0xFF, (size_t)STRIDE * H);
    memcpy(na, forrige, (size_t)STRIDE * H);
    sjekk(diff_rects(na, forrige, W, H, r, DIFF_MAKS_REKT, DIFF_FULL_PROSENT) == 0,
          "like buffere gir null rektangler");

    /* --- 2. Én piksel ------------------------------------------------- */
    {
        memcpy(na, forrige, (size_t)STRIDE * H);
        sett_svart(na, 700, 300);
        int n = diff_rects(na, forrige, W, H, r, DIFF_MAKS_REKT, DIFF_FULL_PROSENT);

        sjekk(n == 1, "en endret piksel gir ett rektangel");
        sjekk(n == 1 && r[0].x == 688 && r[0].y == 288
                     && r[0].w == DIFF_TILE && r[0].h == DIFF_TILE,
              "rektangelet er ruta pikselen ligger i");
        sjekk(justering_ok(r, n), "rektangelet er justert for 4bpp");
        sjekk(alt_dekket(na, forrige, r, n), "endringen ligger inne i rektangelet");
        sjekk(ingen_overlapp(r, n), "rektanglene overlapper ikke");
    }

    /* --- 3. To endringer langt fra hverandre -------------------------- */
    {
        memcpy(na, forrige, (size_t)STRIDE * H);
        sett_svart(na, 100, 100);
        sett_svart(na, 1700, 1300);
        int n = diff_rects(na, forrige, W, H, r, DIFF_MAKS_REKT, DIFF_FULL_PROSENT);

        sjekk(n == 2, "to endringer langt fra hverandre gir to rektangler");
        sjekk(justering_ok(r, n), "begge rektanglene er justert for 4bpp");
        sjekk(alt_dekket(na, forrige, r, n), "begge endringene er dekket");
        sjekk(ingen_overlapp(r, n), "rektanglene overlapper ikke");
    }

    /* --- 4. Et sammenhengende felt blir ett rektangel ------------------ */
    {
        memcpy(na, forrige, (size_t)STRIDE * H);
        for (uint16_t y = 200; y < 264; y++) {
            memset(na + (size_t)y * STRIDE + 50, 0x00, 40);
        }
        int n = diff_rects(na, forrige, W, H, r, DIFF_MAKS_REKT, DIFF_FULL_PROSENT);

        sjekk(n == 1, "et sammenhengende felt blir ett rektangel");
        sjekk(justering_ok(r, n), "feltet er justert for 4bpp");
        sjekk(alt_dekket(na, forrige, r, n), "hele feltet er dekket");
        sjekk(ingen_overlapp(r, n), "rektanglene overlapper ikke");
    }

    /* --- 5. Over terskelen -> hele skjermen --------------------------- */
    {
        memcpy(na, forrige, (size_t)STRIDE * H);
        memset(na, 0x00, (size_t)STRIDE * (H * 6 / 10));
        int n = diff_rects(na, forrige, W, H, r, DIFF_MAKS_REKT, DIFF_FULL_PROSENT);

        sjekk(n == 1 && r[0].x == 0 && r[0].y == 0 && r[0].w == W && r[0].h == H,
              "mer enn terskelen endret gir ett fullskjermsrektangel");
    }

    /* --- 6. Flere spredte endringer enn taket ------------------------- */
    {
        memcpy(na, forrige, (size_t)STRIDE * H);
        int satt = 0;
        for (int rr = 0; rr < 8; rr += 2) {
            for (int cc = 0; cc < 10; cc += 2) {
                sett_svart(na, (uint16_t)(cc * DIFF_TILE), (uint16_t)(rr * DIFF_TILE));
                satt++;
            }
        }
        sjekk(satt > DIFF_MAKS_REKT, "testen setter flere ruter enn taket");

        int n = diff_rects(na, forrige, W, H, r, DIFF_MAKS_REKT, DIFF_FULL_PROSENT);
        sjekk(n > 0 && n <= DIFF_MAKS_REKT, "antallet holder seg innenfor taket");
        sjekk(justering_ok(r, n), "de sammenslaatte er justert for 4bpp");
        sjekk(alt_dekket(na, forrige, r, n), "alle endringene er dekket");
        sjekk(ingen_overlapp(r, n), "rektanglene overlapper ikke");
    }

    /* --- 7. Endringer i hver sin ende skal ikke sluke skjermen -------- *
     * Dette er tilfellet som gjorde at hele skjermen blinket: naar
     * oppdelingen ga flere rektangler enn taket, ble alt slaatt sammen til
     * den omsluttende boksen - og en endring oppe og en nede gir en boks
     * som dekker alt. Naboer skal slaas sammen, ikke motsatte hjorner. */
    {
        memcpy(na, forrige, (size_t)STRIDE * H);

        /* En tabell oeverst der mange rader endrer seg, som Entur-tavla. */
        for (int rad = 0; rad < 20; rad++) {
            uint16_t y = (uint16_t)(100 + rad * 40);
            for (uint16_t dy = 0; dy < 16; dy++) {
                memset(na + (size_t)(y + dy) * STRIDE + 60, 0x00, 100);
            }
        }
        /* Og ett enkelt tall nederst, langt unna. */
        for (uint16_t y = H - 60; y < H - 20; y++) {
            memset(na + (size_t)y * STRIDE + 800, 0x00, 40);
        }

        int n = diff_rects(na, forrige, W, H, r, DIFF_MAKS_REKT, DIFF_FULL_PROSENT);

        unsigned long areal = 0;
        for (int i = 0; i < n; i++) areal += (unsigned long)r[i].w * r[i].h;
        unsigned long prosent = areal * 100UL / ((unsigned long)W * H);

        char hva[80];
        snprintf(hva, sizeof hva,
                 "tabell oeverst + tall nederst tegner %lu%%, ikke hele skjermen",
                 prosent);
        sjekk(prosent < 25, hva);
        sjekk(n > 1, "de to omraadene holdes fra hverandre");
        sjekk(alt_dekket(na, forrige, r, n), "alle endringene er dekket");
        sjekk(ingen_overlapp(r, n), "rektanglene overlapper ikke");
    }

    /* --- 7. Endring i siste rad og siste kolonne ----------------------- *
     * 1404 gaar ikke opp i 16, saa nederste ruterad er 12 piksler hoy.
     * Rektangelet skal stoppe ved panelkanten, ikke utenfor. */
    {
        memcpy(na, forrige, (size_t)STRIDE * H);
        sett_svart(na, W - 1, H - 1);
        int n = diff_rects(na, forrige, W, H, r, DIFF_MAKS_REKT, DIFF_FULL_PROSENT);

        sjekk(n == 1 && (uint32_t)r[0].x + r[0].w == W
                     && (uint32_t)r[0].y + r[0].h == H,
              "rektangelet i hjornet stopper ved panelkanten");
        sjekk(justering_ok(r, n), "hjornerektangelet er justert for 4bpp");
        sjekk(alt_dekket(na, forrige, r, n), "hjornepikselen er dekket");
        sjekk(ingen_overlapp(r, n), "rektanglene overlapper ikke");
    }

    /* --- 8. Utklippet som sendes til panelet -------------------------- *
     * HostAreaPackedPixelWrite_4bp leser kilden som h rader a w/2 byte, tett
     * etter hverandre. Er indekseringa her feil, blir rektangelet smurt
     * sidelengs paa skjermen - det ser man paa veggen, men ikke i en logg. */
    {
        unsigned seed = 9876;
        for (size_t i = 0; i < (size_t)STRIDE * H; i++) {
            seed = seed * 1103515245u + 12345u;
            na[i] = (uint8_t)(seed >> 16);
        }

        const rect_t r8[] = {
            { 688, 288, 48, 32 },     /* midt paa */
            { 0, 0, W, 16 },          /* hele bredden */
            { W - 16, H - 12, 16, 12 },  /* nederste hoyre hjorne */
        };

        for (size_t k = 0; k < sizeof r8 / sizeof r8[0]; k++) {
            size_t bredde = (size_t)r8[k].w / 2;
            uint8_t *bit = malloc(bredde * r8[k].h);
            if (!bit) { printf("tom for minne\n"); return 1; }

            diff_slice(na, W, &r8[k], bit);

            int likt = 1;
            for (uint16_t y = 0; y < r8[k].h && likt; y++) {
                for (size_t bx = 0; bx < bredde && likt; bx++) {
                    uint8_t ventet = na[(size_t)(r8[k].y + y) * STRIDE
                                        + r8[k].x / 2 + bx];
                    if (bit[(size_t)y * bredde + bx] != ventet) likt = 0;
                }
            }
            char hva[80];
            snprintf(hva, sizeof hva, "utklipp %ux%u @ %u,%u stemmer",
                     r8[k].w, r8[k].h, r8[k].x, r8[k].y);
            sjekk(likt, hva);
            free(bit);
        }
    }

    /* --- 9. Tilfeldige moenstre ---------------------------------------- *
     * Haandlagde tilfeller treffer bare det man har tenkt paa. Her kastes
     * tilfeldige felt utover skjermen, og invariantene skal holde uansett. */
    {
        unsigned seed = 271828;
        int runder = 0, brudd_dekning = 0, brudd_overlapp = 0;
        int brudd_tak = 0, brudd_just = 0;

        for (int forsok = 0; forsok < 300; forsok++) {
            memcpy(na, forrige, (size_t)STRIDE * H);

            seed = seed * 1103515245u + 12345u;
            int felt = (int)((seed >> 16) % 25u) + 1;

            for (int f = 0; f < felt; f++) {
                seed = seed * 1103515245u + 12345u;
                uint16_t x = (uint16_t)((seed >> 16) % (W / 2));
                seed = seed * 1103515245u + 12345u;
                uint16_t y = (uint16_t)((seed >> 16) % (uint32_t)(H - 80));
                seed = seed * 1103515245u + 12345u;
                uint16_t bredde = (uint16_t)((seed >> 16) % 120u) + 1u;
                seed = seed * 1103515245u + 12345u;
                uint16_t hoyde = (uint16_t)((seed >> 16) % 60u) + 1u;

                for (uint16_t dy = 0; dy < hoyde; dy++) {
                    memset(na + (size_t)(y + dy) * STRIDE + x, 0x00, bredde);
                }
            }

            int n = diff_rects(na, forrige, W, H, r, DIFF_MAKS_REKT, DIFF_FULL_PROSENT);
            runder++;
            if (!alt_dekket(na, forrige, r, n))     brudd_dekning++;
            if (!ingen_overlapp(r, n))              brudd_overlapp++;
            if (n < 0 || n > DIFF_MAKS_REKT)        brudd_tak++;
            if (n > 0 && !justering_ok(r, n))       brudd_just++;
        }

        char hva[80];
        snprintf(hva, sizeof hva, "%d tilfeldige runder: alt dekket", runder);
        sjekk(brudd_dekning == 0, hva);
        sjekk(brudd_overlapp == 0, "ingen av dem gir overlappende rektangler");
        sjekk(brudd_tak == 0, "ingen av dem sprenger taket");
        sjekk(brudd_just == 0, "alle er justert for 4bpp");
    }

    /* --- 10. Taket satt hoyere enn standarden ------------------------- *
     * --max-rects kan settes per kjoering, saa diff_rects maa takle et annet
     * tall enn DIFF_MAKS_REKT - og aldri skrive flere enn DIFF_TAK. */
    {
        memcpy(na, forrige, (size_t)STRIDE * H);
        for (int rad = 0; rad < 24; rad++) {
            uint16_t y = (uint16_t)(60 + rad * 52);
            for (uint16_t dy = 0; dy < 16; dy++) {
                memset(na + (size_t)(y + dy) * STRIDE + 40 + rad * 8, 0x00, 24);
            }
        }

        /* Arealet maa regnes ut foer neste kall, som skriver over r. */
        int smal = diff_rects(na, forrige, W, H, r, 4, DIFF_FULL_PROSENT);
        unsigned long a_smal = 0;
        for (int i = 0; i < smal; i++) a_smal += (unsigned long)r[i].w * r[i].h;
        sjekk(smal > 0 && smal <= 4, "--max-rects 4 gir hoyst fire rektangler");
        sjekk(alt_dekket(na, forrige, r, smal), "alt er dekket med fire");
        sjekk(ingen_overlapp(r, smal), "de fire overlapper ikke");

        int vid = diff_rects(na, forrige, W, H, r, 24, DIFF_FULL_PROSENT);
        unsigned long a_vid = 0;
        for (int i = 0; i < vid; i++) a_vid += (unsigned long)r[i].w * r[i].h;
        sjekk(vid > 0 && vid <= 24, "--max-rects 24 gir hoyst tjuefire");
        sjekk(vid > smal, "et hoyere tak gir flere rektangler");
        sjekk(alt_dekket(na, forrige, r, vid), "alt er dekket med tjuefire");
        sjekk(ingen_overlapp(r, vid), "de tjuefire overlapper ikke");
        sjekk(a_vid < a_smal, "og mindre areal som tegnes");

        int over = diff_rects(na, forrige, W, H, r, 1000, DIFF_FULL_PROSENT);
        sjekk(over > 0 && over <= DIFF_TAK, "et tak over DIFF_TAK klippes til DIFF_TAK");
    }

    free(forrige); free(na);
    printf("\n%s\n", feil ? "TESTER FEILET" : "alle tester ok");
    return feil ? 1 : 0;
}
