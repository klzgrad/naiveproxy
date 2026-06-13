/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 2017-2025 The NASM Authors - All Rights Reserved */

/*
 * path.c - host operating system specific pathname manipulation functions
 *
 * This file is inherently nonportable ... please help adjusting it to
 * any new platforms that may be necessary.
 */

#include "compiler.h"
#include "nasmlib.h"
#include "error.h"

#define PATH_UNKNOWN	0
#define PATH_UNIX	1
#define PATH_MSDOS	2
#define PATH_MACCLASSIC	3
#define PATH_VMS	4

#ifdef PATHSTYLE
/* PATHSTYLE set externally, hope it is correct */
#elif defined(__MSDOS__) || defined(__DOS__) || \
    defined(__WINDOWS__) || defined(_Windows) ||                        \
    defined(__OS2__) || defined(_WIN16) || defined(WIN32) || defined(_WIN32)
/*
 * MS-DOS, Windows and like operating systems
 */
# define PATHSTYLE PATH_MSDOS
#elif defined(unix) || defined(__unix) || defined(__unix__) ||   \
    defined(__UNIX__) || defined(__Unix__) || \
    defined(_POSIX_VERSION) || defined(_XOPEN_VERSION) || \
    defined(__MACH__) || defined(__BEOS__) || defined(__HAIKU__)
/*
 * Unix and Unix-like operating systems and others using
 * the equivalent syntax (slashes as only separators, no concept of volume)
 *
 * This must come after the __MSDOS__ section, since it seems that at
 * least DJGPP defines __unix__ despite not being a Unix environment at all.
 */
# define PATHSTYLE PATH_UNIX
#elif defined(Macintosh) || defined(macintosh)
# define PATHSTYLE PATH_MACCLASSIC
#elif defined(__VMS)
/* VMS (only partially supported, really) */
# define PATHSTYLE PATH_VMS
#else
/* Something else entirely? */
# define PATHSTYLE PATH_UNKNOWN
#endif

#if PATHSTYLE == PATH_MSDOS
# define separators "/\\:"
# define cleandirend "/\\"
# define catsep '\\'
# define leaveonclean 2         /* Leave \\ at the start alone */
# define curdir "."
#elif PATHSTYLE == PATH_UNIX
# define separators "/"
# define cleandirend "/"
# define catsep '/'
# define leaveonclean 1
# define curdir "."
#elif PATHSTYLE == PATH_MACCLASSIC
# define separators ":"
# define curdir ":"
# define catsep ':'
# define cleandirend ":"
# define leaveonclean 0
# define leave_leading 1
#elif PATHSTYLE == PATH_VMS
/*
 * VMS filenames may have ;version at the end.  Assume we should count that
 * as part of the filename anyway.
 */
# define separators ":]"
# define curdir "[]"
#else
/* No idea what to do here, do nothing.  Feel free to add new ones. */
# define curdir ""
#endif

/*
 * This is an inline, because most compilers can greatly simplify this
 * for a fixed string, like we have here.
 */
static inline bool pure_func ismatch(const char *charset, char ch)
{
    const char *p;

    for (p = charset; *p; p++) {
        if (ch == *p)
            return true;
    }

    return false;
}

static const char * pure_func first_filename_char(const char *path)
{
#ifdef separators
    const char *p = path + strlen(path);

    while (p > path) {
        if (ismatch(separators, p[-1]))
            return p;
        p--;
    }

    return p;
#else
    return path;
#endif
}

/* Return the filename portion of a PATH as a new string */
char *nasm_basename(const char *path)
{
    return nasm_strdup(first_filename_char(path));
}

/* Return the directory name portion of a PATH as a new string */
char *nasm_dirname(const char *path)
{
    const char *p = first_filename_char(path);
    const char *p0 = p;
    (void)p0;                   /* Don't warn if unused */

    if (p == path)
        return nasm_strdup(curdir);

#ifdef cleandirend
    while (p > path+leaveonclean) {
        if (ismatch(cleandirend, p[-1]))
            break;
        p--;
    }
#endif

#ifdef leave_leading
    /* If the directory contained ONLY separators, leave as-is */
    if (p == path+leaveonclean)
        p = p0;
#endif

    return nasm_strndup(path, p-path);
}

/*
 * Concatenate a directory path and a filename.  Note that this function
 * currently does NOT handle the case where file itself contains
 * directory components (except on Unix platforms, because it is trivial.)
 */
char *nasm_catfile(const char *dir, const char *file)
{
#ifndef catsep
    return nasm_strcat(dir, file);
#else
    size_t dl = strlen(dir);
    size_t fl = strlen(file);
    char *p, *pp;
    bool dosep = true;

    if (!dl || ismatch(separators, dir[dl-1])) {
        /* No separator necessary */
        dosep = false;
    }

    p = pp = nasm_malloc(dl + fl + dosep + 1);

    memcpy(pp, dir, dl);
    pp += dl;
    if (dosep)
        *pp++ = catsep;

    memcpy(pp, file, fl+1);

    return p;
#endif
}
