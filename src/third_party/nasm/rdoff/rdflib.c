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

/* rdflib - manipulate RDOFF library files (.rdl) */

/*
 * an rdoff library is simply a sequence of RDOFF object files, each
 * preceded by the name of the module, an ASCII string of up to 255
 * characters, terminated by a zero.
 *
 * When a library is being created, special signature block is placed
 * in the beginning of the file. It is a string 'RDLIB' followed by a
 * version number, then int32_t content size and a int32_t time stamp.
 * The module name of the signature block is '.sig'.
 *
 *
 * There may be an optional directory placed on the end of the file.
 * The format of the directory will be 'RDLDD' followed by a version
 * number, followed by the length of the directory, and then the
 * directory, the format of which has not yet been designed.
 * The module name of the directory must be '.dir'.
 *
 * All module names beginning with '.' are reserved for possible future
 * extensions. The linker ignores all such modules, assuming they have
 * the format of a six uint8_t type & version identifier followed by int32_t
 * content size, followed by data.
 */

#include "compiler.h"
#include "rdfutils.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>

/* functions supported:
 *   create a library	(no extra operands required)
 *   add a module from a library (requires filename and name to give mod.)
 *   replace a module in a library (requires given name and filename)
 *   delete a module from a library (requires given name)
 *   extract a module from the library (requires given name and filename)
 *   list modules
 */

const char *usage =
    "usage:\n"
    "  rdflib x libname [extra operands]\n\n"
    "  where x is one of:\n"
    "    c - create library\n"
    "    a - add module (operands = filename module-name)\n"
    "    x - extract               (module-name filename)\n"
    "    r - replace               (module-name filename)\n"
    "    d - delete                (module-name)\n" "    t - list\n";

/* Library signature */
const char *rdl_signature = "RDLIB2", *sig_modname = ".sig";

char **_argv;

#define _ENDIANNESS 0           /* 0 for little, 1 for big */

static void int32_ttolocal(int32_t *l)
{
#if _ENDIANNESS
    uint8_t t;
    uint8_t *p = (uint8_t *)l;

    t = p[0];
    p[0] = p[3];
    p[3] = t;
    t = p[1];
    p[1] = p[2];
    p[2] = p[1];
#else
    (void)l;             /* placate optimizers */
#endif
}

static char copybytes(FILE * fp, FILE * fp2, int n)
{
    int i, t = 0;

    for (i = 0; i < n; i++) {
        t = fgetc(fp);
        if (t == EOF) {
            fprintf(stderr, "rdflib: premature end of file in '%s'\n",
                    _argv[2]);
            exit(1);
        }
        if (fp2)
            if (fputc(t, fp2) == EOF) {
                fprintf(stderr, "rdflib: write error\n");
                exit(1);
            }
    }
    return (char)t;             /* return last char read */
}

static int32_t copyint32_t(FILE * fp, FILE * fp2)
{
    int32_t l;
    int i, t;
    uint8_t *p = (uint8_t *)&l;

    for (i = 0; i < 4; i++) {   /* skip magic no */
        t = fgetc(fp);
        if (t == EOF) {
            fprintf(stderr, "rdflib: premature end of file in '%s'\n",
                    _argv[2]);
            exit(1);
        }
        if (fp2)
            if (fputc(t, fp2) == EOF) {
                fprintf(stderr, "rdflib: write error\n");
                exit(1);
            }
        *p++ = t;
    }
    int32_ttolocal(&l);
    return l;
}

int main(int argc, char **argv)
{
    FILE *fp, *fp2 = NULL, *fptmp;
    char *p, buf[256], c;
    int i;
    int32_t l;
    time_t t;
    char rdbuf[10];

    _argv = argv;

    if (argc < 3 || !strncmp(argv[1], "-h", 2)
        || !strncmp(argv[1], "--h", 3)) {
        fputs(usage, stdout);
        exit(1);
    }

    rdoff_init();

    switch (argv[1][0]) {
    case 'c':                  /* create library */
        fp = fopen(argv[2], "wb");
        if (!fp) {
            fprintf(stderr, "rdflib: could not open '%s'\n", argv[2]);
            perror("rdflib");
            exit(1);
        }
        nasm_write(sig_modname, strlen(sig_modname) + 1, fp);
        nasm_write(rdl_signature, strlen(rdl_signature), fp);
	t = time(NULL);
        fwriteint32_t(t, fp);
        fclose(fp);
        break;

    case 'a':                  /* add module */
        if (argc < 5) {
            fprintf(stderr, "rdflib: required parameter missing\n");
            exit(1);
        }
        fp = fopen(argv[2], "ab");
        if (!fp) {
            fprintf(stderr, "rdflib: could not open '%s'\n", argv[2]);
            perror("rdflib");
            exit(1);
        }

        fp2 = fopen(argv[3], "rb");
        if (!fp2) {
            fprintf(stderr, "rdflib: could not open '%s'\n", argv[3]);
            perror("rdflib");
            exit(1);
        }

        p = argv[4];
        do {
            if (fputc(*p, fp) == EOF) {
                fprintf(stderr, "rdflib: write error\n");
                exit(1);
            }
        } while (*p++);

        while (!feof(fp2)) {
            i = fgetc(fp2);
            if (i == EOF) {
                break;
            }

            if (fputc(i, fp) == EOF) {
                fprintf(stderr, "rdflib: write error\n");
                exit(1);
            }
        }
        fclose(fp2);
        fclose(fp);
        break;

    case 'x':
        if (argc < 5) {
            fprintf(stderr, "rdflib: required parameter missing\n");
            exit(1);
        }
        break;
    case 't':
        fp = fopen(argv[2], "rb");
        if (!fp) {
            fprintf(stderr, "rdflib: could not open '%s'\n", argv[2]);
            perror("rdflib");
            exit(1);
        }

        fp2 = NULL;
        while (!feof(fp)) {
            /* read name */
            p = buf;
            while ((*(p++) = (char)fgetc(fp)))
                if (feof(fp))
                    break;

            if (feof(fp))
                break;

            fp2 = NULL;
            if (argv[1][0] == 'x') {
                /* check against desired name */
                if (!strcmp(buf, argv[3])) {
                    fp2 = fopen(argv[4], "wb");
                    if (!fp2) {
                        fprintf(stderr, "rdflib: could not open '%s'\n",
                                argv[4]);
                        perror("rdflib");
                        exit(1);
                    }
                }
            } else
                printf("%-40s ", buf);

            /* step over the RDOFF file, extracting type information for
             * the listing, and copying it if fp2 != NULL */

            if (buf[0] == '.') {

                if (argv[1][0] == 't')
                    for (i = 0; i < 6; i++)
                        printf("%c", copybytes(fp, fp2, 1));
                else
                    copybytes(fp, fp2, 6);

                l = copyint32_t(fp, fp2);

                if (argv[1][0] == 't')
                    printf("   %"PRId32" bytes content\n", l);

                copybytes(fp, fp2, l);
            } else if ((c = copybytes(fp, fp2, 6)) >= '2') {    /* version 2 or above */
                l = copyint32_t(fp, fp2);

                if (argv[1][0] == 't')
                    printf("RDOFF%c   %"PRId32" bytes content\n", c, l);
                copybytes(fp, fp2, l);  /* entire object */
            } else {
                if (argv[1][0] == 't')
                    printf("RDOFF1\n");
                /*
                 * version 1 object, so we don't have an object content
                 * length field.
                 */
                copybytes(fp, fp2, copyint32_t(fp, fp2));  /* header */
                copybytes(fp, fp2, copyint32_t(fp, fp2));  /* text */
                copybytes(fp, fp2, copyint32_t(fp, fp2));  /* data */
            }

            if (fp2)
                break;
        }
        fclose(fp);
        if (fp2)
            fclose(fp2);
        else if (argv[1][0] == 'x') {
            fprintf(stderr, "rdflib: module '%s' not found in '%s'\n",
                    argv[3], argv[2]);
            exit(1);
        }
        break;

    case 'r':                  /* replace module */
        argc--;
        /* fall through */

    case 'd':                  /* delete module */
        if (argc < 4) {
            fprintf(stderr, "rdflib: required parameter missing\n");
            exit(1);
        }

        fp = fopen(argv[2], "rb");
        if (!fp) {
            fprintf(stderr, "rdflib: could not open '%s'\n", argv[2]);
            perror("rdflib");
            exit(1);
        }

        if (argv[1][0] == 'r') {
            fp2 = fopen(argv[4], "rb");
            if (!fp2) {
                fprintf(stderr, "rdflib: could not open '%s'\n", argv[4]);
                perror("rdflib");
                exit(1);
            }
        }

        fptmp = tmpfile();
        if (!fptmp) {
            fprintf(stderr, "rdflib: could not open temporary file\n");
            perror("rdflib");
            exit(1);
        }

        /* copy library into temporary file */
        fseek(fp, 0, SEEK_END); /* get file length */
        l = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        copybytes(fp, fptmp, l);
        rewind(fptmp);
        if (freopen(argv[2], "wb", fp) == NULL) {
            fprintf(stderr, "rdflib: could not reopen '%s'\n", argv[2]);
            perror("rdflib");
            exit(1);
        }

        while (!feof(fptmp)) {
            /* read name */
            p = buf;
            while ((*(p++) = (char)fgetc(fptmp)))
                if (feof(fptmp))
                    break;

            if (feof(fptmp))
                break;

            /* check against desired name */
            if (!strcmp(buf, argv[3])) {
                if (fread(p = rdbuf, 1, sizeof(rdbuf), fptmp) < 10) {
                    nasm_fatal("short read on input");
                }
                l = *(int32_t *)(p + 6);
                fseek(fptmp, l, SEEK_CUR);
                break;
            } else {
                nasm_write(buf, strlen(buf) + 1, fp);    /* module name */
                if ((c = copybytes(fptmp, fp, 6)) >= '2') {
                    l = copyint32_t(fptmp, fp);    /* version 2 or above */
                    copybytes(fptmp, fp, l);    /* entire object */
                }
            }
        }

        if (argv[1][0] == 'r') {
            /* copy new module into library */
            p = argv[3];
            do {
                if (fputc(*p, fp) == EOF) {
                    fprintf(stderr, "rdflib: write error\n");
                    exit(1);
                }
            } while (*p++);

            while (!feof(fp2)) {
                i = fgetc(fp2);
                if (i == EOF) {
                    break;
                }
                if (fputc(i, fp) == EOF) {
                    fprintf(stderr, "rdflib: write error\n");
                    exit(1);
                }
            }
            fclose(fp2);
        }

        /* copy rest of library if any */
        while (!feof(fptmp)) {
            i = fgetc(fptmp);
            if (i == EOF) {
                break;
            }

            if (fputc(i, fp) == EOF) {
                fprintf(stderr, "rdflib: write error\n");
                exit(1);
            }
        }

        fclose(fp);
        fclose(fptmp);
        break;

    default:
        fprintf(stderr, "rdflib: command '%c' not recognized\n",
                argv[1][0]);
        exit(1);
    }
    return 0;
}
