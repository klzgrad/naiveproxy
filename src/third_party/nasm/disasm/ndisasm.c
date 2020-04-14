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
 * ndisasm.c   the Netwide Disassembler main module
 */

#include "compiler.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "insns.h"
#include "nasm.h"
#include "nasmlib.h"
#include "error.h"
#include "ver.h"
#include "sync.h"
#include "disasm.h"

#define BPL 8                   /* bytes per line of hex dump */

static const char *help =
    "usage: ndisasm [-a] [-i] [-h] [-r] [-u] [-b bits] [-o origin] [-s sync...]\n"
    "               [-e bytes] [-k start,bytes] [-p vendor] file\n"
    "   -a or -i activates auto (intelligent) sync\n"
    "   -u same as -b 32\n"
    "   -b 16, -b 32 or -b 64 sets the processor mode\n"
    "   -h displays this text\n"
    "   -r or -v displays the version number\n"
    "   -e skips <bytes> bytes of header\n"
    "   -k avoids disassembling <bytes> bytes from position <start>\n"
    "   -p selects the preferred vendor instruction set (intel, amd, cyrix, idt)\n";

static void output_ins(uint64_t, uint8_t *, int, char *);
static void skip(uint32_t dist, FILE * fp);

static void ndisasm_verror(int severity, const char *fmt, va_list va)
{
    vfprintf(stderr, fmt, va);

    if (severity & ERR_FATAL)
	exit(1);
}

int main(int argc, char **argv)
{
    char buffer[INSN_MAX * 2], *p, *ep, *q;
    char outbuf[256];
    char *pname = *argv;
    char *filename = NULL;
    uint32_t nextsync, synclen, initskip = 0L;
    int lenread;
    int32_t lendis;
    bool autosync = false;
    int bits = 16, b;
    bool eof = false;
    iflag_t prefer;
    bool rn_error;
    int64_t offset;
    FILE *fp;

    tolower_init();
    nasm_set_verror(ndisasm_verror);
    iflag_clear_all(&prefer);

    offset = 0;
    init_sync();

    while (--argc) {
        char *v, *vv, *p = *++argv;
        if (*p == '-' && p[1]) {
            p++;
            while (*p)
                switch (nasm_tolower(*p)) {
                case 'a':      /* auto or intelligent sync */
                case 'i':
                    autosync = true;
                    p++;
                    break;
                case 'h':
                    fputs(help, stderr);
                    return 0;
                case 'r':
                case 'v':
                    fprintf(stderr,
                            "NDISASM version %s\n",
			    nasm_version);
                    return 0;
                case 'u':	/* -u for -b 32, -uu for -b 64 */
		    if (bits < 64)
			bits <<= 1;
                    p++;
                    break;
                case 'b':      /* bits */
                    v = p[1] ? p + 1 : --argc ? *++argv : NULL;
                    if (!v) {
                        fprintf(stderr, "%s: `-b' requires an argument\n",
                                pname);
                        return 1;
                    }
		    b = strtoul(v, &ep, 10);
		    if (*ep || !(bits == 16 || bits == 32 || bits == 64)) {
                        fprintf(stderr, "%s: argument to `-b' should"
                                " be 16, 32 or 64\n", pname);
                    } else {
			bits = b;
		    }
                    p = "";     /* force to next argument */
                    break;
                case 'o':      /* origin */
                    v = p[1] ? p + 1 : --argc ? *++argv : NULL;
                    if (!v) {
                        fprintf(stderr, "%s: `-o' requires an argument\n",
                                pname);
                        return 1;
                    }
                    offset = readnum(v, &rn_error);
                    if (rn_error) {
                        fprintf(stderr,
                                "%s: `-o' requires a numeric argument\n",
                                pname);
                        return 1;
                    }
                    p = "";     /* force to next argument */
                    break;
                case 's':      /* sync point */
                    v = p[1] ? p + 1 : --argc ? *++argv : NULL;
                    if (!v) {
                        fprintf(stderr, "%s: `-s' requires an argument\n",
                                pname);
                        return 1;
                    }
                    add_sync(readnum(v, &rn_error), 0L);
                    if (rn_error) {
                        fprintf(stderr,
                                "%s: `-s' requires a numeric argument\n",
                                pname);
                        return 1;
                    }
                    p = "";     /* force to next argument */
                    break;
                case 'e':      /* skip a header */
                    v = p[1] ? p + 1 : --argc ? *++argv : NULL;
                    if (!v) {
                        fprintf(stderr, "%s: `-e' requires an argument\n",
                                pname);
                        return 1;
                    }
                    initskip = readnum(v, &rn_error);
                    if (rn_error) {
                        fprintf(stderr,
                                "%s: `-e' requires a numeric argument\n",
                                pname);
                        return 1;
                    }
                    p = "";     /* force to next argument */
                    break;
                case 'k':      /* skip a region */
                    v = p[1] ? p + 1 : --argc ? *++argv : NULL;
                    if (!v) {
                        fprintf(stderr, "%s: `-k' requires an argument\n",
                                pname);
                        return 1;
                    }
                    vv = strchr(v, ',');
                    if (!vv) {
                        fprintf(stderr,
                                "%s: `-k' requires two numbers separated"
                                " by a comma\n", pname);
                        return 1;
                    }
                    *vv++ = '\0';
                    nextsync = readnum(v, &rn_error);
                    if (rn_error) {
                        fprintf(stderr,
                                "%s: `-k' requires numeric arguments\n",
                                pname);
                        return 1;
                    }
                    synclen = readnum(vv, &rn_error);
                    if (rn_error) {
                        fprintf(stderr,
                                "%s: `-k' requires numeric arguments\n",
                                pname);
                        return 1;
                    }
                    add_sync(nextsync, synclen);
                    p = "";     /* force to next argument */
                    break;
                case 'p':      /* preferred vendor */
                    v = p[1] ? p + 1 : --argc ? *++argv : NULL;
                    if (!v) {
                        fprintf(stderr, "%s: `-p' requires an argument\n",
                                pname);
                        return 1;
                    }
                    if (!strcmp(v, "intel")) {
                        iflag_clear_all(&prefer); /* default */
                    } else if (!strcmp(v, "amd")) {
                        iflag_clear_all(&prefer);
                        iflag_set(&prefer, IF_AMD);
                        iflag_set(&prefer, IF_3DNOW);
                    } else if (!strcmp(v, "cyrix")) {
                        iflag_clear_all(&prefer);
                        iflag_set(&prefer, IF_CYRIX);
                        iflag_set(&prefer, IF_3DNOW);
                    } else if (!strcmp(v, "idt") ||
                               !strcmp(v, "centaur") ||
                               !strcmp(v, "winchip")) {
                        iflag_clear_all(&prefer);
                        iflag_set(&prefer, IF_3DNOW);
                    } else {
                        fprintf(stderr,
                                "%s: unknown vendor `%s' specified with `-p'\n",
                                pname, v);
                        return 1;
                    }
                    p = "";     /* force to next argument */
                    break;
                default:       /*bf */
                    fprintf(stderr, "%s: unrecognised option `-%c'\n",
                            pname, *p);
                    return 1;
                }
        } else if (!filename) {
            filename = p;
        } else {
            fprintf(stderr, "%s: more than one filename specified\n",
                    pname);
            return 1;
        }
    }

    if (!filename) {
        fprintf(stderr, help, pname);
        return 0;
    }

    if (strcmp(filename, "-")) {
        fp = fopen(filename, "rb");
        if (!fp) {
            fprintf(stderr, "%s: unable to open `%s': %s\n",
                    pname, filename, strerror(errno));
            return 1;
        }
    } else
        fp = stdin;

    if (initskip > 0)
        skip(initskip, fp);

    /*
     * This main loop is really horrible, and wants rewriting with
     * an axe. It'll stay the way it is for a while though, until I
     * find the energy...
     */

    p = q = buffer;
    nextsync = next_sync(offset, &synclen);
    do {
        uint32_t to_read = buffer + sizeof(buffer) - p;
	if ((nextsync || synclen) &&
	    to_read > nextsync - offset - (p - q))
            to_read = nextsync - offset - (p - q);
        if (to_read) {
            lenread = fread(p, 1, to_read, fp);
            if (lenread == 0)
                eof = true;     /* help along systems with bad feof */
        } else
            lenread = 0;
        p += lenread;
        if ((nextsync || synclen) &&
	    (uint32_t)offset == nextsync) {
            if (synclen) {
                fprintf(stdout, "%08"PRIX64"  skipping 0x%"PRIX32" bytes\n",
			offset, synclen);
                offset += synclen;
                skip(synclen, fp);
            }
            p = q = buffer;
            nextsync = next_sync(offset, &synclen);
        }
        while (p > q && (p - q >= INSN_MAX || lenread == 0)) {
            lendis = disasm((uint8_t *)q, INSN_MAX, outbuf, sizeof(outbuf),
			    bits, offset, autosync, &prefer);
            if (!lendis || lendis > (p - q)
                || ((nextsync || synclen) &&
		    (uint32_t)lendis > nextsync - offset))
                lendis = eatbyte((uint8_t *) q, outbuf, sizeof(outbuf), bits);
            output_ins(offset, (uint8_t *) q, lendis, outbuf);
            q += lendis;
            offset += lendis;
        }
        if (q >= buffer + INSN_MAX) {
            uint8_t *r = (uint8_t *) buffer, *s = (uint8_t *) q;
            int count = p - q;
            while (count--)
                *r++ = *s++;
            p -= (q - buffer);
            q = buffer;
        }
    } while (lenread > 0 || !(eof || feof(fp)));

    if (fp != stdin)
        fclose(fp);

    return 0;
}

static void output_ins(uint64_t offset, uint8_t *data,
                       int datalen, char *insn)
{
    int bytes;
    fprintf(stdout, "%08"PRIX64"  ", offset);

    bytes = 0;
    while (datalen > 0 && bytes < BPL) {
        fprintf(stdout, "%02X", *data++);
        bytes++;
        datalen--;
    }

    fprintf(stdout, "%*s%s\n", (BPL + 1 - bytes) * 2, "", insn);

    while (datalen > 0) {
        fprintf(stdout, "         -");
        bytes = 0;
        while (datalen > 0 && bytes < BPL) {
            fprintf(stdout, "%02X", *data++);
            bytes++;
            datalen--;
        }
        fprintf(stdout, "\n");
    }
}

/*
 * Skip a certain amount of data in a file, either by seeking if
 * possible, or if that fails then by reading and discarding.
 */
static void skip(uint32_t dist, FILE * fp)
{
    char buffer[256];           /* should fit on most stacks :-) */

    /*
     * Got to be careful with fseek: at least one fseek I've tried
     * doesn't approve of SEEK_CUR. So I'll use SEEK_SET and
     * ftell... horrible but apparently necessary.
     */
    if (fseek(fp, dist + ftell(fp), SEEK_SET)) {
        while (dist > 0) {
            uint32_t len = (dist < sizeof(buffer) ?
                                 dist : sizeof(buffer));
            if (fread(buffer, 1, len, fp) < len) {
                perror("fread");
                exit(1);
            }
            dist -= len;
        }
    }
}
