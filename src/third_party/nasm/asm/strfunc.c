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
 * strfunc.c
 *
 * String transformation functions
 */

#include "nasmlib.h"
#include "nasm.h"

/*
 * Convert a string in UTF-8 format to UTF-16LE
 */
static size_t utf8_to_16le(uint8_t *str, size_t len, char *op)
{
#define EMIT(x) do { if (op) { WRITESHORT(op,x); } outlen++; } while(0)

    size_t outlen = 0;
    int expect = 0;
    uint8_t c;
    uint32_t v = 0, vmin = 0;

    while (len--) {
        c = *str++;

        if (expect) {
            if ((c & 0xc0) != 0x80) {
                expect = 0;
                return -1;
            } else {
                v = (v << 6) | (c & 0x3f);
                if (!--expect) {
                    if (v < vmin || v > 0x10ffff ||
                        (v >= 0xd800 && v <= 0xdfff)) {
                        return -1;
                    } else if (v > 0xffff) {
                        v -= 0x10000;
                        EMIT(0xd800 | (v >> 10));
                        EMIT(0xdc00 | (v & 0x3ff));
                    } else {
                        EMIT(v);
                    }
                }
                continue;
            }
        }

        if (c < 0x80) {
            EMIT(c);
        } else if (c < 0xc0 || c >= 0xfe) {
            /* Invalid UTF-8 */
            return -1;
        } else if (c < 0xe0) {
            v = c & 0x1f;
            expect = 1;
            vmin = 0x80;
        } else if (c < 0xf0) {
            v = c & 0x0f;
            expect = 2;
            vmin = 0x800;
        } else if (c < 0xf8) {
            v = c & 0x07;
            expect = 3;
            vmin = 0x10000;
        } else if (c < 0xfc) {
            v = c & 0x03;
            expect = 4;
            vmin = 0x200000;
        } else {
            v = c & 0x01;
            expect = 5;
            vmin = 0x4000000;
        }
    }

    return expect ? (size_t)-1 : outlen << 1;

#undef EMIT
}

/*
 * Convert a string in UTF-8 format to UTF-16BE
 */
static size_t utf8_to_16be(uint8_t *str, size_t len, char *op)
{
#define EMIT(x)                                 \
    do {                                        \
        uint16_t _y = (x);                      \
        if (op) {                               \
            WRITECHAR(op, _y >> 8);             \
            WRITECHAR(op, _y);                  \
        }                                       \
        outlen++;                               \
    } while (0)                                 \

    size_t outlen = 0;
    int expect = 0;
    uint8_t c;
    uint32_t v = 0, vmin = 0;

    while (len--) {
        c = *str++;

        if (expect) {
            if ((c & 0xc0) != 0x80) {
                expect = 0;
                return -1;
            } else {
                v = (v << 6) | (c & 0x3f);
                if (!--expect) {
                    if (v < vmin || v > 0x10ffff ||
                        (v >= 0xd800 && v <= 0xdfff)) {
                        return -1;
                    } else if (v > 0xffff) {
                        v -= 0x10000;
                        EMIT(0xdc00 | (v & 0x3ff));
                        EMIT(0xd800 | (v >> 10));
                    } else {
                        EMIT(v);
                    }
                }
                continue;
            }
        }

        if (c < 0x80) {
            EMIT(c);
        } else if (c < 0xc0 || c >= 0xfe) {
            /* Invalid UTF-8 */
            return -1;
        } else if (c < 0xe0) {
            v = c & 0x1f;
            expect = 1;
            vmin = 0x80;
        } else if (c < 0xf0) {
            v = c & 0x0f;
            expect = 2;
            vmin = 0x800;
        } else if (c < 0xf8) {
            v = c & 0x07;
            expect = 3;
            vmin = 0x10000;
        } else if (c < 0xfc) {
            v = c & 0x03;
            expect = 4;
            vmin = 0x200000;
        } else {
            v = c & 0x01;
            expect = 5;
            vmin = 0x4000000;
        }
    }

    return expect ? (size_t)-1 : outlen << 1;

#undef EMIT
}

/*
 * Convert a string in UTF-8 format to UTF-32LE
 */
static size_t utf8_to_32le(uint8_t *str, size_t len, char *op)
{
#define EMIT(x) do { if (op) { WRITELONG(op,x); } outlen++; } while(0)

    size_t outlen = 0;
    int expect = 0;
    uint8_t c;
    uint32_t v = 0, vmin = 0;

    while (len--) {
        c = *str++;

        if (expect) {
            if ((c & 0xc0) != 0x80) {
                return -1;
            } else {
                v = (v << 6) | (c & 0x3f);
                if (!--expect) {
                    if (v < vmin || (v >= 0xd800 && v <= 0xdfff)) {
                        return -1;
                    } else {
                        EMIT(v);
                    }
                }
                continue;
            }
        }

        if (c < 0x80) {
            EMIT(c);
        } else if (c < 0xc0 || c >= 0xfe) {
            /* Invalid UTF-8 */
            return -1;
        } else if (c < 0xe0) {
            v = c & 0x1f;
            expect = 1;
            vmin = 0x80;
        } else if (c < 0xf0) {
            v = c & 0x0f;
            expect = 2;
            vmin = 0x800;
        } else if (c < 0xf8) {
            v = c & 0x07;
            expect = 3;
            vmin = 0x10000;
        } else if (c < 0xfc) {
            v = c & 0x03;
            expect = 4;
            vmin = 0x200000;
        } else {
            v = c & 0x01;
            expect = 5;
            vmin = 0x4000000;
        }
    }

    return expect ? (size_t)-1 : outlen << 2;

#undef EMIT
}

/*
 * Convert a string in UTF-8 format to UTF-32BE
 */
static size_t utf8_to_32be(uint8_t *str, size_t len, char *op)
{
#define EMIT(x)                                         \
    do {                                                \
        uint32_t _y = (x);                              \
        if (op) {                                       \
            WRITECHAR(op,_y >> 24);                     \
            WRITECHAR(op,_y >> 16);                     \
            WRITECHAR(op,_y >> 8);                      \
            WRITECHAR(op,_y);                           \
        }                                               \
        outlen++;                                       \
    } while (0)

    size_t outlen = 0;
    int expect = 0;
    uint8_t c;
    uint32_t v = 0, vmin = 0;

    while (len--) {
        c = *str++;

        if (expect) {
            if ((c & 0xc0) != 0x80) {
                return -1;
            } else {
                v = (v << 6) | (c & 0x3f);
                if (!--expect) {
                    if (v < vmin || (v >= 0xd800 && v <= 0xdfff)) {
                        return -1;
                    } else {
                        EMIT(v);
                    }
                }
                continue;
            }
        }

        if (c < 0x80) {
            EMIT(c);
        } else if (c < 0xc0 || c >= 0xfe) {
            /* Invalid UTF-8 */
            return -1;
        } else if (c < 0xe0) {
            v = c & 0x1f;
            expect = 1;
            vmin = 0x80;
        } else if (c < 0xf0) {
            v = c & 0x0f;
            expect = 2;
            vmin = 0x800;
        } else if (c < 0xf8) {
            v = c & 0x07;
            expect = 3;
            vmin = 0x10000;
        } else if (c < 0xfc) {
            v = c & 0x03;
            expect = 4;
            vmin = 0x200000;
        } else {
            v = c & 0x01;
            expect = 5;
            vmin = 0x4000000;
        }
    }

    return expect ? (size_t)-1 : outlen << 2;

#undef EMIT
}

typedef size_t (*transform_func)(uint8_t *, size_t, char *);

/*
 * Apply a specific string transform and return it in a nasm_malloc'd
 * buffer, returning the length.  On error, returns (size_t)-1 and no
 * buffer is allocated.
 */
size_t string_transform(char *str, size_t len, char **out, enum strfunc func)
{
    /* This should match enum strfunc in nasm.h */
    static const transform_func str_transforms[] = {
        utf8_to_16le,
        utf8_to_16le,
        utf8_to_16be,
        utf8_to_32le,
        utf8_to_32le,
        utf8_to_32be,
    };
    transform_func transform = str_transforms[func];
    size_t outlen;
    uint8_t *s = (uint8_t *)str;
    char *buf;

    outlen = transform(s, len, NULL);
    if (outlen == (size_t)-1)
        return -1;

    *out = buf = nasm_malloc(outlen+1);
    buf[outlen] = '\0'; /* Forcibly null-terminate the buffer */
    return transform(s, len, buf);
}
