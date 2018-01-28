// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_QUIC_CONNECTION_CLOSE_DELEGATE_INTERFACE_H_
#define NET_QUIC_CORE_QUIC_CONNECTION_CLOSE_DELEGATE_INTERFACE_H_

#include "net/quic/core/quic_error_codes.h"
#include "net/quic/core/quic_types.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {

// Pure virtual class to close connection on unrecoverable errors.
class QUIC_EXPORT_PRIVATE QuicConnectionCloseDelegateInterface {
 public:
  virtual ~QuicConnectionCloseDelegateInterface() {}

  // Called when an unrecoverable error is encountered.
  virtual void OnUnrecoverableError(QuicErrorCode error,
                                    const std::string& error_details,
                                    ConnectionCloseSource source) = 0;
};

}  // namespace net

#endif  // NET_QUIC_CORE_QUIC_CONNECTION_CLOSE_DELEGATE_INTERFACE_H_
