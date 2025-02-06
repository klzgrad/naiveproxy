/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/ssl.h>

#if !defined(OPENSSL_WINDOWS) && !defined(OPENSSL_PNACL) && \
    !defined(OPENSSL_NO_FILESYSTEM)

#include <dirent.h>
#include <errno.h>
#include <string.h>

#include <openssl/err.h>
#include <openssl/mem.h>


int SSL_add_dir_cert_subjects_to_stack(STACK_OF(X509_NAME) *stack,
                                       const char *path) {
  DIR *dir = opendir(path);
  if (dir == NULL) {
    OPENSSL_PUT_ERROR(SSL, ERR_R_SYS_LIB);
    ERR_add_error_data(3, "opendir('", dir, "')");
    return 0;
  }

  int ret = 0;
  for (;;) {
    // |readdir| may fail with or without setting |errno|.
    errno = 0;
    struct dirent *dirent = readdir(dir);
    if (dirent == NULL) {
      if (errno) {
        OPENSSL_PUT_ERROR(SSL, ERR_R_SYS_LIB);
        ERR_add_error_data(3, "readdir('", path, "')");
      } else {
        ret = 1;
      }
      break;
    }

    char buf[1024];
    if (strlen(path) + strlen(dirent->d_name) + 2 > sizeof(buf)) {
      OPENSSL_PUT_ERROR(SSL, SSL_R_PATH_TOO_LONG);
      break;
    }

    int r = snprintf(buf, sizeof(buf), "%s/%s", path, dirent->d_name);
    if (r <= 0 ||
        r >= (int)sizeof(buf) ||
        !SSL_add_file_cert_subjects_to_stack(stack, buf)) {
      break;
    }
  }

  closedir(dir);
  return ret;
}

#endif  // !WINDOWS && !PNACL && !OPENSSL_NO_FILESYSTEM
