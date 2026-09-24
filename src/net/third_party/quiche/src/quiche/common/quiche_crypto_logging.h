// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_QUICHE_CRYPTO_LOGGING_H_
#define QUICHE_COMMON_QUICHE_CRYPTO_LOGGING_H_

#include "absl/status/status.h"

namespace quiche {

// In debug builds only, log OpenSSL error stack. Then clear OpenSSL error
// stack.
void DLogOpenSslErrors();

// Clears OpenSSL error stack.
void ClearOpenSslErrors();

// Include OpenSSL error stack in Status msg so that callers could choose to
// only log it in debug builds if required.
absl::Status SslErrorAsStatus(
    absl::string_view msg, absl::StatusCode code = absl::StatusCode::kInternal);

}  // namespace quiche

#endif  // QUICHE_COMMON_QUICHE_CRYPTO_LOGGING_H_
