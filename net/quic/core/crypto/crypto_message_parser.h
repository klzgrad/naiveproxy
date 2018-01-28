// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_CRYPTO_CRYPTO_MESSAGE_PARSER_H_
#define NET_QUIC_CORE_CRYPTO_CRYPTO_MESSAGE_PARSER_H_

#include "net/quic/core/quic_error_codes.h"
#include "net/quic/core/quic_types.h"
#include "net/quic/platform/api/quic_string_piece.h"

namespace net {

class QUIC_EXPORT_PRIVATE CryptoMessageParser {
 public:
  virtual ~CryptoMessageParser() {}

  virtual QuicErrorCode error() const = 0;
  virtual const std::string& error_detail() const = 0;

  // Processes input data, which must be delivered in order. Returns
  // false if there was an error, and true otherwise.
  virtual bool ProcessInput(QuicStringPiece input, Perspective perspective) = 0;

  // Returns the number of bytes of buffered input data remaining to be
  // parsed.
  virtual size_t InputBytesRemaining() const = 0;
};

}  // namespace net

#endif  // NET_QUIC_CORE_CRYPTO_CRYPTO_MESSAGE_PARSER_H_
