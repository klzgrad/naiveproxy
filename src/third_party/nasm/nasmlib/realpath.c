/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 2016 The NASM Authors - All Rights Reserved */

/*
 * realpath.c	As system-independent as possible implementation of realpath()
 */

#include "compiler.h"

#include <errno.h>
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

    /* Not all implementations of realpath() support a NULL second argument */
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
