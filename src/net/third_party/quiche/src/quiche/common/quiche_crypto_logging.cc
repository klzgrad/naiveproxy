// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/common/quiche_crypto_logging.h"

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "openssl/err.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quiche {
void DLogOpenSslErrors() {
#ifdef NDEBUG
  // Clear OpenSSL error stack.
  ClearOpenSslErrors();
#else
  while (uint32_t error = ERR_get_error()) {
    char buf[120];
    ERR_error_string_n(error, buf, ABSL_ARRAYSIZE(buf));
    QUICHE_DLOG(ERROR) << "OpenSSL error: " << buf;
  }
#endif
}

void ClearOpenSslErrors() {
  while (ERR_get_error()) {
  }
}

absl::Status SslErrorAsStatus(absl::string_view msg, absl::StatusCode code) {
  std::string message;
  absl::StrAppend(&message, msg, "OpenSSL error: ");
  while (uint32_t error = ERR_get_error()) {
    char buf[120];
    ERR_error_string_n(error, buf, ABSL_ARRAYSIZE(buf));
    absl::StrAppend(&message, buf);
  }
  return absl::Status(code, message);
}

}  // namespace quiche
