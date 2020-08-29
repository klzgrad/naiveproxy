// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/quic_transport_error.h"

#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"

namespace net {

std::string QuicTransportErrorToString(const QuicTransportError& error) {
  std::string message =
      ExtendedErrorToString(error.net_error, error.quic_error);
  if (error.details == message)
    return message;
  return quiche::QuicheStrCat(message, " (", error.details, ")");
}

std::ostream& operator<<(std::ostream& os, const QuicTransportError& error) {
  os << QuicTransportErrorToString(error);
  return os;
}

}  // namespace net
