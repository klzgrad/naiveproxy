/* ----------------------------------------------------------------------- *
 *
 *   Copyright 2023 The NASM Authors - All Rights Reserved
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

#include "nasmlib.h"

/*
 * Produce an unsigned integer string from a number with a specified
 * base, digits and signedness.
 */
int numstr(char *buf, size_t buflen, uint64_t n,
           int digits, unsigned int base, bool ucase)
{
    static const char digit_chars[2][NUMSTR_MAXBASE+1] =
    {
        /* Lower case version */
        "0123456789"
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "@_",

        /* Upper case version */
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "@_"
    };
    const char * const dchars = digit_chars[ucase];
    bool moredigits = digits <= 0;
    char *p;
    int len;

    if (base < 2 || base > NUMSTR_MAXBASE)
        return -1;

    if (moredigits)
        digits = -digits;

    p = buf + buflen;
    *--p = '\0';

    while (p > buf && (digits-- > 0 || (moredigits && n))) {
        *--p = dchars[n % base];
        n /= base;
    }

    len = buflen - (p - buf);   /* Including final null */
    if (p != buf)
        memmove(buf, p, len);

    return len - 1;
}
