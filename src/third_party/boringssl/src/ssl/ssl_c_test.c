/* Copyright 2019 The BoringSSL Authors
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

#include <openssl/ssl.h>

int BORINGSSL_enum_c_type_test(void);

int BORINGSSL_enum_c_type_test(void) {
#if defined(__cplusplus)
#error "This is testing the behaviour of the C compiler."
#error "It's pointless to build it in C++ mode."
#endif

  // In C++, the enums in ssl.h are explicitly typed as ints to allow them to
  // be predeclared. This function confirms that the C compiler believes them
  // to be the same size as ints. They may differ in signedness, however.
  return sizeof(enum ssl_private_key_result_t) == sizeof(int);
}
