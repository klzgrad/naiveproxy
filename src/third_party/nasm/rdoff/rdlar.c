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
 * rdlar.c - new librarian/archiver for RDOFF2.
 */

#include "compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "rdlar.h"

#define PROGRAM_VERSION "0.1"

/** Constants **/
const char commands[] = "adnrtx";
const char modifiers[] = "cflouvV";

/** Global variables **/
char *progname = "rdlar";
char **_argv = NULL;
struct {
    bool createok;
    bool usefname;
    bool align;
    bool odate;
    bool fresh;
    int verbose;
} options = {
0, 0, 0, 0, 0, 0};

#define _ENDIANNESS 0           /* 0 for little, 1 for big */

/*
 * Convert int32_t to little endian (if we were compiled on big-endian machine)
 */
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
#endif
}

/*
 * Print version information
 */
void show_version(void)
{
    puts("New RDOFF2 librarian/archiver, version " PROGRAM_VERSION);
}

/*
 * Print usage instructions
 */
void usage(void)
{
    printf("Usage:  %s [-]{%s}[%s] libfile [module-name] [files]\n",
           progname, commands, modifiers);
    puts(" commands:\n"
         "  a            - add module(s) to the library\n"
         "  d            - delete module(s) from the library\n"
         "  n            - create the library\n"
         "  r            - replace module(s)\n"
         "  t            - display contents of library\n"
         "  x            - extract module(s)\n"
         " command specific modifiers:\n"
         "  o            - preserve original dates\n"
         "  u            - only replace modules that are newer than library contents\n"
         " generic modifiers:\n"
         "  c            - do not warn if the library had to be created\n"
         "  f            - use file name as a module name\n"
         "  v            - be verbose\n"
         "  V            - display version information");
}

/*
 * Print an error message and exit
 */
void error_exit(int errcode, bool useperror, const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "%s: ", progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    putc('\n', stderr);
    if (useperror)
        perror(progname);
    exit(errcode);
}

/*
 * Fill in and write a header
 */
void put_header(struct rdlm_hdr *hdr, FILE * libfp, char *modname)
{
    int n = 0;

    hdr->hdrsize = sizeof(*hdr);
    if (modname)
        hdr->hdrsize += (n = strlen(modname) + 1);
    if (libfp == NULL)
        return;
    if (fwrite(hdr, 1, sizeof(*hdr), libfp) != sizeof(*hdr) ||
        (modname && (fwrite(modname, 1, n, libfp) != n)))
        error_exit(3, true, "could not write header");
}

/*
 * Copy n bytes from one file to another and return last character read.
 */
char copybytes(FILE * fp, FILE * fp2, int n)
{
    int i, t = 0;

    for (i = 0; i < n; i++) {
        t = fgetc(fp);
        if (t == EOF)
            error_exit(1, false, "premature end of file in '%s'",
                       _argv[2]);
        if (fp2)
            if (fputc(t, fp2) == EOF)
                error_exit(1, false, "write error");
    }
    return (char)t;
}

/*
 * Copy uint32_t from one file to another.
 * Return local presentation of int32_t.
 */
int32_t copyint32_t(FILE * fp, FILE * fp2)
{
    int32_t l;
    int i, t;
    uint8_t *p = (uint8_t *)&l;

    for (i = 0; i < 4; i++) {
        t = fgetc(fp);
        if (t == EOF)
            error_exit(1, false, "premature end of file in '%s'",
                       _argv[2]);
        if (fp2)
            if (fputc(t, fp2) == EOF)
                error_exit(1, false, "write error");
        *p++ = t;
    }
    int32_ttolocal(&l);
    return l;
}

/*
 * Create a new library
 */
int create_library(char *libname)
{
    FILE *libfp;
    struct rdlm_hdr hdr;

    hdr.magic = RDLAMAG;
    hdr.hdrsize = 0;
    hdr.date = time(NULL);
    hdr.owner = getuid();
    hdr.group = getgid();
    hdr.mode = umask(022);
    hdr.size = 0;

    libfp = fopen(libname, "wb");
    if (!libfp)
        error_exit(1, true, "could not open '%s'\n", libname);

    /* Write library header */
    put_header(&hdr, libfp, NULL);

    fclose(libfp);
    return true;
}

/*
 * Add a module to the library
 */
int add_module(FILE * libfp, const char *fname, char *modname)
{
    FILE *modfp;
    struct rdlm_hdr hdr = { RDLMMAG, 0, 0, 0, 0, 0, 0 };
    struct stat finfo;
    int i;

    if (options.verbose)
        fprintf(stderr, "adding module %s\n", modname);

    /* Initialize some fields in the module header */
    if (stat(fname, &finfo) < 0)
        error_exit(1, true, "could not stat '%s'", fname);
    hdr.date = finfo.st_mtime;
    hdr.owner = finfo.st_uid;
    hdr.group = finfo.st_gid;
    hdr.size = finfo.st_size;

    modfp = fopen(fname, "rb");
    if (!modfp)
        error_exit(1, true, "could not open '%s'", fname);

    /* Write module header */
    put_header(&hdr, libfp, modname);

    /* Put the module itself */
    while (!feof(modfp)) {
        i = fgetc(modfp);
        if (i == EOF)
            break;
        if (fputc(i, libfp) == EOF)
            error_exit(1, false, "write error");
    }

    fclose(modfp);
    return true;
}

/*
 * Main
 */
int main(int argc, char **argv)
{
    FILE *libfp, *tmpfp, *modfp = NULL;
    struct stat finfo;
    struct rdlm_hdr hdr;
    char buf[MAXMODNAMELEN], *p = NULL;
    char c;
    int i;

    progname = argv[0];
    _argv = argv;

    if (argc < 2) {
        usage();
        exit(1);
    }

    rdoff_init();

    /* Check whether some modifiers were specified */
    for (i = 1; i < strlen(argv[1]); i++) {
        switch (c = argv[1][i]) {
        case 'c':
            options.createok = true;
            break;
        case 'f':
            options.usefname = true;
            break;
        case 'l':
            options.align = true;
            break;
        case 'o':
            options.odate = true;
            break;
        case 'u':
            options.fresh = true;
            break;
        case 'v':
            options.verbose++;
            break;
        case 'V':
            show_version();
            exit(0);
        default:
            if (strchr(commands, c) == NULL)
                error_exit(2, false, "invalid command or modifier '%c'",
                           c);
        }
    }

    if (argc < 3)
        error_exit(2, false, "missing library name");

    /* Process the command */
    if (argv[1][0] == '-')
        argv[1]++;
    switch (c = argv[1][0]) {
    case 'a':                  /* add a module */
        if (argc < 4 || (!options.usefname && argc != 5))
            error_exit(2, false, "invalid number of arguments");

        /* Check if a library already exists. If not - create it */
        if (access(argv[2], F_OK) < 0) {
            if (!options.createok)
                fprintf(stderr, "creating library %s\n", argv[2]);
            create_library(argv[2]);
        }

        libfp = fopen(argv[2], "ab");
        if (!libfp)
            error_exit(1, true, "could not open '%s'", argv[2]);

        if (!options.usefname)
            add_module(libfp, argv[4], argv[3]);
        else
            for (i = 3; i < argc; i++)
                add_module(libfp, argv[i], argv[i]);

        fclose(libfp);
        break;

    case 'n':                  /* create library */
        create_library(argv[2]);
        break;

    case 'x':                  /* extract module(s) */
        if (!options.usefname)
            argc--;
        if (argc < 4)
            error_exit(2, false, "required parameter missing");
        p = options.usefname ? argv[3] : argv[4];
    case 't':                  /* list library contents */
        libfp = fopen(argv[2], "rb");
        if (!libfp)
            error_exit(1, true, "could not open '%s'\n", argv[2]);

        /* Read library header */
        if (fread(&hdr, 1, sizeof(hdr), libfp) != sizeof(hdr) ||
            hdr.magic != RDLAMAG)
            error_exit(1, false, "invalid library format");

        /* Walk through the library looking for requested module */
        while (!feof(libfp)) {
            /* Read module header */
            i = fread(&hdr, 1, sizeof(hdr), libfp);
            if (feof(libfp))
                break;
            if (i != sizeof(hdr) || hdr.magic != RDLMMAG)
                error_exit(1, false, "invalid module header");
            /* Read module name */
            i = hdr.hdrsize - sizeof(hdr);
            if (i > sizeof(buf) || fread(buf, 1, i, libfp) != i)
                error_exit(1, false, "invalid module name");
            if (c == 'x') {
                /* Check against desired name */
                if (!strcmp(buf, argv[3])) {
                    if (options.verbose)
                        fprintf(stderr,
                                "extracting module %s to file %s\n", buf,
                                p);
                    modfp = fopen(p, "wb");
                    if (!modfp)
                        error_exit(1, true, "could not open '%s'", p);
                }
            } else {
                printf("%-40s ", buf);
                if (options.verbose) {
                    printf("%ld bytes", hdr.size);
                }
                putchar('\n');
            }

            copybytes(libfp, modfp, hdr.size);
            if (modfp)
                break;
        }

        fclose(libfp);
        if (modfp)
            fclose(modfp);
        else if (c == 'x')
            error_exit(1, false, "module '%s' not found in '%s'",
                       argv[3], argv[2]);
        break;

    case 'r':                  /* replace module(s) */
        argc--;
        if (stat(argv[4], &finfo) < 0)
            error_exit(1, true, "could not stat '%s'", argv[4]);
    case 'd':                  /* delete module(s) */
        if (argc < 4)
            error_exit(2, false, "required parameter missing");

        libfp = fopen(argv[2], "rb");
        if (!libfp)
            error_exit(1, true, "could not open '%s'", argv[2]);

        /* Copy the library into a temporary file */
        tmpfp = tmpfile();
        if (!tmpfp)
            error_exit(1, true, "could not open temporary file");

        stat(argv[2], &finfo);
        copybytes(libfp, tmpfp, finfo.st_size);
        rewind(tmpfp);
        freopen(argv[2], "wb", libfp);

        /* Read library header and write it to a new file */
        if (fread(&hdr, 1, sizeof(hdr), tmpfp) != sizeof(hdr) ||
            hdr.magic != RDLAMAG)
            error_exit(1, false, "invalid library format");
        put_header(&hdr, libfp, NULL);

        /* Walk through the library looking for requested module */
        while (!feof(tmpfp)) {
            /* Read module header */
            if (fread(&hdr, 1, sizeof(hdr), tmpfp) != sizeof(hdr) ||
                hdr.magic != RDLMMAG)
                error_exit(1, false, "invalid module header");
            /* Read module name */
            i = hdr.hdrsize - sizeof(hdr);
            if (i > sizeof(buf) || fread(buf, 1, i, tmpfp) != i)
                error_exit(1, false, "invalid module name");
            /* Check against desired name */
            if (!strcmp(buf, argv[3]) &&
                (c == 'd' || !options.odate
                 || finfo.st_mtime <= hdr.date)) {
                if (options.verbose)
                    fprintf(stderr, "deleting module %s\n", buf);
                fseek(tmpfp, hdr.size, SEEK_CUR);
                break;
            } else {
                put_header(&hdr, libfp, buf);
                copybytes(tmpfp, libfp, hdr.size);
            }
        }

        if (c == 'r') {
            /* Copy new module into library */
            p = options.usefname ? argv[4] : argv[3];
            add_module(libfp, argv[4], p);
        }

        /* Copy rest of library if any */
        while (!feof(tmpfp)) {
            if ((i = fgetc(tmpfp)) == EOF)
                break;

            if (fputc(i, libfp) == EOF)
                error_exit(1, false, "write error");
        }

        fclose(libfp);
        fclose(tmpfp);
        break;

    default:
        error_exit(2, false, "invalid command '%c'\n", c);
    }

    return 0;
}
