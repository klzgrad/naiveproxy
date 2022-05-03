/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.] */

#include <openssl/asn1.h>

#include <ctype.h>
#include <inttypes.h>
#include <string.h>

#include <openssl/bio.h>
#include <openssl/mem.h>

#include "charmap.h"
#include "internal.h"


// These flags must be distinct from |ESC_FLAGS| and fit in a byte.

// Character is a valid PrintableString character
#define CHARTYPE_PRINTABLESTRING 0x10
// Character needs escaping if it is the first character
#define CHARTYPE_FIRST_ESC_2253 0x20
// Character needs escaping if it is the last character
#define CHARTYPE_LAST_ESC_2253 0x40

#define CHARTYPE_BS_ESC         (ASN1_STRFLGS_ESC_2253 | CHARTYPE_FIRST_ESC_2253 | CHARTYPE_LAST_ESC_2253)

#define ESC_FLAGS (ASN1_STRFLGS_ESC_2253 | \
                  ASN1_STRFLGS_ESC_QUOTE | \
                  ASN1_STRFLGS_ESC_CTRL | \
                  ASN1_STRFLGS_ESC_MSB)

static int maybe_write(BIO *out, const void *buf, int len)
{
    /* If |out| is NULL, ignore the output but report the length. */
    return out == NULL || BIO_write(out, buf, len) == len;
}

/*
 * This function handles display of strings, one character at a time. It is
 * passed an unsigned long for each character because it could come from 2 or
 * even 4 byte forms.
 */

#define HEX_SIZE(type) (sizeof(type)*2)

static int do_esc_char(uint32_t c, unsigned char flags, char *do_quotes,
                       BIO *out)
{
    unsigned char chflgs, chtmp;
    char tmphex[HEX_SIZE(uint32_t) + 3];

    if (c > 0xffff) {
        BIO_snprintf(tmphex, sizeof tmphex, "\\W%08" PRIX32, c);
        if (!maybe_write(out, tmphex, 10))
            return -1;
        return 10;
    }
    if (c > 0xff) {
        BIO_snprintf(tmphex, sizeof tmphex, "\\U%04" PRIX32, c);
        if (!maybe_write(out, tmphex, 6))
            return -1;
        return 6;
    }
    chtmp = (unsigned char)c;
    if (chtmp > 0x7f)
        chflgs = flags & ASN1_STRFLGS_ESC_MSB;
    else
        chflgs = char_type[chtmp] & flags;
    if (chflgs & CHARTYPE_BS_ESC) {
        /* If we don't escape with quotes, signal we need quotes */
        if (chflgs & ASN1_STRFLGS_ESC_QUOTE) {
            if (do_quotes)
                *do_quotes = 1;
            if (!maybe_write(out, &chtmp, 1))
                return -1;
            return 1;
        }
        if (!maybe_write(out, "\\", 1))
            return -1;
        if (!maybe_write(out, &chtmp, 1))
            return -1;
        return 2;
    }
    if (chflgs & (ASN1_STRFLGS_ESC_CTRL | ASN1_STRFLGS_ESC_MSB)) {
        BIO_snprintf(tmphex, 11, "\\%02X", chtmp);
        if (!maybe_write(out, tmphex, 3))
            return -1;
        return 3;
    }
    /*
     * If we get this far and do any escaping at all must escape the escape
     * character itself: backslash.
     */
    if (chtmp == '\\' && flags & ESC_FLAGS) {
        if (!maybe_write(out, "\\\\", 2))
            return -1;
        return 2;
    }
    if (!maybe_write(out, &chtmp, 1))
        return -1;
    return 1;
}

#define BUF_TYPE_WIDTH_MASK     0x7
#define BUF_TYPE_CONVUTF8       0x8

/*
 * This function sends each character in a buffer to do_esc_char(). It
 * interprets the content formats and converts to or from UTF8 as
 * appropriate.
 */

static int do_buf(unsigned char *buf, int buflen,
                  int type, unsigned char flags, char *quotes, BIO *out)
{
    int i, outlen, len, charwidth;
    unsigned char orflags, *p, *q;
    uint32_t c;
    p = buf;
    q = buf + buflen;
    outlen = 0;
    charwidth = type & BUF_TYPE_WIDTH_MASK;

    switch (charwidth) {
    case 4:
        if (buflen & 3) {
            OPENSSL_PUT_ERROR(ASN1, ASN1_R_INVALID_UNIVERSALSTRING);
            return -1;
        }
        break;
    case 2:
        if (buflen & 1) {
            OPENSSL_PUT_ERROR(ASN1, ASN1_R_INVALID_BMPSTRING);
            return -1;
        }
        break;
    default:
        break;
    }

    while (p != q) {
        if (p == buf && flags & ASN1_STRFLGS_ESC_2253)
            orflags = CHARTYPE_FIRST_ESC_2253;
        else
            orflags = 0;
        /* TODO(davidben): Replace this with |cbs_get_ucs2_be|, etc., to check
         * for invalid codepoints. */
        switch (charwidth) {
        case 4:
            c = ((uint32_t)*p++) << 24;
            c |= ((uint32_t)*p++) << 16;
            c |= ((uint32_t)*p++) << 8;
            c |= *p++;
            break;

        case 2:
            c = ((uint32_t)*p++) << 8;
            c |= *p++;
            break;

        case 1:
            c = *p++;
            break;

        case 0:
            i = UTF8_getc(p, buflen, &c);
            if (i < 0)
                return -1;      /* Invalid UTF8String */
            buflen -= i;
            p += i;
            break;
        default:
            return -1;          /* invalid width */
        }
        if (p == q && flags & ASN1_STRFLGS_ESC_2253)
            orflags = CHARTYPE_LAST_ESC_2253;
        if (type & BUF_TYPE_CONVUTF8) {
            unsigned char utfbuf[6];
            int utflen;
            utflen = UTF8_putc(utfbuf, sizeof utfbuf, c);
            for (i = 0; i < utflen; i++) {
                /*
                 * We don't need to worry about setting orflags correctly
                 * because if utflen==1 its value will be correct anyway
                 * otherwise each character will be > 0x7f and so the
                 * character will never be escaped on first and last.
                 */
                len = do_esc_char(utfbuf[i], (unsigned char)(flags | orflags),
                                  quotes, out);
                if (len < 0)
                    return -1;
                outlen += len;
            }
        } else {
            len = do_esc_char(c, (unsigned char)(flags | orflags), quotes, out);
            if (len < 0)
                return -1;
            outlen += len;
        }
    }
    return outlen;
}

/* This function hex dumps a buffer of characters */

static int do_hex_dump(BIO *out, unsigned char *buf, int buflen)
{
    static const char hexdig[] = "0123456789ABCDEF";
    unsigned char *p, *q;
    char hextmp[2];
    if (out) {
        p = buf;
        q = buf + buflen;
        while (p != q) {
            hextmp[0] = hexdig[*p >> 4];
            hextmp[1] = hexdig[*p & 0xf];
            if (!maybe_write(out, hextmp, 2))
                return -1;
            p++;
        }
    }
    return buflen << 1;
}

/*
 * "dump" a string. This is done when the type is unknown, or the flags
 * request it. We can either dump the content octets or the entire DER
 * encoding. This uses the RFC 2253 #01234 format.
 */

static int do_dump(unsigned long lflags, BIO *out, const ASN1_STRING *str)
{
    if (!maybe_write(out, "#", 1)) {
        return -1;
    }

    /* If we don't dump DER encoding just dump content octets */
    if (!(lflags & ASN1_STRFLGS_DUMP_DER)) {
        int outlen = do_hex_dump(out, str->data, str->length);
        if (outlen < 0) {
            return -1;
        }
        return outlen + 1;
    }

    /*
     * Placing the ASN1_STRING in a temporary ASN1_TYPE allows the DER encoding
     * to readily obtained.
     */
    ASN1_TYPE t;
    t.type = str->type;
    /* Negative INTEGER and ENUMERATED values are the only case where
     * |ASN1_STRING| and |ASN1_TYPE| types do not match.
     *
     * TODO(davidben): There are also some type fields which, in |ASN1_TYPE|, do
     * not correspond to |ASN1_STRING|. It is unclear whether those are allowed
     * in |ASN1_STRING| at all, or what the space of allowed types is.
     * |ASN1_item_ex_d2i| will never produce such a value so, for now, we say
     * this is an invalid input. But this corner of the library in general
     * should be more robust. */
    if (t.type == V_ASN1_NEG_INTEGER) {
      t.type = V_ASN1_INTEGER;
    } else if (t.type == V_ASN1_NEG_ENUMERATED) {
      t.type = V_ASN1_ENUMERATED;
    }
    t.value.asn1_string = (ASN1_STRING *)str;
    unsigned char *der_buf = NULL;
    int der_len = i2d_ASN1_TYPE(&t, &der_buf);
    if (der_len < 0) {
        return -1;
    }
    int outlen = do_hex_dump(out, der_buf, der_len);
    OPENSSL_free(der_buf);
    if (outlen < 0) {
        return -1;
    }
    return outlen + 1;
}

/*
 * Lookup table to convert tags to character widths, 0 = UTF8 encoded, -1 is
 * used for non string types otherwise it is the number of bytes per
 * character
 */

static const signed char tag2nbyte[] = {
    -1, -1, -1, -1, -1,         /* 0-4 */
    -1, -1, -1, -1, -1,         /* 5-9 */
    -1, -1, 0, -1,              /* 10-13 */
    -1, -1, -1, -1,             /* 15-17 */
    1, 1, 1,                    /* 18-20 */
    -1, 1, 1, 1,                /* 21-24 */
    -1, 1, -1,                  /* 25-27 */
    4, -1, 2                    /* 28-30 */
};

/*
 * This is the main function, print out an ASN1_STRING taking note of various
 * escape and display options. Returns number of characters written or -1 if
 * an error occurred.
 */

int ASN1_STRING_print_ex(BIO *out, const ASN1_STRING *str, unsigned long lflags)
{
    int outlen, len;
    int type;
    char quotes;
    unsigned char flags;
    quotes = 0;
    /* Keep a copy of escape flags */
    flags = (unsigned char)(lflags & ESC_FLAGS);

    type = str->type;

    outlen = 0;

    if (lflags & ASN1_STRFLGS_SHOW_TYPE) {
        const char *tagname;
        tagname = ASN1_tag2str(type);
        outlen += strlen(tagname);
        if (!maybe_write(out, tagname, outlen) || !maybe_write(out, ":", 1))
            return -1;
        outlen++;
    }

    /* Decide what to do with type, either dump content or display it */

    /* Dump everything */
    if (lflags & ASN1_STRFLGS_DUMP_ALL)
        type = -1;
    /* Ignore the string type */
    else if (lflags & ASN1_STRFLGS_IGNORE_TYPE)
        type = 1;
    else {
        /* Else determine width based on type */
        if ((type > 0) && (type < 31))
            type = tag2nbyte[type];
        else
            type = -1;
        if ((type == -1) && !(lflags & ASN1_STRFLGS_DUMP_UNKNOWN))
            type = 1;
    }

    if (type == -1) {
        len = do_dump(lflags, out, str);
        if (len < 0)
            return -1;
        outlen += len;
        return outlen;
    }

    if (lflags & ASN1_STRFLGS_UTF8_CONVERT) {
        /*
         * Note: if string is UTF8 and we want to convert to UTF8 then we
         * just interpret it as 1 byte per character to avoid converting
         * twice.
         */
        if (!type)
            type = 1;
        else
            type |= BUF_TYPE_CONVUTF8;
    }

    len = do_buf(str->data, str->length, type, flags, &quotes, NULL);
    if (len < 0)
        return -1;
    outlen += len;
    if (quotes)
        outlen += 2;
    if (!out)
        return outlen;
    if (quotes && !maybe_write(out, "\"", 1))
        return -1;
    if (do_buf(str->data, str->length, type, flags, NULL, out) < 0)
        return -1;
    if (quotes && !maybe_write(out, "\"", 1))
        return -1;
    return outlen;
}

int ASN1_STRING_print_ex_fp(FILE *fp, const ASN1_STRING *str,
                            unsigned long flags)
{
    BIO *bio = NULL;
    if (fp != NULL) {
        /* If |fp| is NULL, this function returns the number of bytes without
         * writing. */
        bio = BIO_new_fp(fp, BIO_NOCLOSE);
        if (bio == NULL) {
            return -1;
        }
    }
    int ret = ASN1_STRING_print_ex(bio, str, flags);
    BIO_free(bio);
    return ret;
}

int ASN1_STRING_to_UTF8(unsigned char **out, const ASN1_STRING *in)
{
    ASN1_STRING stmp, *str = &stmp;
    int mbflag, type, ret;
    if (!in)
        return -1;
    type = in->type;
    if ((type < 0) || (type > 30))
        return -1;
    mbflag = tag2nbyte[type];
    if (mbflag == -1)
        return -1;
    mbflag |= MBSTRING_FLAG;
    stmp.data = NULL;
    stmp.length = 0;
    stmp.flags = 0;
    ret = ASN1_mbstring_copy(&str, in->data, in->length, mbflag,
                             B_ASN1_UTF8STRING);
    if (ret < 0)
        return ret;
    *out = stmp.data;
    return stmp.length;
}

int ASN1_STRING_print(BIO *bp, const ASN1_STRING *v)
{
    int i, n;
    char buf[80];
    const char *p;

    if (v == NULL)
        return (0);
    n = 0;
    p = (const char *)v->data;
    for (i = 0; i < v->length; i++) {
        if ((p[i] > '~') || ((p[i] < ' ') &&
                             (p[i] != '\n') && (p[i] != '\r')))
            buf[n] = '.';
        else
            buf[n] = p[i];
        n++;
        if (n >= 80) {
            if (BIO_write(bp, buf, n) <= 0)
                return (0);
            n = 0;
        }
    }
    if (n > 0)
        if (BIO_write(bp, buf, n) <= 0)
            return (0);
    return (1);
}

int ASN1_TIME_print(BIO *bp, const ASN1_TIME *tm)
{
    if (tm->type == V_ASN1_UTCTIME)
        return ASN1_UTCTIME_print(bp, tm);
    if (tm->type == V_ASN1_GENERALIZEDTIME)
        return ASN1_GENERALIZEDTIME_print(bp, tm);
    BIO_write(bp, "Bad time value", 14);
    return (0);
}

static const char *const mon[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

int ASN1_GENERALIZEDTIME_print(BIO *bp, const ASN1_GENERALIZEDTIME *tm)
{
    char *v;
    int gmt = 0;
    int i;
    int y = 0, M = 0, d = 0, h = 0, m = 0, s = 0;
    char *f = NULL;
    int f_len = 0;

    i = tm->length;
    v = (char *)tm->data;

    if (i < 12)
        goto err;
    if (v[i - 1] == 'Z')
        gmt = 1;
    for (i = 0; i < 12; i++)
        if ((v[i] > '9') || (v[i] < '0'))
            goto err;
    y = (v[0] - '0') * 1000 + (v[1] - '0') * 100 + (v[2] - '0') * 10 + (v[3] -
                                                                        '0');
    M = (v[4] - '0') * 10 + (v[5] - '0');
    if ((M > 12) || (M < 1))
        goto err;
    d = (v[6] - '0') * 10 + (v[7] - '0');
    h = (v[8] - '0') * 10 + (v[9] - '0');
    m = (v[10] - '0') * 10 + (v[11] - '0');
    if (tm->length >= 14 &&
        (v[12] >= '0') && (v[12] <= '9') &&
        (v[13] >= '0') && (v[13] <= '9')) {
        s = (v[12] - '0') * 10 + (v[13] - '0');
        /* Check for fractions of seconds. */
        if (tm->length >= 15 && v[14] == '.') {
            int l = tm->length;
            f = &v[14];         /* The decimal point. */
            f_len = 1;
            while (14 + f_len < l && f[f_len] >= '0' && f[f_len] <= '9')
                ++f_len;
        }
    }

    if (BIO_printf(bp, "%s %2d %02d:%02d:%02d%.*s %d%s",
                   mon[M - 1], d, h, m, s, f_len, f, y,
                   (gmt) ? " GMT" : "") <= 0)
        return (0);
    else
        return (1);
 err:
    BIO_write(bp, "Bad time value", 14);
    return (0);
}

// consume_two_digits is a helper function for ASN1_UTCTIME_print. If |*v|,
// assumed to be |*len| bytes long, has two leading digits, updates |*out| with
// their value, updates |v| and |len|, and returns one. Otherwise, returns
// zero.
static int consume_two_digits(int* out, const char **v, int *len) {
  if (*len < 2|| !isdigit((*v)[0]) || !isdigit((*v)[1])) {
    return 0;
  }
  *out = ((*v)[0] - '0') * 10 + ((*v)[1] - '0');
  *len -= 2;
  *v += 2;
  return 1;
}

// consume_zulu_timezone is a helper function for ASN1_UTCTIME_print. If |*v|,
// assumed to be |*len| bytes long, starts with "Z" then it updates |*v| and
// |*len| and returns one. Otherwise returns zero.
static int consume_zulu_timezone(const char **v, int *len) {
  if (*len == 0 || (*v)[0] != 'Z') {
    return 0;
  }

  *len -= 1;
  *v += 1;
  return 1;
}

int ASN1_UTCTIME_print(BIO *bp, const ASN1_UTCTIME *tm) {
  const char *v = (const char *)tm->data;
  int len = tm->length;
  int Y = 0, M = 0, D = 0, h = 0, m = 0, s = 0;

  // YYMMDDhhmm are required to be present.
  if (!consume_two_digits(&Y, &v, &len) ||
      !consume_two_digits(&M, &v, &len) ||
      !consume_two_digits(&D, &v, &len) ||
      !consume_two_digits(&h, &v, &len) ||
      !consume_two_digits(&m, &v, &len)) {
    goto err;
  }
  // https://tools.ietf.org/html/rfc5280, section 4.1.2.5.1, requires seconds
  // to be present, but historically this code has forgiven its absence.
  consume_two_digits(&s, &v, &len);

  // https://tools.ietf.org/html/rfc5280, section 4.1.2.5.1, specifies this
  // interpretation of the year.
  if (Y < 50) {
    Y += 2000;
  } else {
    Y += 1900;
  }
  if (M > 12 || M == 0) {
    goto err;
  }
  if (D > 31 || D == 0) {
    goto err;
  }
  if (h > 23 || m > 59 || s > 60) {
    goto err;
  }

  // https://tools.ietf.org/html/rfc5280, section 4.1.2.5.1, requires the "Z"
  // to be present, but historically this code has forgiven its absence.
  const int is_gmt = consume_zulu_timezone(&v, &len);

  // https://tools.ietf.org/html/rfc5280, section 4.1.2.5.1, does not permit
  // the specification of timezones using the +hhmm / -hhmm syntax, which is
  // the only other thing that might legitimately be found at the end.
  if (len) {
    goto err;
  }

  return BIO_printf(bp, "%s %2d %02d:%02d:%02d %d%s", mon[M - 1], D, h, m, s, Y,
                    is_gmt ? " GMT" : "") > 0;

err:
  BIO_write(bp, "Bad time value", 14);
  return 0;
}
