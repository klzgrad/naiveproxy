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
 * [including the GNU Public Licence.]
 */

#ifndef HEADER_ASN1_H
#define HEADER_ASN1_H

#include <openssl/base.h>

#include <time.h>

#include <openssl/bio.h>
#include <openssl/stack.h>

#include <openssl/bn.h>

#ifdef __cplusplus
extern "C" {
#endif


// Legacy ASN.1 library.
//
// This header is part of OpenSSL's ASN.1 implementation. It is retained for
// compatibility but otherwise underdocumented and not actively maintained. Use
// the new |CBS| and |CBB| library in <openssl/bytestring.h> instead.


// Tag constants.
//
// These constants are used in various APIs to specify ASN.1 types and tag
// components. See the specific API's documentation for details on which values
// are used and how.

// The following constants are tag classes.
#define V_ASN1_UNIVERSAL 0x00
#define V_ASN1_APPLICATION 0x40
#define V_ASN1_CONTEXT_SPECIFIC 0x80
#define V_ASN1_PRIVATE 0xc0

// V_ASN1_CONSTRUCTED indicates an element is constructed, rather than
// primitive.
#define V_ASN1_CONSTRUCTED 0x20

// V_ASN1_PRIMITIVE_TAG is the highest tag number which can be encoded in a
// single byte. Note this is unrelated to whether an element is constructed or
// primitive.
//
// TODO(davidben): Make this private.
#define V_ASN1_PRIMITIVE_TAG 0x1f

// V_ASN1_MAX_UNIVERSAL is the highest supported universal tag number. It is
// necessary to avoid ambiguity with |V_ASN1_NEG| and |MBSTRING_FLAG|.
//
// TODO(davidben): Make this private.
#define V_ASN1_MAX_UNIVERSAL 0xff

// V_ASN1_UNDEF is used in some APIs to indicate an ASN.1 element is omitted.
#define V_ASN1_UNDEF (-1)

// V_ASN1_OTHER is used in |ASN1_TYPE| to indicate a non-universal ASN.1 type.
#define V_ASN1_OTHER (-3)

// V_ASN1_ANY is used by the ASN.1 templates to indicate an ANY type.
#define V_ASN1_ANY (-4)

// The following constants are tag numbers for universal types.
#define V_ASN1_EOC 0
#define V_ASN1_BOOLEAN 1
#define V_ASN1_INTEGER 2
#define V_ASN1_BIT_STRING 3
#define V_ASN1_OCTET_STRING 4
#define V_ASN1_NULL 5
#define V_ASN1_OBJECT 6
#define V_ASN1_OBJECT_DESCRIPTOR 7
#define V_ASN1_EXTERNAL 8
#define V_ASN1_REAL 9
#define V_ASN1_ENUMERATED 10
#define V_ASN1_UTF8STRING 12
#define V_ASN1_SEQUENCE 16
#define V_ASN1_SET 17
#define V_ASN1_NUMERICSTRING 18
#define V_ASN1_PRINTABLESTRING 19
#define V_ASN1_T61STRING 20
#define V_ASN1_TELETEXSTRING 20
#define V_ASN1_VIDEOTEXSTRING 21
#define V_ASN1_IA5STRING 22
#define V_ASN1_UTCTIME 23
#define V_ASN1_GENERALIZEDTIME 24
#define V_ASN1_GRAPHICSTRING 25
#define V_ASN1_ISO64STRING 26
#define V_ASN1_VISIBLESTRING 26
#define V_ASN1_GENERALSTRING 27
#define V_ASN1_UNIVERSALSTRING 28
#define V_ASN1_BMPSTRING 30

// The following constants are used for |ASN1_STRING| values that represent
// negative INTEGER and ENUMERATED values. See |ASN1_STRING| for more details.
#define V_ASN1_NEG 0x100
#define V_ASN1_NEG_INTEGER (V_ASN1_INTEGER | V_ASN1_NEG)
#define V_ASN1_NEG_ENUMERATED (V_ASN1_ENUMERATED | V_ASN1_NEG)

// The following constants are bitmask representations of ASN.1 types.
#define B_ASN1_NUMERICSTRING 0x0001
#define B_ASN1_PRINTABLESTRING 0x0002
#define B_ASN1_T61STRING 0x0004
#define B_ASN1_TELETEXSTRING 0x0004
#define B_ASN1_VIDEOTEXSTRING 0x0008
#define B_ASN1_IA5STRING 0x0010
#define B_ASN1_GRAPHICSTRING 0x0020
#define B_ASN1_ISO64STRING 0x0040
#define B_ASN1_VISIBLESTRING 0x0040
#define B_ASN1_GENERALSTRING 0x0080
#define B_ASN1_UNIVERSALSTRING 0x0100
#define B_ASN1_OCTET_STRING 0x0200
#define B_ASN1_BIT_STRING 0x0400
#define B_ASN1_BMPSTRING 0x0800
#define B_ASN1_UNKNOWN 0x1000
#define B_ASN1_UTF8STRING 0x2000
#define B_ASN1_UTCTIME 0x4000
#define B_ASN1_GENERALIZEDTIME 0x8000
#define B_ASN1_SEQUENCE 0x10000

// ASN1_tag2str returns a string representation of |tag|, interpret as a tag
// number for a universal type, or |V_ASN1_NEG_*|.
OPENSSL_EXPORT const char *ASN1_tag2str(int tag);


// Strings.
//
// ASN.1 contains a myriad of string types, as well as types that contain data
// that may be encoded into a string. This library uses a single type,
// |ASN1_STRING|, to represent most values.

// An asn1_string_st (aka |ASN1_STRING|) represents a value of a string-like
// ASN.1 type. It contains a type field, and a byte string data field with a
// type-specific representation.
//
// When representing a string value, the type field is one of
// |V_ASN1_OCTET_STRING|, |V_ASN1_UTF8STRING|, |V_ASN1_NUMERICSTRING|,
// |V_ASN1_PRINTABLESTRING|, |V_ASN1_T61STRING|, |V_ASN1_VIDEOTEXSTRING|,
// |V_ASN1_IA5STRING|, |V_ASN1_GRAPHICSTRING|, |V_ASN1_ISO64STRING|,
// |V_ASN1_VISIBLESTRING|, |V_ASN1_GENERALSTRING|, |V_ASN1_UNIVERSALSTRING|, or
// |V_ASN1_BMPSTRING|. The data contains the byte representation of of the
// string.
//
// When representing a BIT STRING value, the type field is |V_ASN1_BIT_STRING|.
// See bit string documentation below for how the data and flags are used.
//
// When representing an INTEGER or ENUMERATED value, the type field is one of
// |V_ASN1_INTEGER|, |V_ASN1_NEG_INTEGER|, |V_ASN1_ENUMERATED|, or
// |V_ASN1_NEG_ENUMERATED|. See integer documentation below for details.
//
// When representing a GeneralizedTime or UTCTime value, the type field is
// |V_ASN1_GENERALIZEDTIME| or |V_ASN1_UTCTIME|, respectively. The data contains
// the DER encoding of the value. For example, the UNIX epoch would be
// "19700101000000Z" for a GeneralizedTime and "700101000000Z" for a UTCTime.
//
// |ASN1_STRING|, when stored in an |ASN1_TYPE|, may also represent an element
// with tag not directly supported by this library. See |ASN1_TYPE| for details.
//
// |ASN1_STRING| additionally has the following typedefs: |ASN1_BIT_STRING|,
// |ASN1_BMPSTRING|, |ASN1_ENUMERATED|, |ASN1_GENERALIZEDTIME|,
// |ASN1_GENERALSTRING|, |ASN1_IA5STRING|, |ASN1_INTEGER|, |ASN1_OCTET_STRING|,
// |ASN1_PRINTABLESTRING|, |ASN1_T61STRING|, |ASN1_TIME|,
// |ASN1_UNIVERSALSTRING|, |ASN1_UTCTIME|, |ASN1_UTF8STRING|, and
// |ASN1_VISIBLESTRING|. Other than |ASN1_TIME|, these correspond to universal
// ASN.1 types. |ASN1_TIME| represents a CHOICE of UTCTime and GeneralizedTime,
// with a cutoff of 2049, as used in Section 4.1.2.5 of RFC 5280.
//
// For clarity, callers are encouraged to use the appropriate typedef when
// available. They are the same type as |ASN1_STRING|, so a caller may freely
// pass them into functions expecting |ASN1_STRING|, such as
// |ASN1_STRING_length|.
//
// If a function returns an |ASN1_STRING| where the typedef or ASN.1 structure
// implies constraints on the type field, callers may assume that the type field
// is correct. However, if a function takes an |ASN1_STRING| as input, callers
// must ensure the type field matches. These invariants are not captured by the
// C type system and may not be checked at runtime. For example, callers may
// assume the output of |X509_get0_serialNumber| has type |V_ASN1_INTEGER| or
// |V_ASN1_NEG_INTEGER|. Callers must not pass a string of type
// |V_ASN1_OCTET_STRING| to |X509_set_serialNumber|. Doing so may break
// invariants on the |X509| object and break the |X509_get0_serialNumber|
// invariant.
//
// TODO(davidben): This is very unfriendly. Getting the type field wrong should
// not cause memory errors, but it may do strange things. We should add runtime
// checks to anything that consumes |ASN1_STRING|s from the caller.
struct asn1_string_st {
  int length;
  int type;
  unsigned char *data;
  long flags;
};

// ASN1_STRING_FLAG_BITS_LEFT indicates, in a BIT STRING |ASN1_STRING|, that
// flags & 0x7 contains the number of padding bits added to the BIT STRING
// value. When not set, all trailing zero bits in the last byte are implicitly
// treated as padding. This behavior is deprecated and should not be used.
#define ASN1_STRING_FLAG_BITS_LEFT 0x08

// ASN1_STRING_type_new returns a newly-allocated empty |ASN1_STRING| object of
// type |type|, or NULL on error.
OPENSSL_EXPORT ASN1_STRING *ASN1_STRING_type_new(int type);

// ASN1_STRING_new returns a newly-allocated empty |ASN1_STRING| object with an
// arbitrary type. Prefer one of the type-specific constructors, such as
// |ASN1_OCTET_STRING_new|, or |ASN1_STRING_type_new|.
OPENSSL_EXPORT ASN1_STRING *ASN1_STRING_new(void);

// ASN1_STRING_free releases memory associated with |str|.
OPENSSL_EXPORT void ASN1_STRING_free(ASN1_STRING *str);

// ASN1_STRING_copy sets |dst| to a copy of |str|. It returns one on success and
// zero on error.
OPENSSL_EXPORT int ASN1_STRING_copy(ASN1_STRING *dst, const ASN1_STRING *str);

// ASN1_STRING_dup returns a newly-allocated copy of |str|, or NULL on error.
OPENSSL_EXPORT ASN1_STRING *ASN1_STRING_dup(const ASN1_STRING *str);

// ASN1_STRING_type returns the type of |str|. This value will be one of the
// |V_ASN1_*| constants.
OPENSSL_EXPORT int ASN1_STRING_type(const ASN1_STRING *str);

// ASN1_STRING_get0_data returns a pointer to |str|'s contents. Callers should
// use |ASN1_STRING_length| to determine the length of the string. The string
// may have embedded NUL bytes and may not be NUL-terminated.
OPENSSL_EXPORT const unsigned char *ASN1_STRING_get0_data(
    const ASN1_STRING *str);

// ASN1_STRING_data returns a mutable pointer to |str|'s contents. Callers
// should use |ASN1_STRING_length| to determine the length of the string. The
// string may have embedded NUL bytes and may not be NUL-terminated.
//
// Prefer |ASN1_STRING_get0_data|.
OPENSSL_EXPORT unsigned char *ASN1_STRING_data(ASN1_STRING *str);

// ASN1_STRING_length returns the length of |str|, in bytes.
OPENSSL_EXPORT int ASN1_STRING_length(const ASN1_STRING *str);

// ASN1_STRING_cmp compares |a| and |b|'s type and contents. It returns an
// integer equal to, less than, or greater than zero if |a| is equal to, less
// than, or greater than |b|, respectively. This function compares by length,
// then data, then type. Note the data compared is the |ASN1_STRING| internal
// representation and the type order is arbitrary. While this comparison is
// suitable for sorting, callers should not rely on the exact order when |a|
// and |b| are different types.
//
// If |a| or |b| are BIT STRINGs, this function does not compare the
// |ASN1_STRING_FLAG_BITS_LEFT| flags. Additionally, if |a| and |b| are
// INTEGERs, this comparison does not order the values numerically. For a
// numerical comparison, use |ASN1_INTEGER_cmp|.
//
// TODO(davidben): The BIT STRING comparison seems like a bug. Fix it?
OPENSSL_EXPORT int ASN1_STRING_cmp(const ASN1_STRING *a, const ASN1_STRING *b);

// ASN1_STRING_set sets the contents of |str| to a copy of |len| bytes from
// |data|. It returns one on success and zero on error.
OPENSSL_EXPORT int ASN1_STRING_set(ASN1_STRING *str, const void *data, int len);

// ASN1_STRING_set0 sets the contents of |str| to |len| bytes from |data|. It
// takes ownership of |data|, which must have been allocated with
// |OPENSSL_malloc|.
OPENSSL_EXPORT void ASN1_STRING_set0(ASN1_STRING *str, void *data, int len);

// ASN1_STRING_to_UTF8 converts |in| to UTF-8. On success, sets |*out| to a
// newly-allocated buffer containing the resulting string and returns the length
// of the string. The caller must call |OPENSSL_free| to release |*out| when
// done. On error, it returns a negative number.
OPENSSL_EXPORT int ASN1_STRING_to_UTF8(unsigned char **out,
                                       const ASN1_STRING *in);

// The following formats define encodings for use with functions like
// |ASN1_mbstring_copy|.
#define MBSTRING_FLAG 0x1000
#define MBSTRING_UTF8 (MBSTRING_FLAG)
// |MBSTRING_ASC| refers to Latin-1, not ASCII.
#define MBSTRING_ASC (MBSTRING_FLAG | 1)
#define MBSTRING_BMP (MBSTRING_FLAG | 2)
#define MBSTRING_UNIV (MBSTRING_FLAG | 4)

// DIRSTRING_TYPE contains the valid string types in an X.509 DirectoryString.
#define DIRSTRING_TYPE                                            \
  (B_ASN1_PRINTABLESTRING | B_ASN1_T61STRING | B_ASN1_BMPSTRING | \
   B_ASN1_UTF8STRING)

// PKCS9STRING_TYPE contains the valid string types in a PKCS9String.
#define PKCS9STRING_TYPE (DIRSTRING_TYPE | B_ASN1_IA5STRING)

// ASN1_mbstring_copy converts |len| bytes from |in| to an ASN.1 string. If
// |len| is -1, |in| must be NUL-terminated and the length is determined by
// |strlen|. |in| is decoded according to |inform|, which must be one of
// |MBSTRING_*|. |mask| determines the set of valid output types and is a
// bitmask containing a subset of |B_ASN1_PRINTABLESTRING|, |B_ASN1_IA5STRING|,
// |B_ASN1_T61STRING|, |B_ASN1_BMPSTRING|, |B_ASN1_UNIVERSALSTRING|, and
// |B_ASN1_UTF8STRING|, in that preference order. This function chooses the
// first output type in |mask| which can represent |in|. It interprets T61String
// as Latin-1, rather than T.61.
//
// If |mask| is zero, |DIRSTRING_TYPE| is used by default.
//
// On success, this function returns the |V_ASN1_*| constant corresponding to
// the selected output type and, if |out| and |*out| are both non-NULL, updates
// the object at |*out| with the result. If |out| is non-NULL and |*out| is
// NULL, it instead sets |*out| to a newly-allocated |ASN1_STRING| containing
// the result. If |out| is NULL, it returns the selected output type without
// constructing an |ASN1_STRING|. On error, this function returns -1.
OPENSSL_EXPORT int ASN1_mbstring_copy(ASN1_STRING **out, const uint8_t *in,
                                      int len, int inform, unsigned long mask);

// ASN1_mbstring_ncopy behaves like |ASN1_mbstring_copy| but returns an error if
// the input is less than |minsize| or greater than |maxsize| codepoints long. A
// |maxsize| value of zero is ignored. Note the sizes are measured in
// codepoints, not output bytes.
OPENSSL_EXPORT int ASN1_mbstring_ncopy(ASN1_STRING **out, const uint8_t *in,
                                       int len, int inform, unsigned long mask,
                                       long minsize, long maxsize);

// TODO(davidben): Expand and document function prototypes generated in macros.


// Bit strings.
//
// An ASN.1 BIT STRING type represents a string of bits. The string may not
// necessarily be a whole number of bytes. BIT STRINGs occur in ASN.1 structures
// in several forms:
//
// Some BIT STRINGs represent a bitmask of named bits, such as the X.509 key
// usage extension in RFC 5280, section 4.2.1.3. For such bit strings, DER
// imposes an additional restriction that trailing zero bits are removed. Some
// functions like |ASN1_BIT_STRING_set_bit| help in maintaining this.
//
// Other BIT STRINGs are arbitrary strings of bits used as identifiers and do
// not have this constraint, such as the X.509 issuerUniqueID field.
//
// Finally, some structures use BIT STRINGs as a container for byte strings. For
// example, the signatureValue field in X.509 and the subjectPublicKey field in
// SubjectPublicKeyInfo are defined as BIT STRINGs with a value specific to the
// AlgorithmIdentifier. While some unknown algorithm could choose to store
// arbitrary bit strings, all supported algorithms use a byte string, with bit
// order matching the DER encoding. Callers interpreting a BIT STRING as a byte
// string should use |ASN1_BIT_STRING_num_bytes| instead of |ASN1_STRING_length|
// and reject bit strings that are not a whole number of bytes.
//
// This library represents BIT STRINGs as |ASN1_STRING|s with type
// |V_ASN1_BIT_STRING|. The data contains the encoded form of the BIT STRING,
// including any padding bits added to round to a whole number of bytes, but
// excluding the leading byte containing the number of padding bits. If
// |ASN1_STRING_FLAG_BITS_LEFT| is set, the bottom three bits contains the
// number of padding bits. For example, DER encodes the BIT STRING {1, 0} as
// {0x06, 0x80 = 0b10_000000}. The |ASN1_STRING| representation has data of
// {0x80} and flags of ASN1_STRING_FLAG_BITS_LEFT | 6. If
// |ASN1_STRING_FLAG_BITS_LEFT| is unset, trailing zero bits are implicitly
// removed. Callers should not rely this representation when constructing bit
// strings.

// ASN1_BIT_STRING_num_bytes computes the length of |str| in bytes. If |str|'s
// bit length is a multiple of 8, it sets |*out| to the byte length and returns
// one. Otherwise, it returns zero.
//
// This function may be used with |ASN1_STRING_get0_data| to interpret |str| as
// a byte string.
OPENSSL_EXPORT int ASN1_BIT_STRING_num_bytes(const ASN1_BIT_STRING *str,
                                             size_t *out);

// ASN1_BIT_STRING_set calls |ASN1_STRING_set|. It leaves flags unchanged, so
// the caller must set the number of unused bits.
//
// TODO(davidben): Maybe it should? Wrapping a byte string in a bit string is a
// common use case.
OPENSSL_EXPORT int ASN1_BIT_STRING_set(ASN1_BIT_STRING *str,
                                       const unsigned char *d, int length);

// ASN1_BIT_STRING_set_bit sets bit |n| of |str| to one if |value| is non-zero
// and zero if |value| is zero, resizing |str| as needed. It then truncates
// trailing zeros in |str| to align with the DER represention for a bit string
// with named bits. It returns one on success and zero on error. |n| is indexed
// beginning from zero.
OPENSSL_EXPORT int ASN1_BIT_STRING_set_bit(ASN1_BIT_STRING *str, int n,
                                           int value);

// ASN1_BIT_STRING_get_bit returns one if bit |n| of |a| is in bounds and set,
// and zero otherwise. |n| is indexed beginning from zero.
OPENSSL_EXPORT int ASN1_BIT_STRING_get_bit(const ASN1_BIT_STRING *str, int n);

// ASN1_BIT_STRING_check returns one if |str| only contains bits that are set in
// the |flags_len| bytes pointed by |flags|. Otherwise it returns zero. Bits in
// |flags| are arranged according to the DER representation, so bit 0
// corresponds to the MSB of |flags[0]|.
OPENSSL_EXPORT int ASN1_BIT_STRING_check(const ASN1_BIT_STRING *str,
                                         const unsigned char *flags,
                                         int flags_len);

// TODO(davidben): Expand and document function prototypes generated in macros.


// Integers and enumerated values.
//
// INTEGER and ENUMERATED values are represented as |ASN1_STRING|s where the
// data contains the big-endian encoding of the absolute value of the integer.
// The sign bit is encoded in the type: non-negative values have a type of
// |V_ASN1_INTEGER| or |V_ASN1_ENUMERATED|, while negative values have a type of
// |V_ASN1_NEG_INTEGER| or |V_ASN1_NEG_ENUMERATED|. Note this differs from DER's
// two's complement representation.

// ASN1_INTEGER_set sets |a| to an INTEGER with value |v|. It returns one on
// success and zero on error.
OPENSSL_EXPORT int ASN1_INTEGER_set(ASN1_INTEGER *a, long v);

// ASN1_INTEGER_set sets |a| to an INTEGER with value |v|. It returns one on
// success and zero on error.
OPENSSL_EXPORT int ASN1_INTEGER_set_uint64(ASN1_INTEGER *out, uint64_t v);

// ASN1_INTEGER_get returns the value of |a| as a |long|, or -1 if |a| is out of
// range or the wrong type.
OPENSSL_EXPORT long ASN1_INTEGER_get(const ASN1_INTEGER *a);

// BN_to_ASN1_INTEGER sets |ai| to an INTEGER with value |bn| and returns |ai|
// on success or NULL or error. If |ai| is NULL, it returns a newly-allocated
// |ASN1_INTEGER| on success instead, which the caller must release with
// |ASN1_INTEGER_free|.
OPENSSL_EXPORT ASN1_INTEGER *BN_to_ASN1_INTEGER(const BIGNUM *bn,
                                                ASN1_INTEGER *ai);

// ASN1_INTEGER_to_BN sets |bn| to the value of |ai| and returns |bn| on success
// or NULL or error. If |bn| is NULL, it returns a newly-allocated |BIGNUM| on
// success instead, which the caller must release with |BN_free|.
OPENSSL_EXPORT BIGNUM *ASN1_INTEGER_to_BN(const ASN1_INTEGER *ai, BIGNUM *bn);

// ASN1_INTEGER_cmp compares the values of |x| and |y|. It returns an integer
// equal to, less than, or greater than zero if |x| is equal to, less than, or
// greater than |y|, respectively.
OPENSSL_EXPORT int ASN1_INTEGER_cmp(const ASN1_INTEGER *x,
                                    const ASN1_INTEGER *y);

// ASN1_ENUMERATED_set sets |a| to an ENUMERATED with value |v|. It returns one
// on success and zero on error.
OPENSSL_EXPORT int ASN1_ENUMERATED_set(ASN1_ENUMERATED *a, long v);

// ASN1_INTEGER_get returns the value of |a| as a |long|, or -1 if |a| is out of
// range or the wrong type.
OPENSSL_EXPORT long ASN1_ENUMERATED_get(const ASN1_ENUMERATED *a);

// BN_to_ASN1_ENUMERATED sets |ai| to an ENUMERATED with value |bn| and returns
// |ai| on success or NULL or error. If |ai| is NULL, it returns a
// newly-allocated |ASN1_INTEGER| on success instead, which the caller must
// release with |ASN1_INTEGER_free|.
OPENSSL_EXPORT ASN1_ENUMERATED *BN_to_ASN1_ENUMERATED(const BIGNUM *bn,
                                                      ASN1_ENUMERATED *ai);

// ASN1_ENUMERATED_to_BN sets |bn| to the value of |ai| and returns |bn| on
// success or NULL or error. If |bn| is NULL, it returns a newly-allocated
// |BIGNUM| on success instead, which the caller must release with |BN_free|.
OPENSSL_EXPORT BIGNUM *ASN1_ENUMERATED_to_BN(const ASN1_ENUMERATED *ai,
                                             BIGNUM *bn);

// TODO(davidben): Expand and document function prototypes generated in macros.


// Time.
//
// GeneralizedTime and UTCTime values are represented as |ASN1_STRING|s. The
// type field is |V_ASN1_GENERALIZEDTIME| or |V_ASN1_UTCTIME|, respectively. The
// data field contains the DER encoding of the value. For example, the UNIX
// epoch would be "19700101000000Z" for a GeneralizedTime and "700101000000Z"
// for a UTCTime.
//
// ASN.1 does not define how to interpret UTCTime's two-digit year. RFC 5280
// defines it as a range from 1950 to 2049 for X.509. The library uses the
// RFC 5280 interpretation. It does not currently enforce the restrictions from
// BER, and the additional restrictions from RFC 5280, but future versions may.
// Callers should not rely on fractional seconds and non-UTC time zones.
//
// The |ASN1_TIME| typedef represents the X.509 Time type, which is a CHOICE of
// GeneralizedTime and UTCTime, using UTCTime when the value is in range.

// ASN1_UTCTIME_check returns one if |a| is a valid UTCTime and zero otherwise.
OPENSSL_EXPORT int ASN1_UTCTIME_check(const ASN1_UTCTIME *a);

// ASN1_UTCTIME_set represents |t| as a UTCTime and writes the result to |s|. It
// returns |s| on success and NULL on error. If |s| is NULL, it returns a
// newly-allocated |ASN1_UTCTIME| instead.
//
// Note this function may fail if the time is out of range for UTCTime.
OPENSSL_EXPORT ASN1_UTCTIME *ASN1_UTCTIME_set(ASN1_UTCTIME *s, time_t t);

// ASN1_UTCTIME_adj adds |offset_day| days and |offset_sec| seconds to |t| and
// writes the result to |s| as a UTCTime. It returns |s| on success and NULL on
// error. If |s| is NULL, it returns a newly-allocated |ASN1_UTCTIME| instead.
//
// Note this function may fail if the time overflows or is out of range for
// UTCTime.
OPENSSL_EXPORT ASN1_UTCTIME *ASN1_UTCTIME_adj(ASN1_UTCTIME *s, time_t t,
                                              int offset_day, long offset_sec);

// ASN1_UTCTIME_set_string sets |s| to a UTCTime whose contents are a copy of
// |str|. It returns one on success and zero on error or if |str| is not a valid
// UTCTime.
//
// If |s| is NULL, this function validates |str| without copying it.
OPENSSL_EXPORT int ASN1_UTCTIME_set_string(ASN1_UTCTIME *s, const char *str);

// ASN1_UTCTIME_cmp_time_t compares |s| to |t|. It returns -1 if |s| < |t|, 0 if
// they are equal, 1 if |s| > |t|, and -2 on error.
OPENSSL_EXPORT int ASN1_UTCTIME_cmp_time_t(const ASN1_UTCTIME *s, time_t t);

// ASN1_GENERALIZEDTIME_check returns one if |a| is a valid GeneralizedTime and
// zero otherwise.
OPENSSL_EXPORT int ASN1_GENERALIZEDTIME_check(const ASN1_GENERALIZEDTIME *a);

// ASN1_GENERALIZEDTIME_set represents |t| as a GeneralizedTime and writes the
// result to |s|. It returns |s| on success and NULL on error. If |s| is NULL,
// it returns a newly-allocated |ASN1_GENERALIZEDTIME| instead.
//
// Note this function may fail if the time is out of range for GeneralizedTime.
OPENSSL_EXPORT ASN1_GENERALIZEDTIME *ASN1_GENERALIZEDTIME_set(
    ASN1_GENERALIZEDTIME *s, time_t t);

// ASN1_GENERALIZEDTIME_adj adds |offset_day| days and |offset_sec| seconds to
// |t| and writes the result to |s| as a GeneralizedTime. It returns |s| on
// success and NULL on error. If |s| is NULL, it returns a newly-allocated
// |ASN1_GENERALIZEDTIME| instead.
//
// Note this function may fail if the time overflows or is out of range for
// GeneralizedTime.
OPENSSL_EXPORT ASN1_GENERALIZEDTIME *ASN1_GENERALIZEDTIME_adj(
    ASN1_GENERALIZEDTIME *s, time_t t, int offset_day, long offset_sec);

// ASN1_GENERALIZEDTIME_set_string sets |s| to a GeneralizedTime whose contents
// are a copy of |str|. It returns one on success and zero on error or if |str|
// is not a valid GeneralizedTime.
//
// If |s| is NULL, this function validates |str| without copying it.
OPENSSL_EXPORT int ASN1_GENERALIZEDTIME_set_string(ASN1_GENERALIZEDTIME *s,
                                                   const char *str);

// ASN1_TIME_diff computes |to| - |from|. On success, it sets |*out_days| to the
// difference in days, rounded towards zero, sets |*out_seconds| to the
// remainder, and returns one. On error, it returns zero.
//
// If |from| is before |to|, both outputs will be <= 0, with at least one
// negative. If |from| is after |to|, both will be >= 0, with at least one
// positive. If they are equal, ignoring fractional seconds, both will be zero.
//
// Note this function may fail on overflow, or if |from| or |to| cannot be
// decoded.
OPENSSL_EXPORT int ASN1_TIME_diff(int *out_days, int *out_seconds,
                                  const ASN1_TIME *from, const ASN1_TIME *to);

// ASN1_TIME_set represents |t| as a GeneralizedTime or UTCTime and writes
// the result to |s|. As in RFC 5280, section 4.1.2.5, it uses UTCTime when the
// time fits and GeneralizedTime otherwise. It returns |s| on success and NULL
// on error. If |s| is NULL, it returns a newly-allocated |ASN1_TIME| instead.
//
// Note this function may fail if the time is out of range for GeneralizedTime.
OPENSSL_EXPORT ASN1_TIME *ASN1_TIME_set(ASN1_TIME *s, time_t t);

// ASN1_TIME_adj adds |offset_day| days and |offset_sec| seconds to
// |t| and writes the result to |s|. As in RFC 5280, section 4.1.2.5, it uses
// UTCTime when the time fits and GeneralizedTime otherwise. It returns |s| on
// success and NULL on error. If |s| is NULL, it returns a newly-allocated
// |ASN1_GENERALIZEDTIME| instead.
//
// Note this function may fail if the time overflows or is out of range for
// GeneralizedTime.
OPENSSL_EXPORT ASN1_TIME *ASN1_TIME_adj(ASN1_TIME *s, time_t t, int offset_day,
                                        long offset_sec);

// ASN1_TIME_check returns one if |t| is a valid UTCTime or GeneralizedTime, and
// zero otherwise. |t|'s type determines which check is performed. This
// function does not enforce that UTCTime was used when possible.
OPENSSL_EXPORT int ASN1_TIME_check(const ASN1_TIME *t);

// ASN1_TIME_to_generalizedtime converts |t| to a GeneralizedTime. If |out| is
// NULL, it returns a newly-allocated |ASN1_GENERALIZEDTIME| on success, or NULL
// on error. If |out| is non-NULL and |*out| is NULL, it additionally sets
// |*out| to the result. If |out| and |*out| are non-NULL, it instead updates
// the object pointed by |*out| and returns |*out| on success or NULL on error.
OPENSSL_EXPORT ASN1_GENERALIZEDTIME *ASN1_TIME_to_generalizedtime(
    const ASN1_TIME *t, ASN1_GENERALIZEDTIME **out);

// ASN1_TIME_set_string behaves like |ASN1_UTCTIME_set_string| if |str| is a
// valid UTCTime, and |ASN1_GENERALIZEDTIME_set_string| if |str| is a valid
// GeneralizedTime. If |str| is neither, it returns zero.
OPENSSL_EXPORT int ASN1_TIME_set_string(ASN1_TIME *s, const char *str);

// TODO(davidben): Expand and document function prototypes generated in macros.


// Arbitrary elements.

// ASN1_VALUE_st (aka |ASN1_VALUE|) is an opaque type used internally in the
// library.
typedef struct ASN1_VALUE_st ASN1_VALUE;

// An asn1_type_st (aka |ASN1_TYPE|) represents an arbitrary ASN.1 element,
// typically used used for ANY types. It contains a |type| field and a |value|
// union dependent on |type|.
//
// WARNING: This struct has a complex representation. Callers must not construct
// |ASN1_TYPE| values manually. Use |ASN1_TYPE_set| and |ASN1_TYPE_set1|
// instead. Additionally, callers performing non-trivial operations on this type
// are encouraged to use |CBS| and |CBB| from <openssl/bytestring.h>, and
// convert to or from |ASN1_TYPE| with |d2i_ASN1_TYPE| or |i2d_ASN1_TYPE|.
//
// The |type| field corresponds to the tag of the ASN.1 element being
// represented:
//
// If |type| is a |V_ASN1_*| constant for an ASN.1 string-like type, as defined
// by |ASN1_STRING|, the tag matches the constant. |value| contains an
// |ASN1_STRING| pointer (equivalently, one of the more specific typedefs). See
// |ASN1_STRING| for details on the representation. Unlike |ASN1_STRING|,
// |ASN1_TYPE| does not use the |V_ASN1_NEG| flag for negative INTEGER and
// ENUMERATE values. For a negative value, the |ASN1_TYPE|'s |type| will be
// |V_ASN1_INTEGER| or |V_ASN1_ENUMERATED|, but |value| will an |ASN1_STRING|
// whose |type| is |V_ASN1_NEG_INTEGER| or |V_ASN1_NEG_ENUMERATED|.
//
// If |type| is |V_ASN1_OBJECT|, the tag is OBJECT IDENTIFIER and |value|
// contains an |ASN1_OBJECT| pointer.
//
// If |type| is |V_ASN1_NULL|, the tag is NULL. |value| contains a NULL pointer.
//
// If |type| is |V_ASN1_BOOLEAN|, the tag is BOOLEAN. |value| contains an
// |ASN1_BOOLEAN|.
//
// If |type| is |V_ASN1_SEQUENCE|, |V_ASN1_SET|, or |V_ASN1_OTHER|, the tag is
// SEQUENCE, SET, or some non-universal tag, respectively. |value| is an
// |ASN1_STRING| containing the entire element, including the tag and length.
// The |ASN1_STRING|'s |type| field matches the containing |ASN1_TYPE|'s |type|.
//
// Other positive values of |type|, up to |V_ASN1_MAX_UNIVERSAL|, correspond to
// universal primitive tags not directly supported by this library. |value| is
// an |ASN1_STRING| containing the body of the element, excluding the tag
// and length. The |ASN1_STRING|'s |type| field matches the containing
// |ASN1_TYPE|'s |type|.
struct asn1_type_st {
  int type;
  union {
    char *ptr;
    ASN1_BOOLEAN boolean;
    ASN1_STRING *asn1_string;
    ASN1_OBJECT *object;
    ASN1_INTEGER *integer;
    ASN1_ENUMERATED *enumerated;
    ASN1_BIT_STRING *bit_string;
    ASN1_OCTET_STRING *octet_string;
    ASN1_PRINTABLESTRING *printablestring;
    ASN1_T61STRING *t61string;
    ASN1_IA5STRING *ia5string;
    ASN1_GENERALSTRING *generalstring;
    ASN1_BMPSTRING *bmpstring;
    ASN1_UNIVERSALSTRING *universalstring;
    ASN1_UTCTIME *utctime;
    ASN1_GENERALIZEDTIME *generalizedtime;
    ASN1_VISIBLESTRING *visiblestring;
    ASN1_UTF8STRING *utf8string;
    // set and sequence are left complete and still contain the entire element.
    ASN1_STRING *set;
    ASN1_STRING *sequence;
    ASN1_VALUE *asn1_value;
  } value;
};

// ASN1_TYPE_get returns the type of |a|, which will be one of the |V_ASN1_*|
// constants, or zero if |a| is not fully initialized.
OPENSSL_EXPORT int ASN1_TYPE_get(const ASN1_TYPE *a);

// ASN1_TYPE_set sets |a| to an |ASN1_TYPE| of type |type| and value |value|,
// releasing the previous contents of |a|.
//
// If |type| is |V_ASN1_BOOLEAN|, |a| is set to FALSE if |value| is NULL and
// TRUE otherwise. If setting |a| to TRUE, |value| may be an invalid pointer,
// such as (void*)1.
//
// If |type| is |V_ASN1_NULL|, |value| must be NULL.
//
// For other values of |type|, this function takes ownership of |value|, which
// must point to an object of the corresponding type. See |ASN1_TYPE| for
// details.
OPENSSL_EXPORT void ASN1_TYPE_set(ASN1_TYPE *a, int type, void *value);

// ASN1_TYPE_set1 behaves like |ASN1_TYPE_set| except it does not take ownership
// of |value|. It returns one on success and zero on error.
OPENSSL_EXPORT int ASN1_TYPE_set1(ASN1_TYPE *a, int type, const void *value);

// ASN1_TYPE_cmp returns zero if |a| and |b| are equal and some non-zero value
// otherwise. Note this function can only be used for equality checks, not an
// ordering.
OPENSSL_EXPORT int ASN1_TYPE_cmp(const ASN1_TYPE *a, const ASN1_TYPE *b);

// TODO(davidben): Most of |ASN1_TYPE|'s APIs are hidden behind macros. Expand
// the macros, document them, and move them to this section.


// Human-readable output.
//
// The following functions output types in some human-readable format. These
// functions may be used for debugging and logging. However, the output should
// not be consumed programmatically. They may be ambiguous or lose information.

// ASN1_UTCTIME_print writes a human-readable representation of |a| to |out|. It
// returns one on success and zero on error.
OPENSSL_EXPORT int ASN1_UTCTIME_print(BIO *out, const ASN1_UTCTIME *a);

// ASN1_GENERALIZEDTIME_print writes a human-readable representation of |a| to
// |out|. It returns one on success and zero on error.
OPENSSL_EXPORT int ASN1_GENERALIZEDTIME_print(BIO *out,
                                              const ASN1_GENERALIZEDTIME *a);

// ASN1_TIME_print writes a human-readable representation of |a| to |out|. It
// returns one on success and zero on error.
OPENSSL_EXPORT int ASN1_TIME_print(BIO *out, const ASN1_TIME *a);

// ASN1_STRING_print writes a human-readable representation of |str| to |out|.
// It returns one on success and zero on error. Unprintable characters are
// replaced with '.'.
OPENSSL_EXPORT int ASN1_STRING_print(BIO *out, const ASN1_STRING *str);

// ASN1_STRFLGS_ESC_2253 causes characters to be escaped as in RFC 2253, section
// 2.4.
#define ASN1_STRFLGS_ESC_2253 1

// ASN1_STRFLGS_ESC_CTRL causes all control characters to be escaped.
#define ASN1_STRFLGS_ESC_CTRL 2

// ASN1_STRFLGS_ESC_MSB causes all characters above 127 to be escaped.
#define ASN1_STRFLGS_ESC_MSB 4

// ASN1_STRFLGS_ESC_QUOTE causes the string to be surrounded by quotes, rather
// than using backslashes, when characters are escaped. Fewer characters will
// require escapes in this case.
#define ASN1_STRFLGS_ESC_QUOTE 8

// ASN1_STRFLGS_UTF8_CONVERT causes the string to be encoded as UTF-8, with each
// byte in the UTF-8 encoding treated as an individual character for purposes of
// escape sequences. If not set, each Unicode codepoint in the string is treated
// as a character, with wide characters escaped as "\Uxxxx" or "\Wxxxxxxxx".
// Note this can be ambiguous if |ASN1_STRFLGS_ESC_*| are all unset. In that
// case, backslashes are not escaped, but wide characters are.
#define ASN1_STRFLGS_UTF8_CONVERT 0x10

// ASN1_STRFLGS_IGNORE_TYPE causes the string type to be ignored. The
// |ASN1_STRING| in-memory representation will be printed directly.
#define ASN1_STRFLGS_IGNORE_TYPE 0x20

// ASN1_STRFLGS_SHOW_TYPE causes the string type to be included in the output.
#define ASN1_STRFLGS_SHOW_TYPE 0x40

// ASN1_STRFLGS_DUMP_ALL causes all strings to be printed as a hexdump, using
// RFC 2253 hexstring notation, such as "#0123456789ABCDEF".
#define ASN1_STRFLGS_DUMP_ALL 0x80

// ASN1_STRFLGS_DUMP_UNKNOWN behaves like |ASN1_STRFLGS_DUMP_ALL| but only
// applies to values of unknown type. If unset, unknown values will print
// their contents as single-byte characters with escape sequences.
#define ASN1_STRFLGS_DUMP_UNKNOWN 0x100

// ASN1_STRFLGS_DUMP_DER causes hexdumped strings (as determined by
// |ASN1_STRFLGS_DUMP_ALL| or |ASN1_STRFLGS_DUMP_UNKNOWN|) to print the entire
// DER element as in RFC 2253, rather than only the contents of the
// |ASN1_STRING|.
#define ASN1_STRFLGS_DUMP_DER 0x200

// ASN1_STRFLGS_RFC2253 causes the string to be escaped as in RFC 2253,
// additionally escaping control characters.
#define ASN1_STRFLGS_RFC2253                                              \
  (ASN1_STRFLGS_ESC_2253 | ASN1_STRFLGS_ESC_CTRL | ASN1_STRFLGS_ESC_MSB | \
   ASN1_STRFLGS_UTF8_CONVERT | ASN1_STRFLGS_DUMP_UNKNOWN |                \
   ASN1_STRFLGS_DUMP_DER)

// ASN1_STRING_print_ex writes a human-readable representation of |str| to
// |out|. It returns the number of bytes written on success and -1 on error. If
// |out| is NULL, it returns the number of bytes it would have written, without
// writing anything.
//
// The |flags| should be a combination of combination of |ASN1_STRFLGS_*|
// constants. See the documentation for each flag for how it controls the
// output. If unsure, use |ASN1_STRFLGS_RFC2253|.
OPENSSL_EXPORT int ASN1_STRING_print_ex(BIO *out, const ASN1_STRING *str,
                                        unsigned long flags);

// ASN1_STRING_print_ex_fp behaves like |ASN1_STRING_print_ex| but writes to a
// |FILE| rather than a |BIO|.
OPENSSL_EXPORT int ASN1_STRING_print_ex_fp(FILE *fp, const ASN1_STRING *str,
                                           unsigned long flags);


// Underdocumented functions.
//
// The following functions are not yet documented and organized.

DEFINE_STACK_OF(ASN1_OBJECT)

// ASN1_ENCODING structure: this is used to save the received
// encoding of an ASN1 type. This is useful to get round
// problems with invalid encodings which can break signatures.

typedef struct ASN1_ENCODING_st {
  unsigned char *enc;  // DER encoding
  long len;            // Length of encoding
  int modified;        // set to 1 if 'enc' is invalid
  // alias_only is zero if |enc| owns the buffer that it points to
  // (although |enc| may still be NULL). If one, |enc| points into a
  // buffer that is owned elsewhere.
  unsigned alias_only : 1;
  // alias_only_on_next_parse is one iff the next parsing operation
  // should avoid taking a copy of the input and rather set
  // |alias_only|.
  unsigned alias_only_on_next_parse : 1;
} ASN1_ENCODING;

#define STABLE_FLAGS_MALLOC 0x01
#define STABLE_NO_MASK 0x02

typedef struct asn1_string_table_st {
  int nid;
  long minsize;
  long maxsize;
  unsigned long mask;
  unsigned long flags;
} ASN1_STRING_TABLE;

// Declarations for template structures: for full definitions
// see asn1t.h
typedef struct ASN1_TEMPLATE_st ASN1_TEMPLATE;
typedef struct ASN1_TLC_st ASN1_TLC;

// Declare ASN1 functions: the implement macro in in asn1t.h

#define DECLARE_ASN1_FUNCTIONS(type) DECLARE_ASN1_FUNCTIONS_name(type, type)

#define DECLARE_ASN1_ALLOC_FUNCTIONS(type) \
  DECLARE_ASN1_ALLOC_FUNCTIONS_name(type, type)

#define DECLARE_ASN1_FUNCTIONS_name(type, name) \
  DECLARE_ASN1_ALLOC_FUNCTIONS_name(type, name) \
  DECLARE_ASN1_ENCODE_FUNCTIONS(type, name, name)

#define DECLARE_ASN1_FUNCTIONS_fname(type, itname, name) \
  DECLARE_ASN1_ALLOC_FUNCTIONS_name(type, name)          \
  DECLARE_ASN1_ENCODE_FUNCTIONS(type, itname, name)

#define DECLARE_ASN1_ENCODE_FUNCTIONS(type, itname, name)             \
  OPENSSL_EXPORT type *d2i_##name(type **a, const unsigned char **in, \
                                  long len);                          \
  OPENSSL_EXPORT int i2d_##name(type *a, unsigned char **out);        \
  DECLARE_ASN1_ITEM(itname)

#define DECLARE_ASN1_ENCODE_FUNCTIONS_const(type, name)               \
  OPENSSL_EXPORT type *d2i_##name(type **a, const unsigned char **in, \
                                  long len);                          \
  OPENSSL_EXPORT int i2d_##name(const type *a, unsigned char **out);  \
  DECLARE_ASN1_ITEM(name)

#define DECLARE_ASN1_FUNCTIONS_const(name) \
  DECLARE_ASN1_ALLOC_FUNCTIONS(name)       \
  DECLARE_ASN1_ENCODE_FUNCTIONS_const(name, name)

#define DECLARE_ASN1_ALLOC_FUNCTIONS_name(type, name) \
  OPENSSL_EXPORT type *name##_new(void);              \
  OPENSSL_EXPORT void name##_free(type *a);

#define DECLARE_ASN1_PRINT_FUNCTION(stname) \
  DECLARE_ASN1_PRINT_FUNCTION_fname(stname, stname)

#define DECLARE_ASN1_PRINT_FUNCTION_fname(stname, fname)                \
  OPENSSL_EXPORT int fname##_print_ctx(BIO *out, stname *x, int indent, \
                                       const ASN1_PCTX *pctx);

typedef void *d2i_of_void(void **, const unsigned char **, long);
typedef int i2d_of_void(const void *, unsigned char **);

// The following macros and typedefs allow an ASN1_ITEM
// to be embedded in a structure and referenced. Since
// the ASN1_ITEM pointers need to be globally accessible
// (possibly from shared libraries) they may exist in
// different forms. On platforms that support it the
// ASN1_ITEM structure itself will be globally exported.
// Other platforms will export a function that returns
// an ASN1_ITEM pointer.
//
// To handle both cases transparently the macros below
// should be used instead of hard coding an ASN1_ITEM
// pointer in a structure.
//
// The structure will look like this:
//
// typedef struct SOMETHING_st {
//      ...
//      ASN1_ITEM_EXP *iptr;
//      ...
// } SOMETHING;
//
// It would be initialised as e.g.:
//
// SOMETHING somevar = {...,ASN1_ITEM_ref(X509),...};
//
// and the actual pointer extracted with:
//
// const ASN1_ITEM *it = ASN1_ITEM_ptr(somevar.iptr);
//
// Finally an ASN1_ITEM pointer can be extracted from an
// appropriate reference with: ASN1_ITEM_rptr(X509). This
// would be used when a function takes an ASN1_ITEM * argument.
//

// ASN1_ITEM pointer exported type
typedef const ASN1_ITEM ASN1_ITEM_EXP;

// Macro to obtain ASN1_ITEM pointer from exported type
#define ASN1_ITEM_ptr(iptr) (iptr)

// Macro to include ASN1_ITEM pointer from base type
#define ASN1_ITEM_ref(iptr) (&(iptr##_it))

#define ASN1_ITEM_rptr(ref) (&(ref##_it))

#define DECLARE_ASN1_ITEM(name) extern OPENSSL_EXPORT const ASN1_ITEM name##_it;

DEFINE_STACK_OF(ASN1_INTEGER)

DEFINE_STACK_OF(ASN1_TYPE)

typedef STACK_OF(ASN1_TYPE) ASN1_SEQUENCE_ANY;

DECLARE_ASN1_ENCODE_FUNCTIONS_const(ASN1_SEQUENCE_ANY, ASN1_SEQUENCE_ANY)
DECLARE_ASN1_ENCODE_FUNCTIONS_const(ASN1_SEQUENCE_ANY, ASN1_SET_ANY)

// M_ASN1_* are legacy aliases for various |ASN1_STRING| functions. Use the
// functions themselves.
#define M_ASN1_STRING_length(x) ASN1_STRING_length(x)
#define M_ASN1_STRING_type(x) ASN1_STRING_type(x)
#define M_ASN1_STRING_data(x) ASN1_STRING_data(x)
#define M_ASN1_BIT_STRING_new() ASN1_BIT_STRING_new()
#define M_ASN1_BIT_STRING_free(a) ASN1_BIT_STRING_free(a)
#define M_ASN1_BIT_STRING_dup(a) ASN1_STRING_dup(a)
#define M_ASN1_BIT_STRING_cmp(a, b) ASN1_STRING_cmp(a, b)
#define M_ASN1_BIT_STRING_set(a, b, c) ASN1_BIT_STRING_set(a, b, c)
#define M_ASN1_INTEGER_new() ASN1_INTEGER_new()
#define M_ASN1_INTEGER_free(a) ASN1_INTEGER_free(a)
#define M_ASN1_INTEGER_dup(a) ASN1_INTEGER_dup(a)
#define M_ASN1_INTEGER_cmp(a, b) ASN1_INTEGER_cmp(a, b)
#define M_ASN1_ENUMERATED_new() ASN1_ENUMERATED_new()
#define M_ASN1_ENUMERATED_free(a) ASN1_ENUMERATED_free(a)
#define M_ASN1_ENUMERATED_dup(a) ASN1_STRING_dup(a)
#define M_ASN1_ENUMERATED_cmp(a, b) ASN1_STRING_cmp(a, b)
#define M_ASN1_OCTET_STRING_new() ASN1_OCTET_STRING_new()
#define M_ASN1_OCTET_STRING_free(a) ASN1_OCTET_STRING_free()
#define M_ASN1_OCTET_STRING_dup(a) ASN1_OCTET_STRING_dup(a)
#define M_ASN1_OCTET_STRING_cmp(a, b) ASN1_OCTET_STRING_cmp(a, b)
#define M_ASN1_OCTET_STRING_set(a, b, c) ASN1_OCTET_STRING_set(a, b, c)
#define M_ASN1_OCTET_STRING_print(a, b) ASN1_STRING_print(a, b)
#define M_ASN1_PRINTABLESTRING_new() ASN1_PRINTABLESTRING_new()
#define M_ASN1_PRINTABLESTRING_free(a) ASN1_PRINTABLESTRING_free(a)
#define M_ASN1_IA5STRING_new() ASN1_IA5STRING_new()
#define M_ASN1_IA5STRING_free(a) ASN1_IA5STRING_free(a)
#define M_ASN1_IA5STRING_dup(a) ASN1_STRING_dup(a)
#define M_ASN1_UTCTIME_new() ASN1_UTCTIME_new()
#define M_ASN1_UTCTIME_free(a) ASN1_UTCTIME_free(a)
#define M_ASN1_UTCTIME_dup(a) ASN1_STRING_dup(a)
#define M_ASN1_T61STRING_new() ASN1_T61STRING_new()
#define M_ASN1_T61STRING_free(a) ASN1_T61STRING_free(a)
#define M_ASN1_GENERALIZEDTIME_new() ASN1_GENERALIZEDTIME_new()
#define M_ASN1_GENERALIZEDTIME_free(a) ASN1_GENERALIZEDTIME_free(a)
#define M_ASN1_GENERALIZEDTIME_dup(a) ASN1_STRING_dup(a)
#define M_ASN1_GENERALSTRING_new() ASN1_GENERALSTRING_new()
#define M_ASN1_GENERALSTRING_free(a) ASN1_GENERALSTRING_free(a)
#define M_ASN1_UNIVERSALSTRING_new() ASN1_UNIVERSALSTRING_new()
#define M_ASN1_UNIVERSALSTRING_free(a) ASN1_UNIVERSALSTRING_free(a)
#define M_ASN1_BMPSTRING_new() ASN1_BMPSTRING_new()
#define M_ASN1_BMPSTRING_free(a) ASN1_BMPSTRING_free(a)
#define M_ASN1_VISIBLESTRING_new() ASN1_VISIBLESTRING_new()
#define M_ASN1_VISIBLESTRING_free(a) ASN1_VISIBLESTRING_free(a)
#define M_ASN1_UTF8STRING_new() ASN1_UTF8STRING_new()
#define M_ASN1_UTF8STRING_free(a) ASN1_UTF8STRING_free(a)

#define B_ASN1_TIME B_ASN1_UTCTIME | B_ASN1_GENERALIZEDTIME

#define B_ASN1_PRINTABLE                                              \
  B_ASN1_NUMERICSTRING | B_ASN1_PRINTABLESTRING | B_ASN1_T61STRING |  \
      B_ASN1_IA5STRING | B_ASN1_BIT_STRING | B_ASN1_UNIVERSALSTRING | \
      B_ASN1_BMPSTRING | B_ASN1_UTF8STRING | B_ASN1_SEQUENCE | B_ASN1_UNKNOWN

#define B_ASN1_DIRECTORYSTRING                                       \
  B_ASN1_PRINTABLESTRING | B_ASN1_TELETEXSTRING | B_ASN1_BMPSTRING | \
      B_ASN1_UNIVERSALSTRING | B_ASN1_UTF8STRING

#define B_ASN1_DISPLAYTEXT \
  B_ASN1_IA5STRING | B_ASN1_VISIBLESTRING | B_ASN1_BMPSTRING | B_ASN1_UTF8STRING

DECLARE_ASN1_FUNCTIONS_fname(ASN1_TYPE, ASN1_ANY, ASN1_TYPE)

OPENSSL_EXPORT ASN1_OBJECT *ASN1_OBJECT_new(void);
OPENSSL_EXPORT void ASN1_OBJECT_free(ASN1_OBJECT *a);
OPENSSL_EXPORT int i2d_ASN1_OBJECT(const ASN1_OBJECT *a, unsigned char **pp);
OPENSSL_EXPORT ASN1_OBJECT *c2i_ASN1_OBJECT(ASN1_OBJECT **a,
                                            const unsigned char **pp,
                                            long length);
OPENSSL_EXPORT ASN1_OBJECT *d2i_ASN1_OBJECT(ASN1_OBJECT **a,
                                            const unsigned char **pp,
                                            long length);

DECLARE_ASN1_ITEM(ASN1_OBJECT)

DECLARE_ASN1_FUNCTIONS(ASN1_BIT_STRING)
OPENSSL_EXPORT int i2c_ASN1_BIT_STRING(const ASN1_BIT_STRING *a,
                                       unsigned char **pp);
OPENSSL_EXPORT ASN1_BIT_STRING *c2i_ASN1_BIT_STRING(ASN1_BIT_STRING **a,
                                                    const unsigned char **pp,
                                                    long length);

OPENSSL_EXPORT int i2d_ASN1_BOOLEAN(int a, unsigned char **pp);
OPENSSL_EXPORT int d2i_ASN1_BOOLEAN(int *a, const unsigned char **pp,
                                    long length);

DECLARE_ASN1_FUNCTIONS(ASN1_INTEGER)
OPENSSL_EXPORT int i2c_ASN1_INTEGER(const ASN1_INTEGER *a, unsigned char **pp);
OPENSSL_EXPORT ASN1_INTEGER *c2i_ASN1_INTEGER(ASN1_INTEGER **a,
                                              const unsigned char **pp,
                                              long length);
OPENSSL_EXPORT ASN1_INTEGER *ASN1_INTEGER_dup(const ASN1_INTEGER *x);

DECLARE_ASN1_FUNCTIONS(ASN1_ENUMERATED)

DECLARE_ASN1_FUNCTIONS(ASN1_OCTET_STRING)
OPENSSL_EXPORT ASN1_OCTET_STRING *ASN1_OCTET_STRING_dup(
    const ASN1_OCTET_STRING *a);
OPENSSL_EXPORT int ASN1_OCTET_STRING_cmp(const ASN1_OCTET_STRING *a,
                                         const ASN1_OCTET_STRING *b);
OPENSSL_EXPORT int ASN1_OCTET_STRING_set(ASN1_OCTET_STRING *str,
                                         const unsigned char *data, int len);

DECLARE_ASN1_FUNCTIONS(ASN1_VISIBLESTRING)
DECLARE_ASN1_FUNCTIONS(ASN1_UNIVERSALSTRING)
DECLARE_ASN1_FUNCTIONS(ASN1_UTF8STRING)
DECLARE_ASN1_FUNCTIONS(ASN1_NULL)
DECLARE_ASN1_FUNCTIONS(ASN1_BMPSTRING)

DECLARE_ASN1_FUNCTIONS_name(ASN1_STRING, ASN1_PRINTABLE)

DECLARE_ASN1_FUNCTIONS_name(ASN1_STRING, DIRECTORYSTRING)
DECLARE_ASN1_FUNCTIONS_name(ASN1_STRING, DISPLAYTEXT)
DECLARE_ASN1_FUNCTIONS(ASN1_PRINTABLESTRING)
DECLARE_ASN1_FUNCTIONS(ASN1_T61STRING)
DECLARE_ASN1_FUNCTIONS(ASN1_IA5STRING)
DECLARE_ASN1_FUNCTIONS(ASN1_GENERALSTRING)
DECLARE_ASN1_FUNCTIONS(ASN1_UTCTIME)
DECLARE_ASN1_FUNCTIONS(ASN1_GENERALIZEDTIME)
DECLARE_ASN1_FUNCTIONS(ASN1_TIME)

OPENSSL_EXPORT int i2a_ASN1_INTEGER(BIO *bp, const ASN1_INTEGER *a);
OPENSSL_EXPORT int i2a_ASN1_ENUMERATED(BIO *bp, const ASN1_ENUMERATED *a);
OPENSSL_EXPORT int i2a_ASN1_OBJECT(BIO *bp, const ASN1_OBJECT *a);
OPENSSL_EXPORT int i2a_ASN1_STRING(BIO *bp, const ASN1_STRING *a, int type);
OPENSSL_EXPORT int i2t_ASN1_OBJECT(char *buf, int buf_len,
                                   const ASN1_OBJECT *a);

OPENSSL_EXPORT ASN1_OBJECT *ASN1_OBJECT_create(int nid,
                                               const unsigned char *data,
                                               int len, const char *sn,
                                               const char *ln);

// ASN1_PRINTABLE_type interprets |len| bytes from |s| as a Latin-1 string. It
// returns the first of |V_ASN1_PRINTABLESTRING|, |V_ASN1_IA5STRING|, or
// |V_ASN1_T61STRING| that can represent every character. If |len| is negative,
// |strlen(s)| is used instead.
OPENSSL_EXPORT int ASN1_PRINTABLE_type(const unsigned char *s, int len);

OPENSSL_EXPORT unsigned long ASN1_tag2bit(int tag);

// SPECIALS
OPENSSL_EXPORT int ASN1_get_object(const unsigned char **pp, long *plength,
                                   int *ptag, int *pclass, long omax);
OPENSSL_EXPORT void ASN1_put_object(unsigned char **pp, int constructed,
                                    int length, int tag, int xclass);
OPENSSL_EXPORT int ASN1_put_eoc(unsigned char **pp);
OPENSSL_EXPORT int ASN1_object_size(int constructed, int length, int tag);

OPENSSL_EXPORT void *ASN1_item_dup(const ASN1_ITEM *it, void *x);

OPENSSL_EXPORT void *ASN1_item_d2i_fp(const ASN1_ITEM *it, FILE *in, void *x);
OPENSSL_EXPORT int ASN1_item_i2d_fp(const ASN1_ITEM *it, FILE *out, void *x);

OPENSSL_EXPORT void *ASN1_item_d2i_bio(const ASN1_ITEM *it, BIO *in, void *x);
OPENSSL_EXPORT int ASN1_item_i2d_bio(const ASN1_ITEM *it, BIO *out, void *x);

// Used to load and write netscape format cert

OPENSSL_EXPORT void *ASN1_item_unpack(const ASN1_STRING *oct,
                                      const ASN1_ITEM *it);

OPENSSL_EXPORT ASN1_STRING *ASN1_item_pack(void *obj, const ASN1_ITEM *it,
                                           ASN1_OCTET_STRING **oct);

// ASN1_STRING_set_default_mask does nothing.
OPENSSL_EXPORT void ASN1_STRING_set_default_mask(unsigned long mask);

// ASN1_STRING_set_default_mask_asc returns one.
OPENSSL_EXPORT int ASN1_STRING_set_default_mask_asc(const char *p);

// ASN1_STRING_get_default_mask returns |B_ASN1_UTF8STRING|.
OPENSSL_EXPORT unsigned long ASN1_STRING_get_default_mask(void);

OPENSSL_EXPORT ASN1_STRING *ASN1_STRING_set_by_NID(ASN1_STRING **out,
                                                   const unsigned char *in,
                                                   int inlen, int inform,
                                                   int nid);
OPENSSL_EXPORT ASN1_STRING_TABLE *ASN1_STRING_TABLE_get(int nid);
OPENSSL_EXPORT int ASN1_STRING_TABLE_add(int, long, long, unsigned long,
                                         unsigned long);
OPENSSL_EXPORT void ASN1_STRING_TABLE_cleanup(void);

// ASN1 template functions

// Old API compatible functions
OPENSSL_EXPORT ASN1_VALUE *ASN1_item_new(const ASN1_ITEM *it);
OPENSSL_EXPORT void ASN1_item_free(ASN1_VALUE *val, const ASN1_ITEM *it);
OPENSSL_EXPORT ASN1_VALUE *ASN1_item_d2i(ASN1_VALUE **val,
                                         const unsigned char **in, long len,
                                         const ASN1_ITEM *it);
OPENSSL_EXPORT int ASN1_item_i2d(ASN1_VALUE *val, unsigned char **out,
                                 const ASN1_ITEM *it);

OPENSSL_EXPORT ASN1_TYPE *ASN1_generate_nconf(const char *str, CONF *nconf);
OPENSSL_EXPORT ASN1_TYPE *ASN1_generate_v3(const char *str, X509V3_CTX *cnf);


#ifdef __cplusplus
}

extern "C++" {

BSSL_NAMESPACE_BEGIN

BORINGSSL_MAKE_DELETER(ASN1_OBJECT, ASN1_OBJECT_free)
BORINGSSL_MAKE_DELETER(ASN1_STRING, ASN1_STRING_free)
BORINGSSL_MAKE_DELETER(ASN1_TYPE, ASN1_TYPE_free)

BSSL_NAMESPACE_END

}  // extern C++

#endif

#define ASN1_R_ASN1_LENGTH_MISMATCH 100
#define ASN1_R_AUX_ERROR 101
#define ASN1_R_BAD_GET_ASN1_OBJECT_CALL 102
#define ASN1_R_BAD_OBJECT_HEADER 103
#define ASN1_R_BMPSTRING_IS_WRONG_LENGTH 104
#define ASN1_R_BN_LIB 105
#define ASN1_R_BOOLEAN_IS_WRONG_LENGTH 106
#define ASN1_R_BUFFER_TOO_SMALL 107
#define ASN1_R_CONTEXT_NOT_INITIALISED 108
#define ASN1_R_DECODE_ERROR 109
#define ASN1_R_DEPTH_EXCEEDED 110
#define ASN1_R_DIGEST_AND_KEY_TYPE_NOT_SUPPORTED 111
#define ASN1_R_ENCODE_ERROR 112
#define ASN1_R_ERROR_GETTING_TIME 113
#define ASN1_R_EXPECTING_AN_ASN1_SEQUENCE 114
#define ASN1_R_EXPECTING_AN_INTEGER 115
#define ASN1_R_EXPECTING_AN_OBJECT 116
#define ASN1_R_EXPECTING_A_BOOLEAN 117
#define ASN1_R_EXPECTING_A_TIME 118
#define ASN1_R_EXPLICIT_LENGTH_MISMATCH 119
#define ASN1_R_EXPLICIT_TAG_NOT_CONSTRUCTED 120
#define ASN1_R_FIELD_MISSING 121
#define ASN1_R_FIRST_NUM_TOO_LARGE 122
#define ASN1_R_HEADER_TOO_LONG 123
#define ASN1_R_ILLEGAL_BITSTRING_FORMAT 124
#define ASN1_R_ILLEGAL_BOOLEAN 125
#define ASN1_R_ILLEGAL_CHARACTERS 126
#define ASN1_R_ILLEGAL_FORMAT 127
#define ASN1_R_ILLEGAL_HEX 128
#define ASN1_R_ILLEGAL_IMPLICIT_TAG 129
#define ASN1_R_ILLEGAL_INTEGER 130
#define ASN1_R_ILLEGAL_NESTED_TAGGING 131
#define ASN1_R_ILLEGAL_NULL 132
#define ASN1_R_ILLEGAL_NULL_VALUE 133
#define ASN1_R_ILLEGAL_OBJECT 134
#define ASN1_R_ILLEGAL_OPTIONAL_ANY 135
#define ASN1_R_ILLEGAL_OPTIONS_ON_ITEM_TEMPLATE 136
#define ASN1_R_ILLEGAL_TAGGED_ANY 137
#define ASN1_R_ILLEGAL_TIME_VALUE 138
#define ASN1_R_INTEGER_NOT_ASCII_FORMAT 139
#define ASN1_R_INTEGER_TOO_LARGE_FOR_LONG 140
#define ASN1_R_INVALID_BIT_STRING_BITS_LEFT 141
#define ASN1_R_INVALID_BMPSTRING 142
#define ASN1_R_INVALID_DIGIT 143
#define ASN1_R_INVALID_MODIFIER 144
#define ASN1_R_INVALID_NUMBER 145
#define ASN1_R_INVALID_OBJECT_ENCODING 146
#define ASN1_R_INVALID_SEPARATOR 147
#define ASN1_R_INVALID_TIME_FORMAT 148
#define ASN1_R_INVALID_UNIVERSALSTRING 149
#define ASN1_R_INVALID_UTF8STRING 150
#define ASN1_R_LIST_ERROR 151
#define ASN1_R_MISSING_ASN1_EOS 152
#define ASN1_R_MISSING_EOC 153
#define ASN1_R_MISSING_SECOND_NUMBER 154
#define ASN1_R_MISSING_VALUE 155
#define ASN1_R_MSTRING_NOT_UNIVERSAL 156
#define ASN1_R_MSTRING_WRONG_TAG 157
#define ASN1_R_NESTED_ASN1_ERROR 158
#define ASN1_R_NESTED_ASN1_STRING 159
#define ASN1_R_NON_HEX_CHARACTERS 160
#define ASN1_R_NOT_ASCII_FORMAT 161
#define ASN1_R_NOT_ENOUGH_DATA 162
#define ASN1_R_NO_MATCHING_CHOICE_TYPE 163
#define ASN1_R_NULL_IS_WRONG_LENGTH 164
#define ASN1_R_OBJECT_NOT_ASCII_FORMAT 165
#define ASN1_R_ODD_NUMBER_OF_CHARS 166
#define ASN1_R_SECOND_NUMBER_TOO_LARGE 167
#define ASN1_R_SEQUENCE_LENGTH_MISMATCH 168
#define ASN1_R_SEQUENCE_NOT_CONSTRUCTED 169
#define ASN1_R_SEQUENCE_OR_SET_NEEDS_CONFIG 170
#define ASN1_R_SHORT_LINE 171
#define ASN1_R_STREAMING_NOT_SUPPORTED 172
#define ASN1_R_STRING_TOO_LONG 173
#define ASN1_R_STRING_TOO_SHORT 174
#define ASN1_R_TAG_VALUE_TOO_HIGH 175
#define ASN1_R_TIME_NOT_ASCII_FORMAT 176
#define ASN1_R_TOO_LONG 177
#define ASN1_R_TYPE_NOT_CONSTRUCTED 178
#define ASN1_R_TYPE_NOT_PRIMITIVE 179
#define ASN1_R_UNEXPECTED_EOC 180
#define ASN1_R_UNIVERSALSTRING_IS_WRONG_LENGTH 181
#define ASN1_R_UNKNOWN_FORMAT 182
#define ASN1_R_UNKNOWN_MESSAGE_DIGEST_ALGORITHM 183
#define ASN1_R_UNKNOWN_SIGNATURE_ALGORITHM 184
#define ASN1_R_UNKNOWN_TAG 185
#define ASN1_R_UNSUPPORTED_ANY_DEFINED_BY_TYPE 186
#define ASN1_R_UNSUPPORTED_PUBLIC_KEY_TYPE 187
#define ASN1_R_UNSUPPORTED_TYPE 188
#define ASN1_R_WRONG_PUBLIC_KEY_TYPE 189
#define ASN1_R_WRONG_TAG 190
#define ASN1_R_WRONG_TYPE 191
#define ASN1_R_NESTED_TOO_DEEP 192
#define ASN1_R_BAD_TEMPLATE 193

#endif
