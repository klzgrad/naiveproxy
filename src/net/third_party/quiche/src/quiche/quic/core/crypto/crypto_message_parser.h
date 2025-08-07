// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_CRYPTO_MESSAGE_PARSER_H_
#define QUICHE_QUIC_CORE_CRYPTO_CRYPTO_MESSAGE_PARSER_H_

#include <string>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_types.h"

namespace quic {

class QUICHE_EXPORT CryptoMessageParser {
 public:
  virtual ~CryptoMessageParser() {}

  virtual QuicErrorCode error() const = 0;
  virtual const std::string& error_detail() const = 0;

  // Processes input data, which must be delivered in order. The input data
  // being processed was received at encryption level |level|. Returns
  // false if there was an error, and true otherwise.
  virtual bool ProcessInput(absl::string_view input, EncryptionLevel level) = 0;

  // Returns the number of bytes of buffered input data remaining to be
  // parsed.
  virtual size_t InputBytesRemaining() const = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_CRYPTO_MESSAGE_PARSER_H_
