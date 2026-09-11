/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 2023-2025 The NASM Authors - All Rights Reserved */

#include "nasmlib.h"

const char * const nasmlib_digit_chars[2] = {
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

/*
 * Produce an unsigned integer string from a number with a specified
 * base, digits and signedness.
 */
int numstr(char *buf, size_t buflen, uint64_t n,
           int digits, unsigned int base, bool ucase)
{
    const char * const dchars = nasm_digit_chars(ucase);
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
