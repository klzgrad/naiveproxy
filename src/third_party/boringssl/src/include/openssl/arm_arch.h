/*
 * Copyright 2011-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef OPENSSL_HEADER_ARM_ARCH_H
#define OPENSSL_HEADER_ARM_ARCH_H

#include <openssl/target.h>

// arm_arch.h contains symbols used by ARM assembly, and the C code that calls
// it. It is included as a public header to simplify the build, but is not
// intended for external use.

#if defined(OPENSSL_ARM) || defined(OPENSSL_AARCH64)

// ARMV7_NEON is true when a NEON unit is present in the current CPU.
#define ARMV7_NEON (1 << 0)

// ARMV8_AES indicates support for hardware AES instructions.
#define ARMV8_AES (1 << 2)

// ARMV8_SHA1 indicates support for hardware SHA-1 instructions.
#define ARMV8_SHA1 (1 << 3)

// ARMV8_SHA256 indicates support for hardware SHA-256 instructions.
#define ARMV8_SHA256 (1 << 4)

// ARMV8_PMULL indicates support for carryless multiplication.
#define ARMV8_PMULL (1 << 5)

// ARMV8_SHA512 indicates support for hardware SHA-512 instructions.
#define ARMV8_SHA512 (1 << 6)

#endif  // ARM || AARCH64

#endif  // OPENSSL_HEADER_ARM_ARCH_H
