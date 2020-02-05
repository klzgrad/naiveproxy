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

#ifndef NASMLIB_FILE_H
#define NASMLIB_FILE_H

#include "compiler.h"
#include "nasmlib.h"
#include "error.h"

#include <errno.h>

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif
#ifdef HAVE_IO_H
# include <io.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_SYS_MMAN_H
# include <sys/mman.h>
#endif

#if !defined(HAVE_ACCESS) && defined(HAVE__ACCESS)
# define HAVE_ACCESS 1
# define access _access
#endif
#ifndef R_OK
# define R_OK 4                 /* Classic Unix constant, same on Windows */
#endif

/* Can we adjust the file size without actually writing all the bytes? */
#ifdef HAVE__CHSIZE_S
# define nasm_ftruncate(fd,size) _chsize_s(fd,size)
#elif defined(HAVE__CHSIZE)
# define nasm_ftruncate(fd,size) _chsize(fd,size)
#elif defined(HAVE_FTRUNCATE)
# define nasm_ftruncate(fd,size) ftruncate(fd,size)
#endif

/*
 * On Win32/64, stat has a 32-bit file size but _stati64 has a 64-bit file
 * size.  Things get complicated because some of these may be macros,
 * which autoconf won't pick up on as the standard autoconf tests do
 * #undef.
 */
#ifdef _stati64
# define HAVE_STRUCT__STATI64 1
# define HAVE__STATI64 1
#endif
#ifdef _fstati64
# define HAVE__FSTATI64 1
#endif

#ifdef HAVE_STRUCT__STATI64
typedef struct _stati64 nasm_struct_stat;
# ifdef HAVE__STATI64
#  define nasm_stat _stati64
# endif
# ifdef HAVE__FSTATI64
#  define nasm_fstat _fstati64
# endif
#elif defined(HAVE_STRUCT_STAT)
typedef struct stat nasm_struct_stat;
# ifdef HAVE_STAT
#  define nasm_stat stat
# endif
# ifdef HAVE_FSTAT
#  define nasm_fstat fstat
# endif
#endif

#ifndef HAVE_FILENO
# ifdef fileno                  /* autoconf doesn't always pick up macros */
#  define HAVE_FILENO 1
# elif defined(HAVE__FILENO)
#  define HAVE_FILENO 1
#  define fileno _fileno
# endif
#endif

/* These functions are utterly useless without fileno() */
#ifndef HAVE_FILENO
# undef nasm_fstat
# undef nasm_ftruncate
# undef HAVE_MMAP
# undef HAVE__FILELENGTHI64
#endif

#endif /* NASMLIB_FILE_H */
