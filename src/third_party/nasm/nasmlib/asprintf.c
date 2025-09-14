/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1996-2018 The NASM Authors - All Rights Reserved
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

#include "compiler.h"
#include "nasmlib.h"
#include "alloc.h"

/*
 * nasm_[v]asprintf() are variants of the semi-standard [v]asprintf()
 * functions, except that we return the pointer instead of a count.
 * The length of the string (with or without the final NUL) is available
 * by calling nasm_last_string_{len,size}() afterwards.
 *
 * nasm_[v]axprintf() are similar, but allocates a user-defined amount
 * of storage before the string, and returns a pointer to the
 * allocated buffer. The size of that area is not included in the value
 * returned by nasm_last_string_size().
 */

void *nasm_vaxprintf(size_t extra, const char *fmt, va_list ap)
{
    char *strp;
    va_list xap;
    size_t bytes;

    va_copy(xap, ap);
    bytes = vsnprintf(NULL, 0, fmt, xap) + 1;
    _nasm_last_string_size = bytes;
    va_end(xap);

    strp = nasm_malloc(extra+bytes);
    memset(strp, 0, extra);
    vsnprintf(strp+extra, bytes, fmt, ap);
    return strp;
}

char *nasm_vasprintf(const char *fmt, va_list ap)
{
    return nasm_vaxprintf(0, fmt, ap);
}

void *nasm_axprintf(size_t extra, const char *fmt, ...)
{
    va_list ap;
    void *strp;

    va_start(ap, fmt);
    strp = nasm_vaxprintf(extra, fmt, ap);
    va_end(ap);

    return strp;
}

char *nasm_asprintf(const char *fmt, ...)
{
    va_list ap;
    char *strp;

    va_start(ap, fmt);
    strp = nasm_vaxprintf(0, fmt, ap);
    va_end(ap);

    return strp;
}
