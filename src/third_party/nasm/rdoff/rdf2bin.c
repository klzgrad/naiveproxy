/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1996-2009 The NASM Authors - All Rights Reserved
 *   See the file AUTHORS included with the NASM distribution for
 *   the specific copyright holders.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following
 *   conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *
 *     THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 *     CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *     INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *     MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *     DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 *     CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *     SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *     NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *     LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *     HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *     CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 *     OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 *     EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ----------------------------------------------------------------------- */

/*
 * rdf2bin.c - convert an RDOFF object file to flat binary
 */

#include "compiler.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "rdfload.h"
#include "nasmlib.h"

const char *progname;

static uint32_t origin = 0;
static bool origin_def = false;
static uint32_t align = 16;
static bool align_def = false;

struct output_format {
    const char *name;
    const char *mode;
    int (*init)(FILE *f);
    int (*output)(FILE *f, void *data, uint32_t bytes, uint32_t where);
    int (*fini)(FILE *f);
};

static int null_init_fini(FILE *f)
{
    (void)f;
    return 0;
}

static int com_init(FILE *f)
{
    (void)f;
    if (!origin_def)
	origin = 0x100;
    return 0;
}

static int output_bin(FILE *f, void *data, uint32_t bytes, uint32_t where)
{
    static uint32_t offset = 0;	/* Current file offset, if applicable */
    size_t pad;

    if (where-origin < offset) {
	fprintf(stderr, "%s: internal error: backwards movement\n", progname);
	exit(1);
    }

    pad = (where-origin) - offset;
    fwritezero(pad, f);
    offset += pad;

    if (fwrite(data, 1, bytes, f) != bytes)
	return -1;
    offset += bytes;

    return 0;
}

static int write_ith_record(FILE *f, unsigned int len, uint16_t addr,
			    uint8_t type, void *data)
{
    char buf[1+2+4+2+255*2+2+2];
    char *p = buf;
    uint8_t csum, *dptr = data;
    unsigned int i;

    if (len > 255) {
	fprintf(stderr, "%s: internal error: invalid ith record size\n",
		progname);
	exit(1);
    }

    csum = len + addr + (addr >> 8) + type;
    for (i = 0; i < len; i++)
	csum += dptr[i];
    csum = -csum;

    p += sprintf(p, ":%02X%04X%02X", len, addr, type);
    for (i = 0; i < len; i++)
	p += sprintf(p, "%02X", dptr[i]);
    p += sprintf(p, "%02X\n", csum);

    if (fwrite(buf, 1, p-buf, f) != (size_t)(p-buf))
	return -1;

    return 0;
}

static int output_ith(FILE *f, void *data, uint32_t bytes, uint32_t where)
{
    static uint32_t last = 0;	/* Last address written */
    uint8_t abuf[2];
    uint8_t *dbuf = data;
    uint32_t chunk;

    while (bytes) {
	if ((where ^ last) & ~0xffff) {
	    abuf[0] = where >> 24;
	    abuf[1] = where >> 16;
	    if (write_ith_record(f, 2, 0, 4, abuf))
		return -1;
	}

	/* Output up to 32 bytes, but always end on an aligned boundary */
	chunk = 32 - (where & 31);
	if (bytes < chunk)
	    chunk = bytes;

	if (write_ith_record(f, chunk, (uint16_t)where, 0, dbuf))
	    return -1;

	dbuf += chunk;
	last = where + chunk - 1;
	where += chunk;
	bytes -= chunk;
    }
    return 0;
}

static int fini_ith(FILE *f)
{
    /* XXX: entry point? */
    return write_ith_record(f, 0, 0, 1, NULL);
}

static int write_srecord(FILE *f, unsigned int len,  unsigned int alen,
			 uint32_t addr, uint8_t type, void *data)
{
    char buf[2+2+8+255*2+2+2];
    char *p = buf;
    uint8_t csum, *dptr = data;
    unsigned int i;

    if (len > 255) {
	fprintf(stderr, "%s: internal error: invalid srec record size\n",
		progname);
	exit(1);
    }

    switch (alen) {
    case 2:
	addr &= 0xffff;
	break;
    case 3:
	addr &= 0xffffff;
	break;
    case 4:
	break;
    default:
	fprintf(stderr, "%s: internal error: invalid srec address length\n",
		progname);
	exit(1);
    }

    csum = (len+alen+1) + addr + (addr >> 8) + (addr >> 16) + (addr >> 24);
    for (i = 0; i < len; i++)
	csum += dptr[i];
    csum = 0xff-csum;

    p += sprintf(p, "S%c%02X%0*X", type, len+alen+1, alen*2, addr);
    for (i = 0; i < len; i++)
	p += sprintf(p, "%02X", dptr[i]);
    p += sprintf(p, "%02X\n", csum);

    if (fwrite(buf, 1, p-buf, f) != (size_t)(p-buf))
	return -1;

    return 0;
}

static int init_srec(FILE *f)
{
    return write_srecord(f, 0, 2, 0, '0', NULL);
}

static int fini_srec(FILE *f)
{
    /* XXX: entry point? */
    return write_srecord(f, 0, 4, 0, '7', NULL);
}

static int output_srec(FILE *f, void *data, uint32_t bytes, uint32_t where)
{
    uint8_t *dbuf = data;
    unsigned int chunk;

    while (bytes) {
	/* Output up to 32 bytes, but always end on an aligned boundary */
	chunk = 32 - (where & 31);
	if (bytes < chunk)
	    chunk = bytes;

	if (write_srecord(f, chunk, 4, where, '3', dbuf))
	    return -1;

	dbuf += chunk;
	where += chunk;
	bytes -= chunk;
    }
    return 0;
}

static struct output_format output_formats[] = {
    { "bin",  "wb", null_init_fini, output_bin, null_init_fini },
    { "com",  "wb", com_init, output_bin, null_init_fini },
    { "ith",  "wt", null_init_fini, output_ith, fini_ith },
    { "ihx",  "wt", null_init_fini, output_ith, fini_ith },
    { "srec", "wt", init_srec, output_srec, fini_srec },
    { NULL, NULL, NULL, NULL, NULL }
};

static const char *getformat(const char *pathname)
{
    const char *p;
    static char fmt_buf[16];

    /*
     * Search backwards for the string "rdf2" followed by a string
     * of alphanumeric characters.  This should handle path prefixes,
     * as well as extensions (e.g. C:\FOO\RDF2SREC.EXE).
     */
    for (p = strchr(pathname, '\0')-1 ; p >= pathname ; p--) {
	if (!nasm_stricmp(p, "rdf2")) {
	    const char *q = p+4;
	    char *r = fmt_buf;
	    while (isalnum(*q) && r < fmt_buf+sizeof fmt_buf-1)
		*r++ = *q++;
	    *r = '\0';
	    if (fmt_buf[0])
		return fmt_buf;
	}
     }
    return NULL;
}

static void usage(void)
{
    fprintf(stderr,
	    "Usage: %s [options] input-file output-file\n"
	    "Options:\n"
	    "    -o origin       Specify the relocation origin\n"
	    "    -p alignment    Specify minimum segment alignment\n"
	    "    -f format       Select format (bin, com, ith, srec)\n"
	    "    -q              Run quiet\n"
	    "    -v              Run verbose\n",
	    progname);
}

int main(int argc, char **argv)
{
    rdfmodule *m;
    bool err;
    FILE *of;
    int codepad, datapad;
    const char *format = NULL;
    const struct output_format *fmt;
    bool quiet = false;

    progname = argv[0];

    if (argc < 2) {
	usage();
        return 1;
    }

    rdoff_init();

    argv++, argc--;

    while (argc > 2) {
	if (argv[0][0] == '-' && argv[0][1] && !argv[0][2]) {
	    switch (argv[0][1]) {
	    case 'o':
		argv++, argc--;
		origin = readnum(*argv, &err);
		if (err) {
		    fprintf(stderr, "%s: invalid parameter: %s\n",
			    progname, *argv);
		    return 1;
		}
		origin_def = true;
		break;
	    case 'p':
		argv++, argc--;
		align = readnum(*argv, &err);
		if (err) {
		    fprintf(stderr, "%s: invalid parameter: %s\n",
			    progname, *argv);
		    return 1;
		}
		align_def = true;
		break;
	    case 'f':
		argv++, argc--;
		format = *argv;
		break;
	    case 'q':
		quiet = true;
		break;
	    case 'v':
		quiet = false;
		break;
	    case 'h':
		usage();
		return 0;
	    default:
		fprintf(stderr, "%s: unknown option: %s\n",
			progname, *argv);
		return 1;
	    }
	}
        argv++, argc--;
    }

    if (argc < 2) {
	usage();
        return 1;
    }

    if (!format)
	format = getformat(progname);

    if (!format) {
	fprintf(stderr, "%s: unable to determine desired output format\n",
		progname);
	return 1;
    }

    for (fmt = output_formats; fmt->name; fmt++) {
	if (!nasm_stricmp(format, fmt->name))
	    break;
    }

    if (!fmt->name) {
	fprintf(stderr, "%s: unknown output format: %s\n", progname, format);
	return 1;
    }

    m = rdfload(*argv);

    if (!m) {
        rdfperror(progname, *argv);
        return 1;
    }

    if (!quiet)
	printf("relocating %s: origin=%"PRIx32", align=%d\n",
	       *argv, origin, align);

    m->textrel = origin;
    m->datarel = origin + m->f.seg[0].length;
    if (m->datarel % align != 0) {
        codepad = align - (m->datarel % align);
        m->datarel += codepad;
    } else
        codepad = 0;

    m->bssrel = m->datarel + m->f.seg[1].length;
    if (m->bssrel % align != 0) {
        datapad = align - (m->bssrel % align);
        m->bssrel += datapad;
    } else
        datapad = 0;

    if (!quiet)
	printf("code: %08"PRIx32"\ndata: %08"PRIx32"\nbss:  %08"PRIx32"\n",
	       m->textrel, m->datarel, m->bssrel);

    rdf_relocate(m);

    argv++;

    of = fopen(*argv, fmt->mode);
    if (!of) {
        fprintf(stderr, "%s: could not open output file %s: %s\n",
		progname, *argv, strerror(errno));
        return 1;
    }

    if (fmt->init(of) ||
	fmt->output(of, m->t, m->f.seg[0].length, m->textrel) ||
	fmt->output(of, m->d, m->f.seg[1].length, m->datarel) ||
	fmt->fini(of)) {
        fprintf(stderr, "%s: error writing to %s: %s\n",
		progname, *argv, strerror(errno));
        return 1;
    }

    fclose(of);
    return 0;
}
