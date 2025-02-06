/*
 * Copyright 1999-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef OPENSSL_HEADER_TYPE_CHECK_H
#define OPENSSL_HEADER_TYPE_CHECK_H

#include <openssl/base.h>

#if defined(__cplusplus)
extern "C" {
#endif


// CHECKED_CAST casts |p| from type |from| to type |to|.
//
// TODO(davidben): Although this macro is not public API and is unused in
// BoringSSL, wpa_supplicant uses it to define its own stacks. Remove this once
// wpa_supplicant has been fixed.
#define CHECKED_CAST(to, from, p) ((to) (1 ? (p) : (from)0))


#if defined(__cplusplus)
}  // extern C
#endif

#endif  // OPENSSL_HEADER_TYPE_CHECK_H
