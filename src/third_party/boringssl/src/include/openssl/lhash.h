/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef OPENSSL_HEADER_LHASH_H
#define OPENSSL_HEADER_LHASH_H

#include <openssl/base.h>

#if defined(__cplusplus)
extern "C" {
#endif


// lhash is an internal library and not exported for use outside BoringSSL. This
// header is provided for compatibility with code that expects OpenSSL.


// These two macros are exported for compatibility with existing callers of
// |X509V3_EXT_conf_nid|. Do not use these symbols outside BoringSSL.
#define LHASH_OF(type) struct lhash_st_##type
#define DECLARE_LHASH_OF(type) LHASH_OF(type);


#if defined(__cplusplus)
}  // extern C
#endif

#endif  // OPENSSL_HEADER_LHASH_H
