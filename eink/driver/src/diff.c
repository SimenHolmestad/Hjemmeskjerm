/* Diffen gjøres på det pakkede bufferet, ikke på BMP-en.
 *
 * Det bufferet er allerede i panelets koordinater, allerede speilet og
 * allerede kvantisert til de 16 nivåene panelet faktisk viser. Da slipper vi
 * å regne om koordinater, og siden 1 byte er nøyaktig 2 piksler, blir hele
 * sammenligningen memcmp på bytegrenser.
 */
#include <string.h>

#include "diff.h"

/* main.c avviser paneler over 4096 piksler, så rutenettet har et tak og
 * trenger ingen allokering. */
#define MAKS_SIDE (4096 / DIFF_TILE)

static uint8_t skitten[MAKS_SIDE * MAKS_SIDE];

static int rute_endret(const uint8_t *na, const uint8_t *forrige,
                       uint32_t stride, uint16_t x, uint16_t y,
                       uint16_t rw, uint16_t rh)
{
    uint32_t off = (uint32_t)y * stride + x / 2u;
    for (uint16_t i = 0; i < rh; i++, off += stride) {
        if (memcmp(na + off, forrige + off, rw / 2u) != 0) return 1;
    }
    return 0;
}

static rect_t rekt_av_ruter(int c, int r, int c2, int r2, uint16_t w, uint16_t h)
{
    uint32_t x  = (uint32_t)c * DIFF_TILE;
    uint32_t y  = (uint32_t)r * DIFF_TILE;
    uint32_t x2 = (uint32_t)(c2 + 1) * DIFF_TILE;
    uint32_t y2 = (uint32_t)(r2 + 1) * DIFF_TILE;
    if (x2 > w) x2 = w;
    if (y2 > h) y2 = h;

    rect_t rekt = { (uint16_t)x, (uint16_t)y,
                    (uint16_t)(x2 - x), (uint16_t)(y2 - y) };
    return rekt;
}

int diff_rects(const uint8_t *na, const uint8_t *forrige,
               uint16_t w, uint16_t h,
               rect_t *ut, int maks_rekt, unsigned full_prosent)
{
    const uint32_t stride = (uint32_t)w / 2u;
    const int kol = (w + DIFF_TILE - 1) / DIFF_TILE;
    const int rad = (h + DIFF_TILE - 1) / DIFF_TILE;

    /* Går ikke bredden opp i rutenettet, kan siste kolonne bli en bredde
     * IT8951 ikke godtar i 4bpp. Da tegner vi heller alt: det er den bredden
     * driveren uansett har brukt hele tiden. */
    const rect_t hele = { 0, 0, w, h };
    if (maks_rekt < 1 || w % DIFF_TILE != 0 || kol > MAKS_SIDE || rad > MAKS_SIDE) {
        ut[0] = hele;
        return 1;
    }

    memset(skitten, 0, (size_t)kol * (size_t)rad);

    /* Den omsluttende boksen føres underveis: den er svaret vi faller tilbake
     * på hvis sammenslåingen under gir flere rektangler enn vi vil ha. */
    int min_c = kol, maks_c = -1, min_r = rad, maks_r = -1;

    for (int r = 0; r < rad; r++) {
        uint16_t y  = (uint16_t)(r * DIFF_TILE);
        uint16_t rh = (uint16_t)((y + DIFF_TILE <= h) ? DIFF_TILE : h - y);
        for (int c = 0; c < kol; c++) {
            uint16_t x  = (uint16_t)(c * DIFF_TILE);
            uint16_t rw = (uint16_t)((x + DIFF_TILE <= w) ? DIFF_TILE : w - x);
            if (!rute_endret(na, forrige, stride, x, y, rw, rh)) continue;

            skitten[(size_t)r * kol + c] = 1;
            if (c < min_c) min_c = c;
            if (c > maks_c) maks_c = c;
            if (r < min_r) min_r = r;
            if (r > maks_r) maks_r = r;
        }
    }

    if (maks_r < 0) return 0;

    /* Grådig sammenslåing: voks til høyre så langt rutene er skitne, så
     * nedover så lenge hele kolonneområdet er skittent. */
    int n = 0;
    for (int r = min_r; r <= maks_r && n < maks_rekt; r++) {
        for (int c = min_c; c <= maks_c && n < maks_rekt; c++) {
            if (!skitten[(size_t)r * kol + c]) continue;

            int c2 = c;
            while (c2 + 1 <= maks_c && skitten[(size_t)r * kol + c2 + 1]) c2++;

            int r2 = r;
            while (r2 + 1 <= maks_r) {
                int hel = 1;
                for (int i = c; i <= c2; i++) {
                    if (!skitten[(size_t)(r2 + 1) * kol + i]) { hel = 0; break; }
                }
                if (!hel) break;
                r2++;
            }

            for (int i = r; i <= r2; i++) {
                memset(&skitten[(size_t)i * kol + c], 0, (size_t)(c2 - c + 1));
            }
            ut[n++] = rekt_av_ruter(c, r, c2, r2, w, h);
        }
    }

    /* Ble noe liggende igjen, traff vi taket. Da er den omsluttende boksen
     * både enklere og billigere enn å presse flere rektangler ut. */
    for (int i = 0; i < kol * rad; i++) {
        if (skitten[i]) {
            ut[0] = rekt_av_ruter(min_c, min_r, maks_c, maks_r, w, h);
            n = 1;
            break;
        }
    }

    uint64_t areal = 0;
    for (int i = 0; i < n; i++) areal += (uint64_t)ut[i].w * ut[i].h;
    if (areal * 100u > (uint64_t)full_prosent * w * h) {
        ut[0] = hele;
        return 1;
    }

    return n;
}

void diff_slice(const uint8_t *pakket, uint16_t w, const rect_t *r, uint8_t *ut)
{
    const size_t stride  = (size_t)w / 2u;
    const size_t bredde  = (size_t)r->w / 2u;

    for (uint16_t i = 0; i < r->h; i++) {
        memcpy(ut + (size_t)i * bredde,
               pakket + ((size_t)r->y + i) * stride + r->x / 2u, bredde);
    }
}
