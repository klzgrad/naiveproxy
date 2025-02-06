/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/bio.h>

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

#include <openssl/err.h>
#include <openssl/mem.h>

int BIO_printf(BIO *bio, const char *format, ...) {
  va_list args;
  char buf[256], *out, out_malloced = 0;
  int out_len, ret;

  va_start(args, format);
  out_len = vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  if (out_len < 0) {
    return -1;
  }

  if ((size_t)out_len >= sizeof(buf)) {
    const size_t requested_len = (size_t)out_len;
    // The output was truncated. Note that vsnprintf's return value does not
    // include a trailing NUL, but the buffer must be sized for it.
    out = reinterpret_cast<char *>(OPENSSL_malloc(requested_len + 1));
    out_malloced = 1;
    if (out == NULL) {
      return -1;
    }
    va_start(args, format);
    out_len = vsnprintf(out, requested_len + 1, format, args);
    va_end(args);
    assert(out_len == (int)requested_len);
  } else {
    out = buf;
  }

  ret = BIO_write(bio, out, out_len);
  if (out_malloced) {
    OPENSSL_free(out);
  }

  return ret;
}
