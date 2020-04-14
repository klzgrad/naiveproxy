/* ----------------------------------------------------------------------- *
 *
 *   Copyright 2016 The NASM Authors - All Rights Reserved
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
 * realpath.c	As system-independent as possible implementation of realpath()
 */

#include "compiler.h"

#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif

#include "nasmlib.h"

#ifdef HAVE_CANONICALIZE_FILE_NAME

/*
 * GNU-specific, but avoids the realpath(..., NULL)
 * portability problem if it exists.
 */
char *nasm_realpath(const char *rel_path)
{
    char *rp = canonicalize_file_name(rel_path);
    return rp ? rp : nasm_strdup(rel_path);
}

#elif defined(HAVE_REALPATH)

/*
 * POSIX.1-2008 defines realpath(..., NULL); POSIX.1-2001 doesn't guarantee
 * that a NULL second argument is supported.
 */

char *nasm_realpath(const char *rel_path)
{
    char *rp;

    rp = realpath(rel_path, NULL);

    /* Not all implemetations of realpath() support a NULL second argument */
    if (!rp && errno == EINVAL) {
        long path_max = -1;
        char *rp;

#if defined(HAVE_PATHCONF) && defined(_PC_PATH_MAX)
        path_max = pathconf(rel_path, _PC_PATH_MAX); /* POSIX */
#endif

        if (path_max < 0) {
#ifdef PATH_MAX
            path_max = PATH_MAX;    /* SUSv2 */
#elif defined(MAXPATHLEN)
            path_max = MAXPATHLEN;  /* Solaris */
#else
            path_max = 65536;    /* Crazily high, we hope */
#endif
        }

        rp = nasm_malloc(path_max);

        if (!realpath(rel_path, rp)) {
            nasm_free(rp);
            rp = NULL;
        } else {
            /* On some systems, pathconf() can return a very large value */

            rp[path_max - 1] = '\0'; /* Just in case overrun is possible */
            rp = nasm_realloc(rp, strlen(rp) + 1);
        }
    }

    return rp ? rp : nasm_strdup(rel_path);
}

#elif defined(HAVE__FULLPATH)

/*
 * win32/win64 API
 */

char *nasm_realpath(const char *rel_path)
{
    char *rp = _fullpath(NULL, rel_path, 0);
    return rp ? rp : nasm_strdup(rel_path);
}

#else

/*
 * There is nothing we know how to do here, so hope it just works anyway.
 */

char *nasm_realpath(const char *rel_path)
{
    return nasm_strdup(rel_path);
}

#endif
