/* Copyright (c) 2017, Google Inc.
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

#include <openssl/rand.h>

#include <stdlib.h>

#include "../fipsmodule/rand/internal.h"


// g_buffering_enabled is true if fork-unsafe buffering has been enabled.
static int g_buffering_enabled = 0;
static CRYPTO_once_t g_buffering_enabled_once = CRYPTO_ONCE_INIT;
static int g_buffering_enabled_pending = 0;

static void g_buffer_enabled_init(void) {
  g_buffering_enabled = g_buffering_enabled_pending;
}

#if !defined(OPENSSL_WINDOWS)
void RAND_enable_fork_unsafe_buffering(int fd) {
  // We no longer support setting the file-descriptor with this function.
  if (fd != -1) {
    abort();
  }

  g_buffering_enabled_pending = 1;
  CRYPTO_once(&g_buffering_enabled_once, g_buffer_enabled_init);
  if (g_buffering_enabled != 1) {
    // RAND_bytes has been called before this function.
    abort();
  }
}
#endif

int rand_fork_unsafe_buffering_enabled(void) {
  CRYPTO_once(&g_buffering_enabled_once, g_buffer_enabled_init);
  return g_buffering_enabled;
}
