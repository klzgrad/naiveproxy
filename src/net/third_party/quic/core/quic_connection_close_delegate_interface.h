// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_QUIC_CONNECTION_CLOSE_DELEGATE_INTERFACE_H_
#define NET_THIRD_PARTY_QUIC_CORE_QUIC_CONNECTION_CLOSE_DELEGATE_INTERFACE_H_

#include "net/third_party/quic/core/quic_error_codes.h"
#include "net/third_party/quic/core/quic_types.h"
#include "net/third_party/quic/platform/api/quic_export.h"
#include "net/third_party/quic/platform/api/quic_string.h"

namespace quic {

// Pure virtual class to close connection on unrecoverable errors.
class QUIC_EXPORT_PRIVATE QuicConnectionCloseDelegateInterface {
 public:
  virtual ~QuicConnectionCloseDelegateInterface() {}

  // Called when an unrecoverable error is encountered.
  virtual void OnUnrecoverableError(QuicErrorCode error,
                                    const QuicString& error_details,
                                    ConnectionCloseSource source) = 0;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_QUIC_CONNECTION_CLOSE_DELEGATE_INTERFACE_H_
