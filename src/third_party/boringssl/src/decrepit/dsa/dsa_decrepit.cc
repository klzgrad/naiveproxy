/*
 * Copyright 2002-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/bn.h>
#include <openssl/dsa.h>


struct wrapped_callback {
  void (*callback)(int, int, void *);
  void *arg;
};

// callback_wrapper converts an “old” style generation callback to the newer
// |BN_GENCB| form.
static int callback_wrapper(int event, int n, BN_GENCB *gencb) {
  struct wrapped_callback *wrapped = (struct wrapped_callback *) gencb->arg;
  wrapped->callback(event, n, wrapped->arg);
  return 1;
}

DSA *DSA_generate_parameters(int bits, uint8_t *seed_in, int seed_len,
                             int *counter_ret, unsigned long *h_ret,
                             void (*callback)(int, int, void *), void *cb_arg) {
  if (bits < 0 || seed_len < 0) {
      return NULL;
  }

  DSA *ret = DSA_new();
  if (ret == NULL) {
      return NULL;
  }

  BN_GENCB gencb_storage;
  BN_GENCB *cb = NULL;

  struct wrapped_callback wrapped;

  if (callback != NULL) {
    wrapped.callback = callback;
    wrapped.arg = cb_arg;

    cb = &gencb_storage;
    BN_GENCB_set(cb, callback_wrapper, &wrapped);
  }

  if (!DSA_generate_parameters_ex(ret, bits, seed_in, seed_len, counter_ret,
                                  h_ret, cb)) {
    goto err;
  }

  return ret;

err:
  DSA_free(ret);
  return NULL;
}
