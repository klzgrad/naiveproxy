/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1996-2014 The NASM Authors - All Rights Reserved
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
 * rdfdump.c - dump RDOFF file header.
 */

#include "compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rdfutils.h"

#define PROGRAM_VERSION	"2.3"

FILE *infile;

static void print_header(int32_t length, int rdf_version)
{
    char buf[129], t, l, s, flags;
    uint8_t reclen;
    int32_t o, ll;
    uint16_t rs;

    while (length > 0) {
        nasm_read(&t, 1, infile);
        if (rdf_version >= 2) {
            nasm_read(&reclen, 1, infile);
        }
        switch (t) {
        case RDFREC_GENERIC:   /* generic record */
            printf("  generic record (length=%d)\n", (int)reclen);
            fseek(infile, reclen, SEEK_CUR);
            break;

        case RDFREC_RELOC:     /* relocation record */
        case RDFREC_SEGRELOC:  /* segment relocation */
            nasm_read(&s, 1, infile);
            nasm_read(&o, 4, infile);
            nasm_read(&l, 1, infile);
            nasm_read(&rs, 2, infile);
            printf("  %s: location (%04x:%08"PRIx32"), length %d, "
                   "referred seg %04x\n",
                   t == 1 ? "relocation" : "seg relocation", (int)s,
                   translateint32_t(o), (int)l, translateint16_t(rs));
            if (rdf_version >= 2 && reclen != 8)
                printf("    warning: reclen != 8\n");
            if (rdf_version == 1)
                length -= 9;
            if (rdf_version == 1 && t == 6)
                printf
                    ("    warning: seg relocation not supported in RDOFF1\n");
            break;

        case RDFREC_IMPORT:    /* import record */
        case RDFREC_FARIMPORT: /* import far symbol */
            nasm_read(&flags, 1, infile);
            nasm_read(&rs, 2, infile);
            ll = 0;

            if (rdf_version == 1) {
                do {
                    nasm_read(&buf[ll], 1, infile);
                } while (buf[ll++]);
            } else {
                for (; ll < reclen - 3; ll++)
                    nasm_read(&buf[ll], 1, infile);
            }

            if (t == 7)
                printf("far ");
            printf((flags & SYM_IMPORT) ? "  import" : "  extern");
            if (flags & SYM_FUNCTION)
                printf(" proc");
            if (flags & SYM_DATA)
                printf(" data");
            printf(": segment %04x = %s\n", translateint16_t(rs), buf);
            if (rdf_version == 1)
                length -= ll + 3;
            if (rdf_version == 1 && t == 7)
                printf
                    ("    warning: far import not supported in RDOFF1\n");
            break;

        case RDFREC_GLOBAL:    /* export record */
            nasm_read(&flags, 1, infile);
            nasm_read(&s, 1, infile);
            nasm_read(&o, 4, infile);
            ll = 0;

            if (rdf_version == 1) {
                do {
                    nasm_read(&buf[ll], 1, infile);
                } while (buf[ll++]);
            } else {
                for (; ll < reclen - 6; ll++)
                    nasm_read(&buf[ll], 1, infile);
            }
            printf((flags & SYM_GLOBAL) ? "  export" : "  public");
            if (flags & SYM_FUNCTION)
                printf(" proc");
            if (flags & SYM_DATA)
                printf(" data");
            printf(": (%04x:%08"PRIx32") = %s\n", (int)s, translateint32_t(o), buf);
            if (rdf_version == 1)
                length -= ll + 6;
            break;

        case RDFREC_DLL:       /* DLL and Module records */
        case RDFREC_MODNAME:
            ll = 0;
            if (rdf_version == 1) {
                do {
                    nasm_read(&buf[ll], 1, infile);
                } while (buf[ll++]);
            } else {
                for (; ll < reclen; ll++)
                    nasm_read(&buf[ll], 1, infile);
            }
            if (t == 4)
                printf("  dll: %s\n", buf);
            else
                printf("  module: %s\n", buf);
            if (rdf_version == 1)
                length -= ll + 1;
            break;

        case RDFREC_BSS:       /* BSS reservation */
            nasm_read(&ll, 4, infile);
            printf("  bss reservation: %08"PRIx32" bytes\n", translateint32_t(ll));
            if (rdf_version == 1)
                length -= 5;
            if (rdf_version > 1 && reclen != 4)
                printf("    warning: reclen != 4\n");
            break;

        case RDFREC_COMMON:{
                uint16_t seg, align;
                uint32_t size;

                nasm_read(&seg, 2, infile);
                nasm_read(&size, 4, infile);
                nasm_read(&align, 2, infile);
                for (ll = 0; ll < reclen - 8; ll++)
                    nasm_read(buf + ll, 1, infile);
                printf("  common: segment %04x = %s, %"PRId32":%d\n",
                       translateint16_t(seg), buf, translateint32_t(size),
                       translateint16_t(align));
                break;
            }

        default:
            printf("  unrecognized record (type %d", (int)t);
            if (rdf_version > 1) {
                printf(", length %d", (int)reclen);
                fseek(infile, reclen, SEEK_CUR);
            } else
                length--;
            printf(")\n");
        }
        if (rdf_version != 1)
            length -= 2 + reclen;
    }
}

int main(int argc, char **argv)
{
    char id[7];
    int32_t l;
    uint16_t s;
    int verbose = 0;
    int32_t offset;
    int foundnullsegment = 0;
    int version;
    int32_t segmentcontentlength = 0;
    int nsegments = 0;
    int32_t headerlength = 0;
    int32_t objectlength = 0;

    printf("RDOFF dump utility, version %s\n", PROGRAM_VERSION);
    printf("RDOFF2 revision %s\n", RDOFF2_REVISION);
    puts("Copyright (c) 1996,99 Julian R Hall\n"
         "Improvements and fixes (c) 2002-2004 RET & COM Research.");

    if (argc < 2) {
        fputs("Usage: rdfdump [-v] <filename>\n", stderr);
        exit(1);
    }

    rdoff_init();

    if (!strcmp(argv[1], "-v")) {
        verbose = 1;
        if (argc < 3) {
            fputs("required parameter missing\n", stderr);
            exit(1);
        }
        argv++;
    }

    infile = fopen(argv[1], "rb");
    if (!infile) {
        fprintf(stderr, "rdfdump: Could not open %s\n", argv[1]);
        exit(1);
    }

    nasm_read(id, 6, infile);
    if (strncmp(id, "RDOFF", 5)) {
        fputs("rdfdump: File does not contain valid RDOFF header\n",
              stderr);
        exit(1);
    }

    printf("File %s: RDOFF version %c\n\n", argv[1], id[5]);
    if (id[5] < '1' || id[5] > '2') {
        fprintf(stderr, "rdfdump: unknown RDOFF version '%c'\n", id[5]);
        exit(1);
    }
    version = id[5] - '0';

    if (version > 1) {
        nasm_read(&l, 4, infile);
        objectlength = translateint32_t(l);
        printf("Object content size: %"PRId32" bytes\n", objectlength);
    }

    nasm_read(&l, 4, infile);
    headerlength = translateint32_t(l);
    printf("Header (%"PRId32" bytes):\n", headerlength);
    print_header(headerlength, version);

    if (version == 1) {
        nasm_read(&l, 4, infile);
        l = translateint32_t(l);
        printf("\nText segment length = %"PRId32" bytes\n", l);
        offset = 0;
        while (l--) {
            nasm_read(id, 1, infile);
            if (verbose) {
                if (offset % 16 == 0)
                    printf("\n%08"PRIx32" ", offset);
                printf(" %02x", (int)(uint8_t)id[0]);
                offset++;
            }
        }
        if (verbose)
            printf("\n\n");

        nasm_read(&l, 4, infile);
        l = translateint32_t(l);
        printf("Data segment length = %"PRId32" bytes\n", l);

        if (verbose) {
            offset = 0;
            while (l--) {
                nasm_read(id, 1, infile);
                if (offset % 16 == 0)
                    printf("\n%08"PRIx32" ", offset);
                printf(" %02x", (int)(uint8_t)id[0]);
                offset++;
            }
            printf("\n");
        }
    } else {
        do {
            nasm_read(&s, 2, infile);
            s = translateint16_t(s);
            if (!s) {
                printf("\nNULL segment\n");
                foundnullsegment = 1;
                break;
            }
            printf("\nSegment:\n  Type   = %04X (%s)\n", (int)s,
                   translatesegmenttype(s));
            nsegments++;

            nasm_read(&s, 2, infile);
            printf("  Number = %04X\n", (int)translateint16_t(s));
            nasm_read(&s, 2, infile);
            printf("  Resrvd = %04X\n", (int)translateint16_t(s));
            nasm_read(&l, 4, infile);
            l = translateint32_t(l);
            printf("  Length = %"PRId32" bytes\n", l);
            segmentcontentlength += l;

            offset = 0;
            while (l--) {
                nasm_read(id, 1, infile);
                if (verbose) {
                    if (offset % 16 == 0)
                        printf("\n%08"PRIx32" ", offset);
                    printf(" %02x", (int)(uint8_t)id[0]);
                    offset++;
                }
            }
            if (verbose)
                printf("\n");
        } while (!feof(infile));
        if (!foundnullsegment)
            printf("\nWarning: unexpected end of file - "
                   "NULL segment not found\n");

        printf("\nTotal number of segments: %d\n", nsegments);
        printf("Total segment content length: %"PRId32" bytes\n",
               segmentcontentlength);

        /* calculate what the total object content length should have been */
        l = segmentcontentlength + 10 * (nsegments + 1) + headerlength + 4;
        if (l != objectlength)
            printf("Warning: actual object length (%"PRId32") != "
                   "stored object length (%"PRId32")\n", l, objectlength);
    }
    fclose(infile);
    return 0;
}
