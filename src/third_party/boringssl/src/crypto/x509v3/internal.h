/* Copyright (c) 2018, Google Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. */

#ifndef OPENSSL_HEADER_X509V3_INTERNAL_H
#define OPENSSL_HEADER_X509V3_INTERNAL_H

#include <openssl/base.h>

#include <openssl/conf.h>

#if defined(__cplusplus)
extern "C" {
#endif


// x509v3_bytes_to_hex encodes |len| bytes from |buffer| to hex and returns a
// newly-allocated NUL-terminated string containing the result, or NULL on
// allocation error.
//
// Note this function was historically named |hex_to_string| in OpenSSL, not
// |string_to_hex|.
char *x509v3_bytes_to_hex(const unsigned char *buffer, long len);

// x509v3_hex_string_to_bytes decodes |str| in hex and returns a newly-allocated
// array containing the result, or NULL on error. On success, it sets |*len| to
// the length of the result. Colon separators between bytes in the input are
// allowed and ignored.
//
// Note this function was historically named |string_to_hex| in OpenSSL, not
// |hex_to_string|.
unsigned char *x509v3_hex_to_bytes(const char *str, long *len);

// x509v3_name_cmp returns zero if |name| is equal to |cmp| or begins with |cmp|
// followed by '.'. Otherwise, it returns a non-zero number.
int x509v3_name_cmp(const char *name, const char *cmp);

// x509v3_looks_like_dns_name returns one if |in| looks like a DNS name and zero
// otherwise.
OPENSSL_EXPORT int x509v3_looks_like_dns_name(const unsigned char *in,
                                              size_t len);

// x509v3_cache_extensions fills in a number of fields relating to X.509
// extensions in |x|. It returns one on success and zero if some extensions were
// invalid.
int x509v3_cache_extensions(X509 *x);

// x509v3_a2i_ipadd decodes |ipasc| as an IPv4 or IPv6 address. IPv6 addresses
// use colon-separated syntax while IPv4 addresses use dotted decimal syntax. If
// it decodes an IPv4 address, it writes the result to the first four bytes of
// |ipout| and returns four. If it decodes an IPv6 address, it writes the result
// to all 16 bytes of |ipout| and returns 16. Otherwise, it returns zero.
int x509v3_a2i_ipadd(unsigned char ipout[16], const char *ipasc);

// A |BIT_STRING_BITNAME| is used to contain a list of bit names.
typedef struct {
  int bitnum;
  const char *lname;
  const char *sname;
} BIT_STRING_BITNAME;

// x509V3_add_value_asn1_string appends a |CONF_VALUE| with the specified name
// and value to |*extlist|. if |*extlist| is NULL, it sets |*extlist| to a
// newly-allocated |STACK_OF(CONF_VALUE)| first. It returns one on success and
// zero on error.
int x509V3_add_value_asn1_string(const char *name, const ASN1_STRING *value,
                                 STACK_OF(CONF_VALUE) **extlist);


#if defined(__cplusplus)
}  /* extern C */
#endif

#endif  /* OPENSSL_HEADER_X509V3_INTERNAL_H */
