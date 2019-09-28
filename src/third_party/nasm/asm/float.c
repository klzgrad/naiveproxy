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

/*
 * float.c     floating-point constant support for the Netwide Assembler
 */

#include "compiler.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nasm.h"
#include "float.h"
#include "error.h"

/*
 * -----------------
 *  local variables
 * -----------------
 */
static bool daz = false;        /* denormals as zero */
static enum float_round rc = FLOAT_RC_NEAR;     /* rounding control */

/*
 * -----------
 *  constants
 * -----------
 */

/* "A limb is like a digit but bigger */
typedef uint32_t fp_limb;
typedef uint64_t fp_2limb;

#define LIMB_BITS       32
#define LIMB_BYTES      (LIMB_BITS/8)
#define LIMB_TOP_BIT    ((fp_limb)1 << (LIMB_BITS-1))
#define LIMB_MASK       ((fp_limb)(~0))
#define LIMB_ALL_BYTES  ((fp_limb)0x01010101)
#define LIMB_BYTE(x)    ((x)*LIMB_ALL_BYTES)

/* 112 bits + 64 bits for accuracy + 16 bits for rounding */
#define MANT_LIMBS 6

/* 52 digits fit in 176 bits because 10^53 > 2^176 > 10^52 */
#define MANT_DIGITS 52

/* the format and the argument list depend on MANT_LIMBS */
#define MANT_FMT "%08x_%08x_%08x_%08x_%08x_%08x"
#define MANT_ARG SOME_ARG(mant, 0)

#define SOME_ARG(a,i) (a)[(i)+0], (a)[(i)+1], (a)[(i)+2], \
                      (a)[(i)+3], (a)[(i)+4], (a)[(i)+5]

/*
 * ---------------------------------------------------------------------------
 *  emit a printf()-like debug message... but only if DEBUG_FLOAT was defined
 * ---------------------------------------------------------------------------
 */

#ifdef DEBUG_FLOAT
#define dprintf(x) printf x
#else
#define dprintf(x) do { } while (0)
#endif

/*
 * ---------------------------------------------------------------------------
 *  multiply
 * ---------------------------------------------------------------------------
 */
static int float_multiply(fp_limb *to, fp_limb *from)
{
    fp_2limb temp[MANT_LIMBS * 2];
    int i, j;

    /*
     * guaranteed that top bit of 'from' is set -- so we only have
     * to worry about _one_ bit shift to the left
     */
    dprintf(("%s=" MANT_FMT "\n", "mul1", SOME_ARG(to, 0)));
    dprintf(("%s=" MANT_FMT "\n", "mul2", SOME_ARG(from, 0)));

    memset(temp, 0, sizeof temp);

    for (i = 0; i < MANT_LIMBS; i++) {
        for (j = 0; j < MANT_LIMBS; j++) {
            fp_2limb n;
            n = (fp_2limb) to[i] * (fp_2limb) from[j];
            temp[i + j] += n >> LIMB_BITS;
            temp[i + j + 1] += (fp_limb)n;
        }
    }

    for (i = MANT_LIMBS * 2; --i;) {
        temp[i - 1] += temp[i] >> LIMB_BITS;
        temp[i] &= LIMB_MASK;
    }

    dprintf(("%s=" MANT_FMT "_" MANT_FMT "\n", "temp", SOME_ARG(temp, 0),
             SOME_ARG(temp, MANT_LIMBS)));

    if (temp[0] & LIMB_TOP_BIT) {
        for (i = 0; i < MANT_LIMBS; i++) {
            to[i] = temp[i] & LIMB_MASK;
        }
        dprintf(("%s=" MANT_FMT " (%i)\n", "prod", SOME_ARG(to, 0), 0));
        return 0;
    } else {
        for (i = 0; i < MANT_LIMBS; i++) {
            to[i] = (temp[i] << 1) + !!(temp[i + 1] & LIMB_TOP_BIT);
        }
        dprintf(("%s=" MANT_FMT " (%i)\n", "prod", SOME_ARG(to, 0), -1));
        return -1;
    }
}

/*
 * ---------------------------------------------------------------------------
 *  read an exponent; returns INT32_MAX on error
 * ---------------------------------------------------------------------------
 */
static int32_t read_exponent(const char *string, int32_t max)
{
    int32_t i = 0;
    bool neg = false;

    if (*string == '+') {
        string++;
    } else if (*string == '-') {
        neg = true;
        string++;
    }
    while (*string) {
        if (*string >= '0' && *string <= '9') {
            i = (i * 10) + (*string - '0');

            /*
             * To ensure that underflows and overflows are
             * handled properly we must avoid wraparounds of
             * the signed integer value that is used to hold
             * the exponent. Therefore we cap the exponent at
             * +/-5000, which is slightly more/less than
             * what's required for normal and denormal numbers
             * in single, double, and extended precision, but
             * sufficient to avoid signed integer wraparound.
             */
            if (i > max)
                i = max;
        } else if (*string == '_') {
            /* do nothing */
        } else {
            nasm_error(ERR_NONFATAL,
                  "invalid character in floating-point constant %s: '%c'",
                  "exponent", *string);
            return INT32_MAX;
        }
        string++;
    }

    return neg ? -i : i;
}

/*
 * ---------------------------------------------------------------------------
 *  convert
 * ---------------------------------------------------------------------------
 */
static bool ieee_flconvert(const char *string, fp_limb *mant,
                           int32_t * exponent)
{
    char digits[MANT_DIGITS];
    char *p, *q, *r;
    fp_limb mult[MANT_LIMBS], bit;
    fp_limb *m;
    int32_t tenpwr, twopwr;
    int32_t extratwos;
    bool started, seendot, warned;

    warned = false;
    p = digits;
    tenpwr = 0;
    started = seendot = false;

    while (*string && *string != 'E' && *string != 'e') {
        if (*string == '.') {
            if (!seendot) {
                seendot = true;
            } else {
                nasm_error(ERR_NONFATAL,
                      "too many periods in floating-point constant");
                return false;
            }
        } else if (*string >= '0' && *string <= '9') {
            if (*string == '0' && !started) {
                if (seendot) {
                    tenpwr--;
                }
            } else {
                started = true;
                if (p < digits + sizeof(digits)) {
                    *p++ = *string - '0';
                } else {
                    if (!warned) {
                        nasm_error(ERR_WARNING|ERR_WARN_FL_TOOLONG|ERR_PASS2,
                              "floating-point constant significand contains "
                              "more than %i digits", MANT_DIGITS);
                        warned = true;
                    }
                }
                if (!seendot) {
                    tenpwr++;
                }
            }
        } else if (*string == '_') {
            /* do nothing */
        } else {
            nasm_error(ERR_NONFATAL|ERR_PASS2,
                  "invalid character in floating-point constant %s: '%c'",
                  "significand", *string);
            return false;
        }
        string++;
    }

    if (*string) {
        int32_t e;

        string++;               /* eat the E */
        e = read_exponent(string, 5000);
        if (e == INT32_MAX)
            return false;
        tenpwr += e;
    }

    /*
     * At this point, the memory interval [digits,p) contains a
     * series of decimal digits zzzzzzz, such that our number X
     * satisfies X = 0.zzzzzzz * 10^tenpwr.
     */
    q = digits;
    dprintf(("X = 0."));
    while (q < p) {
        dprintf(("%c", *q + '0'));
        q++;
    }
    dprintf((" * 10^%i\n", tenpwr));

    /*
     * Now convert [digits,p) to our internal representation.
     */
    bit = LIMB_TOP_BIT;
    for (m = mant; m < mant + MANT_LIMBS; m++) {
        *m = 0;
    }
    m = mant;
    q = digits;
    started = false;
    twopwr = 0;
    while (m < mant + MANT_LIMBS) {
        fp_limb carry = 0;
        while (p > q && !p[-1]) {
            p--;
        }
        if (p <= q) {
            break;
        }
        for (r = p; r-- > q;) {
            int32_t i;
            i = 2 * *r + carry;
            if (i >= 10) {
                carry = 1;
                i -= 10;
            } else {
                carry = 0;
            }
            *r = i;
        }
        if (carry) {
            *m |= bit;
            started = true;
        }
        if (started) {
            if (bit == 1) {
                bit = LIMB_TOP_BIT;
                m++;
            } else {
                bit >>= 1;
            }
        } else {
            twopwr--;
        }
    }
    twopwr += tenpwr;

    /*
     * At this point, the 'mant' array contains the first frac-
     * tional places of a base-2^16 real number which when mul-
     * tiplied by 2^twopwr and 5^tenpwr gives X.
     */
    dprintf(("X = " MANT_FMT " * 2^%i * 5^%i\n", MANT_ARG, twopwr,
             tenpwr));

    /*
     * Now multiply 'mant' by 5^tenpwr.
     */
    if (tenpwr < 0) {           /* mult = 5^-1 = 0.2 */
        for (m = mult; m < mult + MANT_LIMBS - 1; m++) {
            *m = LIMB_BYTE(0xcc);
        }
        mult[MANT_LIMBS - 1] = LIMB_BYTE(0xcc)+1;
        extratwos = -2;
        tenpwr = -tenpwr;

        /*
         * If tenpwr was 1000...000b, then it becomes 1000...000b. See
         * the "ANSI C" comment below for more details on that case.
         *
         * Because we already truncated tenpwr to +5000...-5000 inside
         * the exponent parsing code, this shouldn't happen though.
         */
    } else if (tenpwr > 0) {    /* mult = 5^+1 = 5.0 */
        mult[0] = (fp_limb)5 << (LIMB_BITS-3); /* 0xA000... */
        for (m = mult + 1; m < mult + MANT_LIMBS; m++) {
            *m = 0;
        }
        extratwos = 3;
    } else {
        extratwos = 0;
    }
    while (tenpwr) {
        dprintf(("loop=" MANT_FMT " * 2^%i * 5^%i (%i)\n", MANT_ARG,
                 twopwr, tenpwr, extratwos));
        if (tenpwr & 1) {
            dprintf(("mant*mult\n"));
            twopwr += extratwos + float_multiply(mant, mult);
        }
        dprintf(("mult*mult\n"));
        extratwos = extratwos * 2 + float_multiply(mult, mult);
        tenpwr >>= 1;

        /*
         * In ANSI C, the result of right-shifting a signed integer is
         * considered implementation-specific. To ensure that the loop
         * terminates even if tenpwr was 1000...000b to begin with, we
         * manually clear the MSB, in case a 1 was shifted in.
         *
         * Because we already truncated tenpwr to +5000...-5000 inside
         * the exponent parsing code, this shouldn't matter; neverthe-
         * less it is the right thing to do here.
         */
        tenpwr &= (uint32_t) - 1 >> 1;
    }

    /*
     * At this point, the 'mant' array contains the first frac-
     * tional places of a base-2^16 real number in [0.5,1) that
     * when multiplied by 2^twopwr gives X. Or it contains zero
     * of course. We are done.
     */
    *exponent = twopwr;
    return true;
}

/*
 * ---------------------------------------------------------------------------
 *  operations of specific bits
 * ---------------------------------------------------------------------------
 */

/* Set a bit, using *bigendian* bit numbering (0 = MSB) */
static void set_bit(fp_limb *mant, int bit)
{
    mant[bit/LIMB_BITS] |= LIMB_TOP_BIT >> (bit & (LIMB_BITS-1));
}

/* Test a single bit */
static int test_bit(const fp_limb *mant, int bit)
{
    return (mant[bit/LIMB_BITS] >> (~bit & (LIMB_BITS-1))) & 1;
}

/* Report if the mantissa value is all zero */
static bool is_zero(const fp_limb *mant)
{
    int i;

    for (i = 0; i < MANT_LIMBS; i++)
        if (mant[i])
            return false;

    return true;
}

/*
 * ---------------------------------------------------------------------------
 *  round a mantissa off after i words
 * ---------------------------------------------------------------------------
 */

#define ROUND_COLLECT_BITS                      \
    do {                                        \
        m = mant[i] & (2*bit-1);                \
        for (j = i+1; j < MANT_LIMBS; j++)      \
            m = m | mant[j];                    \
    } while (0)

#define ROUND_ABS_DOWN                          \
    do {                                        \
        mant[i] &= ~(bit-1);                    \
        for (j = i+1; j < MANT_LIMBS; j++)      \
            mant[j] = 0;                        \
        return false;                           \
    } while (0)

#define ROUND_ABS_UP                            \
    do {                                        \
        mant[i] = (mant[i] & ~(bit-1)) + bit;   \
        for (j = i+1; j < MANT_LIMBS; j++)      \
            mant[j] = 0;                        \
        while (i > 0 && !mant[i])               \
            ++mant[--i];                        \
        return !mant[0];                        \
    } while (0)

static bool ieee_round(bool minus, fp_limb *mant, int bits)
{
    fp_limb m = 0;
    int32_t j;
    int i = bits / LIMB_BITS;
    int p = bits % LIMB_BITS;
    fp_limb bit = LIMB_TOP_BIT >> p;

    if (rc == FLOAT_RC_NEAR) {
        if (mant[i] & bit) {
            mant[i] &= ~bit;
            ROUND_COLLECT_BITS;
            mant[i] |= bit;
            if (m) {
                ROUND_ABS_UP;
            } else {
                if (test_bit(mant, bits-1)) {
                    ROUND_ABS_UP;
                } else {
                    ROUND_ABS_DOWN;
                }
            }
        } else {
            ROUND_ABS_DOWN;
        }
    } else if (rc == FLOAT_RC_ZERO ||
               rc == (minus ? FLOAT_RC_UP : FLOAT_RC_DOWN)) {
        ROUND_ABS_DOWN;
    } else {
        /* rc == (minus ? FLOAT_RC_DOWN : FLOAT_RC_UP) */
        /* Round toward +/- infinity */
        ROUND_COLLECT_BITS;
        if (m) {
            ROUND_ABS_UP;
        } else {
            ROUND_ABS_DOWN;
        }
    }
    return false;
}

/* Returns a value >= 16 if not a valid hex digit */
static unsigned int hexval(char c)
{
    unsigned int v = (unsigned char) c;

    if (v >= '0' && v <= '9')
        return v - '0';
    else
        return (v|0x20) - 'a' + 10;
}

/* Handle floating-point numbers with radix 2^bits and binary exponent */
static bool ieee_flconvert_bin(const char *string, int bits,
                               fp_limb *mant, int32_t *exponent)
{
    static const int log2tbl[16] =
        { -1, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3 };
    fp_limb mult[MANT_LIMBS + 1], *mp;
    int ms;
    int32_t twopwr;
    bool seendot, seendigit;
    unsigned char c;
    const int radix = 1 << bits;
    fp_limb v;

    twopwr = 0;
    seendot = seendigit = false;
    ms = 0;
    mp = NULL;

    memset(mult, 0, sizeof mult);

    while ((c = *string++) != '\0') {
        if (c == '.') {
            if (!seendot)
                seendot = true;
            else {
                nasm_error(ERR_NONFATAL,
                      "too many periods in floating-point constant");
                return false;
            }
        } else if ((v = hexval(c)) < (unsigned int)radix) {
            if (!seendigit && v) {
                int l = log2tbl[v];

                seendigit = true;
                mp = mult;
                ms = (LIMB_BITS-1)-l;

                twopwr += l+1-bits;
            }

            if (seendigit) {
                if (ms <= 0) {
                    *mp |= v >> -ms;
                    mp++;
                    if (mp > &mult[MANT_LIMBS])
                        mp = &mult[MANT_LIMBS]; /* Guard slot */
                    ms += LIMB_BITS;
                }
                *mp |= v << ms;
                ms -= bits;

                if (!seendot)
                    twopwr += bits;
            } else {
                if (seendot)
                    twopwr -= bits;
            }
        } else if (c == 'p' || c == 'P') {
            int32_t e;
            e = read_exponent(string, 20000);
            if (e == INT32_MAX)
                return false;
            twopwr += e;
            break;
        } else if (c == '_') {
            /* ignore */
        } else {
            nasm_error(ERR_NONFATAL,
                  "floating-point constant: `%c' is invalid character", c);
            return false;
        }
    }

    if (!seendigit) {
        memset(mant, 0, MANT_LIMBS*sizeof(fp_limb)); /* Zero */
        *exponent = 0;
    } else {
        memcpy(mant, mult, MANT_LIMBS*sizeof(fp_limb));
        *exponent = twopwr;
    }

    return true;
}

/*
 * Shift a mantissa to the right by i bits.
 */
static void ieee_shr(fp_limb *mant, int i)
{
    fp_limb n, m;
    int j = 0;
    int sr, sl, offs;

    sr = i % LIMB_BITS; sl = LIMB_BITS-sr;
    offs = i/LIMB_BITS;

    if (sr == 0) {
        if (offs)
            for (j = MANT_LIMBS-1; j >= offs; j--)
                mant[j] = mant[j-offs];
    } else if (MANT_LIMBS-1-offs < 0) {
        j = MANT_LIMBS-1;
    } else {
        n = mant[MANT_LIMBS-1-offs] >> sr;
        for (j = MANT_LIMBS-1; j > offs; j--) {
            m = mant[j-offs-1];
            mant[j] = (m << sl) | n;
            n = m >> sr;
        }
        mant[j--] = n;
    }
    while (j >= 0)
        mant[j--] = 0;
}

/* Produce standard IEEE formats, with implicit or explicit integer
   bit; this makes the following assumptions:

   - the sign bit is the MSB, followed by the exponent,
     followed by the integer bit if present.
   - the sign bit plus exponent fit in 16 bits.
   - the exponent bias is 2^(n-1)-1 for an n-bit exponent */

struct ieee_format {
    int bytes;
    int mantissa;               /* Fractional bits in the mantissa */
    int explicit;               /* Explicit integer */
    int exponent;               /* Bits in the exponent */
};

/*
 * The 16- and 128-bit formats are expected to be in IEEE 754r.
 * AMD SSE5 uses the 16-bit format.
 *
 * The 32- and 64-bit formats are the original IEEE 754 formats.
 *
 * The 80-bit format is x87-specific, but widely used.
 *
 * The 8-bit format appears to be the consensus 8-bit floating-point
 * format.  It is apparently used in graphics applications.
 */
static const struct ieee_format ieee_8   = {  1,   3, 0,  4 };
static const struct ieee_format ieee_16  = {  2,  10, 0,  5 };
static const struct ieee_format ieee_32  = {  4,  23, 0,  8 };
static const struct ieee_format ieee_64  = {  8,  52, 0, 11 };
static const struct ieee_format ieee_80  = { 10,  63, 1, 15 };
static const struct ieee_format ieee_128 = { 16, 112, 0, 15 };

/* Types of values we can generate */
enum floats {
    FL_ZERO,
    FL_DENORMAL,
    FL_NORMAL,
    FL_INFINITY,
    FL_QNAN,
    FL_SNAN
};

static int to_packed_bcd(const char *str, const char *p,
                         int s, uint8_t *result,
                         const struct ieee_format *fmt)
{
    int n = 0;
    char c;
    int tv = -1;

    if (fmt != &ieee_80) {
        nasm_error(ERR_NONFATAL,
              "packed BCD requires an 80-bit format");
        return 0;
    }

    while (p >= str) {
        c = *p--;
        if (c >= '0' && c <= '9') {
            if (tv < 0) {
                if (n == 9) {
                    nasm_error(ERR_WARNING|ERR_PASS2,
                          "packed BCD truncated to 18 digits");
                }
                tv = c-'0';
            } else {
                if (n < 9)
                    *result++ = tv + ((c-'0') << 4);
                n++;
                tv = -1;
            }
        } else if (c == '_') {
            /* do nothing */
        } else {
            nasm_error(ERR_NONFATAL,
                  "invalid character `%c' in packed BCD constant", c);
            return 0;
        }
    }
    if (tv >= 0) {
        if (n < 9)
            *result++ = tv;
        n++;
    }
    while (n < 9) {
        *result++ = 0;
        n++;
    }
    *result = (s < 0) ? 0x80 : 0;

    return 1;                   /* success */
}

static int to_float(const char *str, int s, uint8_t *result,
                    const struct ieee_format *fmt)
{
    fp_limb mant[MANT_LIMBS];
    int32_t exponent = 0;
    const int32_t expmax = 1 << (fmt->exponent - 1);
    fp_limb one_mask = LIMB_TOP_BIT >>
        ((fmt->exponent+fmt->explicit) % LIMB_BITS);
    const int one_pos = (fmt->exponent+fmt->explicit)/LIMB_BITS;
    int i;
    int shift;
    enum floats type;
    bool ok;
    const bool minus = s < 0;
    const int bits = fmt->bytes * 8;
    const char *strend;

    nasm_assert(str[0]);

    strend = strchr(str, '\0');
    if (strend[-1] == 'P' || strend[-1] == 'p')
        return to_packed_bcd(str, strend-2, s, result, fmt);

    if (str[0] == '_') {
        /* Special tokens */

        switch (str[2]) {
        case 'n':              /* __nan__ */
        case 'N':
        case 'q':              /* __qnan__ */
        case 'Q':
            type = FL_QNAN;
            break;
        case 's':              /* __snan__ */
        case 'S':
            type = FL_SNAN;
            break;
        case 'i':              /* __infinity__ */
        case 'I':
            type = FL_INFINITY;
            break;
        default:
            nasm_error(ERR_NONFATAL,
                  "internal error: unknown FP constant token `%s'\n", str);
            type = FL_QNAN;
            break;
        }
    } else {
        if (str[0] == '0') {
            switch (str[1]) {
            case 'x': case 'X':
            case 'h': case 'H':
                ok = ieee_flconvert_bin(str+2, 4, mant, &exponent);
                break;
            case 'o': case 'O':
            case 'q': case 'Q':
                ok = ieee_flconvert_bin(str+2, 3, mant, &exponent);
                break;
            case 'b': case 'B':
            case 'y': case 'Y':
                ok = ieee_flconvert_bin(str+2, 1, mant, &exponent);
                break;
            case 'd': case 'D':
            case 't': case 'T':
                ok = ieee_flconvert(str+2, mant, &exponent);
                break;
            case 'p': case 'P':
                return to_packed_bcd(str+2, strend-1, s, result, fmt);
            default:
                /* Leading zero was just a zero? */
                ok = ieee_flconvert(str, mant, &exponent);
                break;
            }
        } else if (str[0] == '$') {
            ok = ieee_flconvert_bin(str+1, 4, mant, &exponent);
        } else {
            ok = ieee_flconvert(str, mant, &exponent);
        }

        if (!ok) {
            type = FL_QNAN;
        } else if (mant[0] & LIMB_TOP_BIT) {
            /*
             * Non-zero.
             */
            exponent--;
            if (exponent >= 2 - expmax && exponent <= expmax) {
                type = FL_NORMAL;
            } else if (exponent > 0) {
                if (pass0 == 1)
                    nasm_error(ERR_WARNING|ERR_WARN_FL_OVERFLOW|ERR_PASS2,
                          "overflow in floating-point constant");
                type = FL_INFINITY;
            } else {
                /* underflow or denormal; the denormal code handles
                   actual underflow. */
                type = FL_DENORMAL;
            }
        } else {
            /* Zero */
            type = FL_ZERO;
        }
    }

    switch (type) {
    case FL_ZERO:
    zero:
        memset(mant, 0, sizeof mant);
        break;

    case FL_DENORMAL:
    {
        shift = -(exponent + expmax - 2 - fmt->exponent)
            + fmt->explicit;
        ieee_shr(mant, shift);
        ieee_round(minus, mant, bits);
        if (mant[one_pos] & one_mask) {
            /* One's position is set, we rounded up into normal range */
            exponent = 1;
            if (!fmt->explicit)
                mant[one_pos] &= ~one_mask;     /* remove explicit one */
            mant[0] |= exponent << (LIMB_BITS-1 - fmt->exponent);
        } else {
            if (daz || is_zero(mant)) {
                /* Flush denormals to zero */
                nasm_error(ERR_WARNING|ERR_WARN_FL_UNDERFLOW|ERR_PASS2,
                      "underflow in floating-point constant");
                goto zero;
            } else {
                nasm_error(ERR_WARNING|ERR_WARN_FL_DENORM|ERR_PASS2,
                      "denormal floating-point constant");
            }
        }
        break;
    }

    case FL_NORMAL:
        exponent += expmax - 1;
        ieee_shr(mant, fmt->exponent+fmt->explicit);
        ieee_round(minus, mant, bits);
        /* did we scale up by one? */
        if (test_bit(mant, fmt->exponent+fmt->explicit-1)) {
            ieee_shr(mant, 1);
            exponent++;
            if (exponent >= (expmax << 1)-1) {
                    nasm_error(ERR_WARNING|ERR_WARN_FL_OVERFLOW|ERR_PASS2,
                          "overflow in floating-point constant");
                type = FL_INFINITY;
                goto overflow;
            }
        }

        if (!fmt->explicit)
            mant[one_pos] &= ~one_mask; /* remove explicit one */
        mant[0] |= exponent << (LIMB_BITS-1 - fmt->exponent);
        break;

    case FL_INFINITY:
    case FL_QNAN:
    case FL_SNAN:
    overflow:
        memset(mant, 0, sizeof mant);
        mant[0] = (((fp_limb)1 << fmt->exponent)-1)
            << (LIMB_BITS-1 - fmt->exponent);
        if (fmt->explicit)
            mant[one_pos] |= one_mask;
        if (type == FL_QNAN)
            set_bit(mant, fmt->exponent+fmt->explicit+1);
        else if (type == FL_SNAN)
            set_bit(mant, fmt->exponent+fmt->explicit+fmt->mantissa);
        break;
    }

    mant[0] |= minus ? LIMB_TOP_BIT : 0;

    for (i = fmt->bytes - 1; i >= 0; i--)
        *result++ = mant[i/LIMB_BYTES] >> (((LIMB_BYTES-1)-(i%LIMB_BYTES))*8);

    return 1;                   /* success */
}

int float_const(const char *number, int sign, uint8_t *result, int bytes)
{
    switch (bytes) {
    case 1:
        return to_float(number, sign, result, &ieee_8);
    case 2:
        return to_float(number, sign, result, &ieee_16);
    case 4:
        return to_float(number, sign, result, &ieee_32);
    case 8:
        return to_float(number, sign, result, &ieee_64);
    case 10:
        return to_float(number, sign, result, &ieee_80);
    case 16:
        return to_float(number, sign, result, &ieee_128);
    default:
        nasm_panic("strange value %d passed to float_const", bytes);
        return 0;
    }
}

/* Set floating-point options */
int float_option(const char *option)
{
    if (!nasm_stricmp(option, "daz")) {
        daz = true;
        return 0;
    } else if (!nasm_stricmp(option, "nodaz")) {
        daz = false;
        return 0;
    } else if (!nasm_stricmp(option, "near")) {
        rc = FLOAT_RC_NEAR;
        return 0;
    } else if (!nasm_stricmp(option, "down")) {
        rc = FLOAT_RC_DOWN;
        return 0;
    } else if (!nasm_stricmp(option, "up")) {
        rc = FLOAT_RC_UP;
        return 0;
    } else if (!nasm_stricmp(option, "zero")) {
        rc = FLOAT_RC_ZERO;
        return 0;
    } else if (!nasm_stricmp(option, "default")) {
        rc = FLOAT_RC_NEAR;
        daz = false;
        return 0;
    } else {
        return -1;              /* Unknown option */
    }
}
