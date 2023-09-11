// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PKI_CHECK_H_
#define PKI_CHECK_H_

// This header defines the CHECK, DCHECK, macros, inherited from chrome.

// This file is not used in chrome, so check here to make sure we are
// only compiling inside boringssl.
#if !defined(_BORINGSSL_LIBPKI_)
#error "_BORINGSSL_LIBPKI_ is not defined when compiling BoringSSL libpki"
#endif

#include <cassert>

// This is not used in this include file, but is here temporarily to avoid
// an intrusive change in chrome's files until we are fully extracted.
// TODO(bbe) move this include to the relevant .cc files once we are no
// longer using chrome as the canonical source.
#include <openssl/base.h>

// In chrome DCHECK is used like assert() but often erroneously. to be
// safe we make DCHECK the same as CHECK, and if we truly wish to have
// this be an assert, we convert to assert() as in the rest of boringssl.
// TODO(bbe) scan all DCHECK's in here once we are no longer using chrome
// as the canonical source and convert to CHECK unless certain they
// can be an assert().
#define DCHECK CHECK

// CHECK aborts if a condition is not true.
#define CHECK(A) \
  do {           \
    if (!(A))    \
      abort();   \
  } while (0);

#define DCHECK_EQ CHECK_EQ
#define DCHECK_NE CHECK_NE
#define DCHECK_LE CHECK_LE
#define DCHECK_LT CHECK_LT
#define DCHECK_GE CHECK_GE
#define DCHECK_GT CHECK_GT

#define CHECK_EQ(val1, val2) CHECK((val1) == (val2))
#define CHECK_NE(val1, val2) CHECK((val1) != (val2))
#define CHECK_LE(val1, val2) CHECK((val1) <= (val2))
#define CHECK_LT(val1, val2) CHECK((val1) < (val2))
#define CHECK_GE(val1, val2) CHECK((val1) >= (val2))
#define CHECK_GT(val1, val2) CHECK((val1) > (val2))

#endif  // PKI_CHECK_H_
