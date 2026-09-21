#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "state.h"

#define MAGI    0x52505045u   /* "EPPR" */
#define VERSJON 1u

struct hode {
    uint32_t magi;
    uint16_t versjon;
    uint16_t w;
    uint16_t h;
    uint16_t pad;
};

/* Samme mønster som låsefila i main.c: et fast sted om vi får lov, ellers
 * /tmp. Havner den i /tmp, overlever den ikke en omstart, og da blir første
 * runde etterpå en full opptegning. Det er den trygge retningen. */
static const char *sti(void)
{
    static char valgt[64];
    if (valgt[0] != '\0') return valgt;

    mkdir("/var/lib/epaper", 0755);
    if (access("/var/lib/epaper", W_OK) == 0) {
        snprintf(valgt, sizeof valgt, "/var/lib/epaper/prev.4bpp");
    } else {
        snprintf(valgt, sizeof valgt, "/tmp/epaper-prev.4bpp");
    }
    return valgt;
}

int state_load(uint8_t *buf, uint16_t w, uint16_t h)
{
    size_t n = (size_t)(w / 2) * h;
    FILE *fp = fopen(sti(), "rb");
    if (fp == NULL) return -1;

    struct hode hode;
    int ok = fread(&hode, sizeof hode, 1, fp) == 1
             && hode.magi == MAGI && hode.versjon == VERSJON
             && hode.w == w && hode.h == h
             && fread(buf, 1, n, fp) == n;
    fclose(fp);
    return ok ? 0 : -1;
}

int state_store(const uint8_t *buf, uint16_t w, uint16_t h)
{
    size_t n = (size_t)(w / 2) * h;
    char midlertidig[80];
    snprintf(midlertidig, sizeof midlertidig, "%s.tmp", sti());

    FILE *fp = fopen(midlertidig, "wb");
    if (fp == NULL) return -1;

    struct hode hode = { MAGI, VERSJON, w, h, 0 };
    int ok = fwrite(&hode, sizeof hode, 1, fp) == 1
             && fwrite(buf, 1, n, fp) == n
             && fflush(fp) == 0
             && fsync(fileno(fp)) == 0;
    if (fclose(fp) != 0) ok = 0;

    /* rename() først når fila er hel, så et avbrudd ikke etterlater et
     * halvskrevet buffer som ser gyldig ut. */
    if (ok && rename(midlertidig, sti()) == 0) return 0;
    unlink(midlertidig);
    return -1;
}

void state_drop(void)
{
    unlink(sti());
}
