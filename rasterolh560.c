/*
 * rastertolh560 - CUPS raster filter for LH-560 / "LeftHans2" TSPL label printers
 *
 * Unofficial. The command stream was reverse-engineered from the output of the
 * vendor's macOS rastertolabel filter (observed behaviour only, no vendor code).
 *
 * Per page the vendor filter emits exactly:
 *
 *   SIZE <w> mm ,<h> mm
 *   REFERENCE <hoff*8>,<voff*8>
 *   DIRECTION <rotate>,0
 *   GAP 3 mm,0 mm
 *   OFFSET <voff> mm
 *   DENSITY <n>            (omitted when Darkness=Default)
 *   SPEED <n>              (omitted when zePrintRate=Default)
 *   SETC AUTODOTTED ON|OFF
 *   SETC PAUSEKEY ON
 *   SETC WATERMARK OFF
 *   CLS
 *   BITMAP 0,0,<ceil(w/8)>,<h>,1,<raw 1-bpp data, 1 = white>
 *   PRINT 1,1
 *
 * Observed no-ops in the vendor filter (so also here): media tracking
 * (Continuous / Gap / Black Line) and FeedOffset.
 *
 * Usage (CUPS filter): rastertolh560 job user title copies options [file]
 *
 * Extra job option: Threshold=1..255 (default 128). A pixel is black when its
 * gray value is below the threshold.
 *
 * Build:  cc -O2 -Wall -Wno-deprecated-declarations -o rastertolh560 \
 *            rastertolh560.c $(cups-config --cflags --libs)
 */

#include <cups/cups.h>
#include <cups/raster.h>
#include <cups/ppd.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define DOTS_PER_MM 8	/* 203 dpi, same constant the vendor filter uses */

static int            num_options;
static cups_option_t *options;
static ppd_file_t    *ppd;

/* Job option first, then PPD default (covers `lpadmin -o X=Y`), then fallback. */
static const char *
get_opt(const char *name, const char *fallback)
{
  const char   *val = cupsGetOption(name, num_options, options);
  ppd_option_t *opt;

  if (val)
    return val;
  if (ppd && (opt = ppdFindOption(ppd, name)) != NULL && opt->defchoice[0])
    return opt->defchoice;
  return fallback;
}

static int
clamp(int v, int lo, int hi)
{
  return v < lo ? lo : (v > hi ? hi : v);
}

static int
is_default(const char *s)
{
  return !strcasecmp(s, "Default");
}

int
main(int argc, char *argv[])
{
  int                   fd = 0, page = 0;
  cups_raster_t        *ras;
  cups_page_header2_t   hdr;
  const char           *s;

  if (argc < 6 || argc > 7)
  {
    fputs("ERROR: Usage: rastertolh560 job user title copies options [file]\n", stderr);
    return 1;
  }
  if (argc == 7 && (fd = open(argv[6], O_RDONLY)) < 0)
  {
    perror("ERROR: Unable to open raster file");
    return 1;
  }

  num_options = cupsParseOptions(argv[5], 0, &options);
  if ((s = getenv("PPD")) != NULL && (ppd = ppdOpenFile(s)) != NULL)
    ppdMarkDefaults(ppd);

  if ((ras = cupsRasterOpen(fd, CUPS_RASTER_READ)) == NULL)
  {
    fputs("ERROR: Unable to read raster stream\n", stderr);
    return 1;
  }

  while (cupsRasterReadHeader2(ras, &hdr))
  {
    unsigned       x, y, width = hdr.cupsWidth, height = hdr.cupsHeight;
    unsigned       rowbytes = (width + 7) / 8;
    unsigned       copies = hdr.NumCopies > 0 ? hdr.NumCopies : 1;
    unsigned char *in, *out;
    int            wmm, hmm, hoff, voff, rot, thr, invert;

    if (hdr.cupsBitsPerPixel != 8 ||
        (hdr.cupsColorSpace != CUPS_CSPACE_W && hdr.cupsColorSpace != CUPS_CSPACE_K))
    {
      fputs("ERROR: Only 8-bit gray raster input is supported\n", stderr);
      return 1;
    }
    invert = (hdr.cupsColorSpace == CUPS_CSPACE_K);

    page ++;
    fprintf(stderr, "PAGE: %d %u\n", page, copies);

    wmm  = (int)(hdr.PageSize[0] * 25.4 / 72.0 + 0.5);
    hmm  = (int)(hdr.PageSize[1] * 25.4 / 72.0 + 0.5);
    hoff = atoi(get_opt("AdjustHoriaontal", "0"));
    voff = atoi(get_opt("AdjustVertical", "0"));
    rot  = clamp(atoi(get_opt("Rotate", "0")), 0, 3);
    thr  = clamp(atoi(get_opt("Threshold", "128")), 1, 255);

    printf("SIZE %d mm ,%d mm\n", wmm, hmm);
    printf("REFERENCE %d,%d\n", hoff * DOTS_PER_MM, voff * DOTS_PER_MM);
    printf("DIRECTION %d,0\n", rot);
    printf("GAP 3 mm,0 mm\n");
    printf("OFFSET %d mm\n", voff);

    s = get_opt("Darkness", "6");
    if (!is_default(s))
      printf("DENSITY %d\n", clamp(atoi(s), 0, 15));

    s = get_opt("zePrintRate", "4");
    if (!is_default(s))
      printf("SPEED %d\n", clamp(atoi(s), 1, 15));

    printf("SETC AUTODOTTED %s\n", atoi(get_opt("AutoDotted", "0")) ? "ON" : "OFF");
    printf("SETC PAUSEKEY ON\n");
    printf("SETC WATERMARK OFF\n");
    printf("CLS\n");
    printf("BITMAP 0,0,%u,%u,1,", rowbytes, height);

    in  = malloc(hdr.cupsBytesPerLine ? hdr.cupsBytesPerLine : 1);
    out = malloc(rowbytes ? rowbytes : 1);
    if (!in || !out)
    {
      fputs("ERROR: Out of memory\n", stderr);
      return 1;
    }

    for (y = 0; y < height; y ++)
    {
      /* On a short read, finish the page with white so BITMAP stays well-formed. */
      if (cupsRasterReadPixels(ras, in, hdr.cupsBytesPerLine) < 1)
        memset(in, invert ? 0 : 255, hdr.cupsBytesPerLine);

      memset(out, 0xff, rowbytes);          /* padding bits stay white (1) */
      for (x = 0; x < width; x ++)
      {
        int v = invert ? 255 - in[x] : in[x];
        if (v < thr)
          out[x >> 3] &= (unsigned char)~(0x80 >> (x & 7));
      }
      fwrite(out, 1, rowbytes, stdout);
    }

    printf("PRINT 1,%u\n", copies);
    fflush(stdout);
    free(in);
    free(out);
  }

  cupsRasterClose(ras);
  if (fd)
    close(fd);
  if (ppd)
    ppdClose(ppd);
  cupsFreeOptions(num_options, options);

  if (page == 0)
    fputs("ERROR: No pages found\n", stderr);
  return page == 0;
}
