/* epaper – viser et bilde på en Waveshare 10,3" IT8951 e-paper-skjerm.
 *
 *   epaper display <fil.bmp>   vis bildet (4bpp, GC16)
 *   epaper clear [--init]      gjør skjermen hvit
 *   epaper info                skriv ut hva panelet sier om seg selv
 *
 * Bildet skal allerede være rotert til panelets liggende format av
 * render.py. Speilingen panelet trenger gjør vi her, i pakkingen.
 */
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

#include "bmp.h"
#include "common.h"
#include "pack.h"

#include "EPD_IT8951.h"
#include "epd_host.h"

/* Slås på med -v eller EPAPER_DEBUG=1. Leses av Debug()-makroen i vendret kode. */
int Debug_Enabled = 0;

static volatile sig_atomic_t g_stop = 0;
static int g_hw_oppe = 0;     /* har vi kalt DEV_Module_Init? */
static int g_lock_fd = -1;

/* Kalles fra vendret driverkode når den står fast og ikke har noen vei videre. */
void EPD_Host_Fatal(const char *fmt, ...)
{
    va_list ap;
    fputs("epaper: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    /* Med vilje _exit og ikke exit: vi kan bli kalt midt i en SPI-overføring,
     * og da er det tryggere å slippe taket enn å prøve å rydde. */
    _exit(EXIT_HARDWARE);
}

static void handler(int sig)
{
    (void)sig;
    g_stop = 1;   /* alt annet gjøres ved neste sjekkpunkt */
}

static void install_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = handler;
    /* SA_RESTART: uten den ville EINTR kunne bryte systemkall midt i en
     * overføring. */
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
}

static void ryd_opp(void)
{
    if (g_hw_oppe) {
        EPD_IT8951_Sleep();
        DEV_Module_Exit();
        g_hw_oppe = 0;
    }
    if (g_lock_fd >= 0) {
        close(g_lock_fd);
        g_lock_fd = -1;
    }
}

static void avbrutt_hvis_bedt_om(void)
{
    if (!g_stop) return;
    fprintf(stderr, "epaper: avbrutt\n");
    if (g_hw_oppe) EPD_IT8951_WaitForDisplayReady();
    ryd_opp();
    exit(EXIT_SIGNAL);
}

static void bruk(void)
{
    fputs(
        "bruk:\n"
        "  epaper display <fil.bmp>   vis bildet (4bpp, GC16)\n"
        "  epaper clear [--init]      gjor skjermen hvit (--init skrubber ghosting)\n"
        "  epaper info                skriv ut panelets egen info\n"
        "\n"
        "flagg:\n"
        "  -v           logg fra driveren til stderr (samme som EPAPER_DEBUG=1)\n"
        "  --packed     bruk blokkskriving over SPI. Raskere, men mindre utproevd;\n"
        "               driveren sjekker da BUSY bare ved starten av overfoeringen.\n"
        "\n"
        "miljo:\n"
        "  EPAPER_VCOM  panelets VCOM i volt, f.eks. -1.14. Staar paa flexkabelen.\n",
        stderr);
}

/* VCOM i millivolt som positivt tall, slik EPD_IT8951_Init vil ha det. */
static int les_vcom_mv(void)
{
    const char *s = getenv("EPAPER_VCOM");
    if (s == NULL || *s == '\0') return EPAPER_VCOM_DEFAULT_MV;

    char *slutt = NULL;
    errno = 0;
    double v = strtod(s, &slutt);
    if (errno != 0 || slutt == s || *slutt != '\0') {
        fprintf(stderr, "epaper: EPAPER_VCOM=\"%s\" er ikke et tall\n", s);
        exit(EXIT_CONFIG);
    }
    long mv = lround(fabs(v) * 1000.0);
    if (mv < 200 || mv > 5000) {
        fprintf(stderr, "epaper: EPAPER_VCOM=%s gir %ld mV, som er utenfor "
                        "det rimelige (200-5000 mV)\n", s, mv);
        exit(EXIT_CONFIG);
    }
    return (int)mv;
}

/* Hindrer at to prosesser roter på SPI-bussen samtidig – f.eks. render.py og
 * en manuell kjøring. Klarer vi ikke å låse, sier vi fra, men fortsetter:
 * en manglende låsefil skal ikke gjøre skjermen svart. */
static void ta_lock(void)
{
    const char *sti = "/run/lock/epaper.lock";
    g_lock_fd = open(sti, O_RDWR | O_CREAT | O_CLOEXEC, 0666);
    if (g_lock_fd < 0) {
        sti = "/tmp/epaper.lock";
        g_lock_fd = open(sti, O_RDWR | O_CREAT | O_CLOEXEC, 0666);
    }
    if (g_lock_fd < 0) {
        Debug("kunne ikke apne lasefil: %s\n", strerror(errno));
        return;
    }
    if (flock(g_lock_fd, LOCK_EX) != 0) {
        Debug("flock feilet: %s\n", strerror(errno));
    }
}

/* Firmware-strengene er ikke garantert NUL-terminerte. */
static void trygg_streng(char *ut, size_t n, const UBYTE *inn, size_t maks)
{
    size_t i = 0;
    for (; i < maks && i + 1 < n; i++) {
        unsigned char c = (unsigned char)inn[i];
        if (c == '\0') break;
        ut[i] = (c >= 0x20 && c < 0x7f) ? (char)c : '?';
    }
    ut[i] = '\0';
}

int main(int argc, char **argv)
{
    const char *kommando = NULL;
    const char *bmp_sti  = NULL;
    int init_clear   = 0;
    int packed_write = 0;

    if (getenv("EPAPER_DEBUG") != NULL) Debug_Enabled = 1;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-v") == 0)            { Debug_Enabled = 1; }
        else if (strcmp(a, "--packed") == 0) { packed_write = 1; }
        else if (strcmp(a, "--init") == 0)   { init_clear = 1; }
        else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) { bruk(); return EXIT_OK; }
        else if (a[0] == '-')  { fprintf(stderr, "epaper: ukjent flagg %s\n", a); bruk(); return EXIT_USAGE; }
        else if (kommando == NULL) { kommando = a; }
        else if (bmp_sti == NULL)  { bmp_sti = a; }
        else { fprintf(stderr, "epaper: for mange argumenter\n"); return EXIT_USAGE; }
    }

    if (kommando == NULL) { bruk(); return EXIT_USAGE; }

    int er_display = (strcmp(kommando, "display") == 0);
    int er_clear   = (strcmp(kommando, "clear") == 0);
    int er_info    = (strcmp(kommando, "info") == 0);
    if (!er_display && !er_clear && !er_info) {
        fprintf(stderr, "epaper: ukjent kommando \"%s\"\n", kommando);
        bruk();
        return EXIT_USAGE;
    }
    if (er_display && bmp_sti == NULL) {
        fprintf(stderr, "epaper: display trenger en BMP-fil\n");
        return EXIT_USAGE;
    }

    /* Les og valider bildet FØR vi rører hardware. En ugyldig fil skal ikke
     * kunne etterlate panelet halvtegnet, og det er ingen grunn til å ta
     * GPIO-linjene for noe som uansett kommer til å feile. */
    bmp_image_t bilde;
    memset(&bilde, 0, sizeof bilde);
    if (er_display) {
        char err[256];
        if (bmp_load(bmp_sti, &bilde, err, sizeof err) != 0) {
            fprintf(stderr, "epaper: %s\n", err);
            return EXIT_INPUT;
        }
        if (bilde.w % 2 != 0) {
            fprintf(stderr, "epaper: bredden ma vaere et partall (4bpp pakkes to og to)\n");
            bmp_free(&bilde);
            return EXIT_INPUT;
        }
    }

    install_signals();
    ta_lock();
    avbrutt_hvis_bedt_om();

    if (DEV_Module_Init() != 0) {
        fprintf(stderr, "epaper: fikk ikke satt opp GPIO/SPI. Sjekk at SPI er "
                        "skrudd paa, og at brukeren er med i gruppene gpio og spi.\n");
        bmp_free(&bilde);
        return EXIT_HARDWARE;
    }
    g_hw_oppe = 1;

    int vcom_mv = les_vcom_mv();
    IT8951_Dev_Info info = EPD_IT8951_Init((UWORD)vcom_mv);

    if (Debug_Enabled) {
        /* Ra bytes fra GetSystemInfo, FOER fornuftssjekken - det er nettopp
         * naar den slaar til at man trenger aa se hva som faktisk kom inn.
         * Bare nuller betyr at ingenting svarer; soppel betyr at SPI gaar,
         * men at noe annet er galt. */
        const unsigned char *ra = (const unsigned char *)&info;
        fprintf(stderr, "ra enhetsinfo (%zu byte):", sizeof info);
        for (size_t i = 0; i < sizeof info; i++) {
            fprintf(stderr, "%s%02x", (i % 16 == 0) ? "\n  " : " ", ra[i]);
        }
        fputc('\n', stderr);
        fprintf(stderr, "BUSY-pinne (GPIO %d) leser: %d\n",
                EPD_BUSY_PIN, DEV_Digital_Read(EPD_BUSY_PIN));
    }

    /* En dod SPI-buss gir gjerne plausible, men helt gale tall. */
    if (info.Panel_W == 0 || info.Panel_H == 0
        || info.Panel_W > 4096 || info.Panel_H > 4096) {
        fprintf(stderr, "epaper: panelet rapporterte %ux%u, som ikke gir mening. "
                        "Sjekk SPI-kablingen.\n", info.Panel_W, info.Panel_H);
        ryd_opp();
        bmp_free(&bilde);
        return EXIT_HARDWARE;
    }

    /* Memory_Addr_H er uint16_t og ville blitt forfremmet til signed int her,
     * så en verdi over 0x7fff ville vært signed overflow. Derfor castet. */
    UDOUBLE target = ((UDOUBLE)info.Memory_Addr_H << 16) | (UDOUBLE)info.Memory_Addr_L;

    avbrutt_hvis_bedt_om();

    if (er_info) {
        char fw[16], lut[16];
        trygg_streng(fw, sizeof fw, (const UBYTE *)info.FW_Version, 16);
        trygg_streng(lut, sizeof lut, (const UBYTE *)info.LUT_Version, 16);
        printf("panel_w=%u\n", info.Panel_W);
        printf("panel_h=%u\n", info.Panel_H);
        printf("memory_addr=0x%08lx\n", (unsigned long)target);
        printf("fw_version=%s\n", fw);
        printf("lut_version=%s\n", lut);
        printf("vcom_mv=%d\n", vcom_mv);
        ryd_opp();
        return EXIT_OK;
    }

    if (er_clear) {
        EPD_IT8951_Clear_Refresh(info, target, init_clear ? INIT_Mode : GC16_Mode);
    } else {
        if (bilde.w != info.Panel_W || bilde.h != info.Panel_H) {
            fprintf(stderr, "epaper: bildet er %ux%u, men panelet er %ux%u. "
                            "render.py skal rotere til panelets format.\n",
                    bilde.w, bilde.h, info.Panel_W, info.Panel_H);
            ryd_opp();
            bmp_free(&bilde);
            return EXIT_INPUT;
        }

        size_t pakket_stor = (size_t)(bilde.w / 2) * bilde.h;
        uint8_t *pakket = malloc(pakket_stor);
        if (pakket == NULL) {
            fprintf(stderr, "epaper: tom for minne (%zu byte)\n", pakket_stor);
            ryd_opp();
            bmp_free(&bilde);
            return EXIT_HARDWARE;
        }
        pack_4bpp_mirrored(bilde.top_row, bilde.row_step, bilde.lut,
                           bilde.w, bilde.h, pakket);
        bmp_free(&bilde);
        avbrutt_hvis_bedt_om();

        /* Hold=false gir Display_AreaBuf med eksplisitt måladresse – samme vei
         * som Waveshares eget fullskjerms-4bpp-eksempel bruker. Vi venter selv
         * på at panelet blir ferdig rett nedenfor. */
        EPD_IT8951_4bp_Refresh(pakket, 0, 0, info.Panel_W, info.Panel_H,
                               false, target, packed_write ? true : false);
        free(pakket);
    }

    /* Både Clear_Refresh og 4bp_Refresh returnerer så snart kommandoen er
     * sendt – panelet oppdaterer fortsatt. Går vi ut nå, drar DEV_Module_Exit
     * RST lav midt i oppdateringen. Den lille pausen er fordi LUTAFSR ikke
     * rekker å bli nullforskjellig med en gang. */
    DEV_Delay_ms(100);
    EPD_IT8951_WaitForDisplayReady();

    ryd_opp();
    return EXIT_OK;
}
