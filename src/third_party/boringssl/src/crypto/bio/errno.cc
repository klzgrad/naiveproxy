/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/bio.h>

#include <errno.h>

#include "internal.h"


int bio_errno_should_retry(int return_value) {
  if (return_value != -1) {
    return 0;
  }

  return
#ifdef EWOULDBLOCK
      errno == EWOULDBLOCK ||
#endif
#ifdef ENOTCONN
      errno == ENOTCONN ||
#endif
#ifdef EINTR
      errno == EINTR ||
#endif
#ifdef EAGAIN
      errno == EAGAIN ||
#endif
#ifdef EPROTO
      errno == EPROTO ||
#endif
#ifdef EINPROGRESS
      errno == EINPROGRESS ||
#endif
#ifdef EALREADY
      errno == EALREADY ||
#endif
      0;
}
