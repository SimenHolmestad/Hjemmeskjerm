#include "bmp.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int feil(char *err, size_t n, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static int feil(char *err, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, n, fmt, ap);
    va_end(ap);
    return -1;
}

void bmp_free(bmp_image_t *img)
{
    if (img == NULL) return;
    free(img->buf);
    img->buf = NULL;
    img->top_row = NULL;
}

int bmp_load(const char *path, bmp_image_t *img, char *err, size_t errlen)
{
    memset(img, 0, sizeof *img);

    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return feil(err, errlen, "kan ikke apne %s", path);
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return feil(err, errlen, "%s er ikke en vanlig fil", path);
    }
    long size = ftell(fp);
    rewind(fp);

    /* 14 byte filhode + minst 40 byte DIB-hode + 1024 byte palett. */
    if (size < 14 + 40 + 1024) {
        fclose(fp);
        return feil(err, errlen, "%s er for liten til a vaere en palett-BMP (%ld byte)",
                    path, size);
    }

    uint8_t *buf = malloc((size_t)size);
    if (buf == NULL) {
        fclose(fp);
        return feil(err, errlen, "tom for minne (%ld byte)", size);
    }
    size_t lest = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    if (lest != (size_t)size) {
        free(buf);
        return feil(err, errlen, "kunne bare lese %zu av %ld byte fra %s",
                    lest, size, path);
    }

    if (buf[0] != 'B' || buf[1] != 'M') {
        free(buf);
        return feil(err, errlen, "%s er ikke en BMP-fil", path);
    }

    uint32_t off_bits = rd32(buf + 10);
    uint32_t dib_size = rd32(buf + 14);
    int32_t  width    = (int32_t)rd32(buf + 18);
    int32_t  height   = (int32_t)rd32(buf + 22);
    uint16_t planes   = rd16(buf + 26);
    uint16_t bpp      = rd16(buf + 28);
    uint32_t compr    = rd32(buf + 30);
    uint32_t clr_used = rd32(buf + 46);

    /* biSize kan være større enn 40 (BITMAPV4/V5), og paletten ligger da
     * lenger ut. Alt vi trenger av felter ligger innenfor de første 40. */
    if (dib_size < 40 || (long)(14 + dib_size) > size) {
        free(buf);
        return feil(err, errlen, "ukjent BMP-hodestorrelse %u", dib_size);
    }
    if (bpp != 8) {
        free(buf);
        return feil(err, errlen,
                    "forventet 8 bits per piksel (gratone med palett), fikk %u. "
                    "render.py skal skrive bildet med Pillow-modus \"L\".", bpp);
    }
    if (compr != 0) {
        free(buf);
        return feil(err, errlen, "komprimerte BMP-er stottes ikke (biCompression=%u)", compr);
    }
    if (planes != 1) {
        free(buf);
        return feil(err, errlen, "biPlanes=%u, forventet 1", planes);
    }
    if (width <= 0 || width > 65535 || height == 0
        || height < -65535 || height > 65535) {
        free(buf);
        return feil(err, errlen, "urimelige dimensjoner %dx%d", width, height);
    }

    uint32_t abs_h  = (uint32_t)(height < 0 ? -height : height);
    uint32_t stride = (((uint32_t)width * 8u + 31u) / 32u) * 4u;   /* 4-byte justert */
    uint32_t ncolors = (clr_used == 0) ? 256u : clr_used;
    if (ncolors > 256) ncolors = 256;

    uint32_t pal_off = 14u + dib_size;
    if ((long)(pal_off + ncolors * 4u) > size || pal_off + ncolors * 4u > off_bits) {
        free(buf);
        return feil(err, errlen, "paletten passer ikke i fila");
    }
    if ((long)off_bits > size
        || (uint64_t)off_bits + (uint64_t)stride * abs_h > (uint64_t)size) {
        free(buf);
        return feil(err, errlen, "pikseldataen er avkortet (trenger %llu byte, fila er %ld)",
                    (unsigned long long)off_bits + (unsigned long long)stride * abs_h, size);
    }

    /* Paletten er BGRA. Samme luma-formel som Waveshare bruker, slik at et
     * bilde som ikke er rent gratone oppforer seg likt hos oss og hos dem. */
    for (uint32_t i = 0; i < 256; i++) {
        if (i < ncolors) {
            const uint8_t *e = buf + pal_off + i * 4u;
            uint32_t b = e[0], g = e[1], r = e[2];
            img->lut[i] = (uint8_t)((r * 299u + g * 587u + b * 114u + 500u) / 1000u);
        } else {
            img->lut[i] = (uint8_t)i;
        }
    }

    const uint8_t *pixels = buf + off_bits;
    img->buf = buf;
    img->w   = (uint16_t)width;
    img->h   = (uint16_t)abs_h;
    if (height > 0) {
        /* Bunn-opp: siste rad i minnet er bildets overste. Dette er Pillow. */
        img->top_row  = pixels + (size_t)(abs_h - 1) * stride;
        img->row_step = -(int32_t)stride;
    } else {
        img->top_row  = pixels;
        img->row_step = (int32_t)stride;
    }
    return 0;
}
