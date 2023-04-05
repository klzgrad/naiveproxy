/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1996-2017 The NASM Authors - All Rights Reserved
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

#include "file.h"

void nasm_read(void *ptr, size_t size, FILE *f)
{
    size_t n = fread(ptr, 1, size, f);
    if (ferror(f)) {
        nasm_fatal("unable to read input: %s", strerror(errno));
    } else if (n != size || feof(f)) {
        nasm_fatal("fatal short read on input");
    }
}

void nasm_write(const void *ptr, size_t size, FILE *f)
{
    size_t n = fwrite(ptr, 1, size, f);
    if (n != size || ferror(f) || feof(f))
        nasm_fatal("unable to write output: %s", strerror(errno));
}

void fwriteint16_t(uint16_t data, FILE * fp)
{
    data = cpu_to_le16(data);
    nasm_write(&data, 2, fp);
}

void fwriteint32_t(uint32_t data, FILE * fp)
{
    data = cpu_to_le32(data);
    nasm_write(&data, 4, fp);
}

void fwriteint64_t(uint64_t data, FILE * fp)
{
    data = cpu_to_le64(data);
    nasm_write(&data, 8, fp);
}

void fwriteaddr(uint64_t data, int size, FILE * fp)
{
    data = cpu_to_le64(data);
    nasm_write(&data, size, fp);
}

void fwritezero(off_t bytes, FILE *fp)
{
    size_t blksize;

#ifdef os_ftruncate
    if (bytes >= BUFSIZ && !ferror(fp) && !feof(fp)) {
	off_t pos = ftello(fp);
	if (pos != (off_t)-1) {
            off_t end = pos + bytes;
	    if (!fflush(fp) && !os_ftruncate(fileno(fp), end)) {
                fseeko(fp, 0, SEEK_END);
                pos = ftello(fp);
                if (pos != (off_t)-1)
                    bytes = end - pos; /* This SHOULD be zero */
            }
	}
    }
#endif

    while (bytes > 0) {
	blksize = (bytes < ZERO_BUF_SIZE) ? bytes : ZERO_BUF_SIZE;

	nasm_write(zero_buffer, blksize, fp);
	bytes -= blksize;
    }
}

#ifdef _WIN32

/*
 * On Windows, we want to use _wfopen(), as fopen() has a much smaller limit
 * on the path length that it supports.
 *
 * Previously we tried to prefix the path name with \\?\ in order to
 * let the Windows kernel know that we are not limited to PATH_MAX
 * characters, but it breaks relative paths among other things, and
 * apparently Windows 10 contains a registry option to override this
 * limit anyway. One day maybe they will even implement UTF-8 as byte
 * characters so we can use the standard file API even on this OS.
 */

os_filename os_mangle_filename(const char *filename)
{
    mbstate_t ps;
    size_t wclen;
    wchar_t *buf;
    const char *p;

    /*
     * Note: mbsrtowcs() return (size_t)-1 on error, otherwise
     * the length of the string *without* final NUL in wchar_t
     * units. Thus we add 1 for the final NUL; the error value
     * now becomes 0.
     */
    memset(&ps, 0, sizeof ps);  /* Begin in the initial state */
    p = filename;
    wclen = mbsrtowcs(NULL, &p, 0, &ps) + 1;
    if (!wclen)
        return NULL;

    buf = nasm_malloc(wclen * sizeof(wchar_t));

    memset(&ps, 0, sizeof ps);  /* Begin in the initial state */
    p = filename;
    if (mbsrtowcs(buf, &p, wclen, &ps) + 1 != wclen || p) {
        nasm_free(buf);
        return NULL;
    }

    return buf;
}

#endif

void nasm_set_binary_mode(FILE *f)
{
	os_set_binary_mode(f);
}

FILE *nasm_open_read(const char *filename, enum file_flags flags)
{
    FILE *f = NULL;
    os_filename osfname;

    osfname = os_mangle_filename(filename);
    if (osfname) {
        os_fopenflag fopen_flags[4];
        memset(fopen_flags, 0, sizeof fopen_flags);

        fopen_flags[0] = 'r';
        fopen_flags[1] = (flags & NF_TEXT) ? 't' : 'b';

#if defined(__GLIBC__) || defined(__linux__)
        /*
         * Try to open this file with memory mapping for speed, unless we are
         * going to do it "manually" with nasm_map_file()
         */
        if (!(flags & NF_FORMAP))
            fopen_flags[2] = 'm';
#endif

        while (true) {
            f = os_fopen(osfname, fopen_flags);
            if (f || errno != EINVAL || !fopen_flags[2])
                break;

            /* We got EINVAL but with 'm'; try again without 'm' */
            fopen_flags[2] = '\0';
        }

        os_free_filename(osfname);
    }

    if (!f && (flags & NF_FATAL))
        nasm_fatalf(ERR_NOFILE, "unable to open input file: `%s': %s",
                    filename, strerror(errno));

    return f;
}

FILE *nasm_open_write(const char *filename, enum file_flags flags)
{
    FILE *f = NULL;
    os_filename osfname;

    osfname = os_mangle_filename(filename);
    if (osfname) {
        os_fopenflag fopen_flags[3];

        fopen_flags[0] = 'w';
        fopen_flags[1] = (flags & NF_TEXT) ? 't' : 'b';
        fopen_flags[2] = '\0';

        f = os_fopen(osfname, fopen_flags);
        os_free_filename(osfname);
    }

    if (!f && (flags & NF_FATAL))
        nasm_fatalf(ERR_NOFILE, "unable to open output file: `%s': %s",
                    filename, strerror(errno));

    switch (flags & NF_BUF_MASK) {
    case NF_IONBF:
        setvbuf(f, NULL, _IONBF, 0);
        break;
    case NF_IOLBF:
        setvbuf(f, NULL, _IOLBF, 0);
        break;
    case NF_IOFBF:
        setvbuf(f, NULL, _IOFBF, 0);
        break;
    default:
        break;
    }

    return f;
}

/* The appropriate "rb" strings for os_fopen() */
static const os_fopenflag fopenflags_rb[3] = { 'r', 'b', 0 };

/*
 * Report the existence of a file
 */
bool nasm_file_exists(const char *filename)
{
#ifndef os_access
    FILE *f;
#endif
    os_filename osfname;
    bool exists;

    osfname = os_mangle_filename(filename);
    if (!osfname)
        return false;

#ifdef os_access
    exists = os_access(osfname, R_OK) == 0;
#else
    f = os_fopen(osfname, fopenflags_rb);
    exists = f != NULL;
    if (f)
        fclose(f);
#endif

    os_free_filename(osfname);
    return exists;
}

/*
 * Report the file size of an open file.  This MAY move the file pointer.
 */
off_t nasm_file_size(FILE *f)
{
    off_t where, end;
    os_struct_stat st;

    if (!os_fstat(fileno(f), &st) && S_ISREG(st.st_mode))
        return st.st_size;

    /* Do it the hard way... this tests for seekability */

    if (fseeko(f, 0, SEEK_CUR))
        goto fail;              /* Not seekable, don't even try */

    where = ftello(f);
    if (where == (off_t)-1)
        goto fail;

    if (fseeko(f, 0, SEEK_END))
        goto fail;

    end = ftello(f);
    if (end == (off_t)-1)
        goto fail;

    /*
     * Move the file pointer back. If this fails, this is probably
     * not a plain file.
     */
    if (fseeko(f, where, SEEK_SET))
        goto fail;

    return end;

fail:
    return -1;
}

/*
 * Report file size given pathname
 */
off_t nasm_file_size_by_path(const char *pathname)
{
    os_filename osfname;
    off_t len = -1;
    os_struct_stat st;
    FILE *fp;

    osfname = os_mangle_filename(pathname);

    if (!os_stat(osfname, &st) && S_ISREG(st.st_mode))
        len = st.st_size;

    fp = os_fopen(osfname, fopenflags_rb);
    if (fp) {
        len = nasm_file_size(fp);
        fclose(fp);
    }

    return len;
}

/*
 * Report the timestamp on a file, returns true if successful
 */
bool nasm_file_time(time_t *t, const char *pathname)
{
#ifdef os_stat
    os_filename osfname;
    os_struct_stat st;
    bool rv = false;

    osfname = os_mangle_filename(pathname);
    if (!osfname)
        return false;

    rv = !os_stat(osfname, &st);
    *t = st.st_mtime;
    os_free_filename(osfname);

    return rv;
#else
    return false;               /* No idea how to do this on this OS */
#endif
}
