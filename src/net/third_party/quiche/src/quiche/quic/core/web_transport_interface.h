// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This header contains interfaces that abstract away different backing
// protocols for WebTransport.

#ifndef QUICHE_QUIC_CORE_WEB_TRANSPORT_INTERFACE_H_
#define QUICHE_QUIC_CORE_WEB_TRANSPORT_INTERFACE_H_

#include "quiche/quic/core/quic_types.h"
#include "quiche/web_transport/web_transport.h"

namespace quic {

using WebTransportSessionError = webtransport::SessionErrorCode;
using WebTransportStreamError = webtransport::StreamErrorCode;

using WebTransportStreamVisitor = webtransport::StreamVisitor;
using WebTransportStream = webtransport::Stream;
using WebTransportVisitor = webtransport::SessionVisitor;
using WebTransportSession = webtransport::Session;

inline webtransport::DatagramStatus MessageStatusToWebTransportStatus(
    MessageStatus status) {
  switch (status) {
    case MESSAGE_STATUS_SUCCESS:
      return webtransport::DatagramStatus(
          webtransport::DatagramStatusCode::kSuccess, "");
    case MESSAGE_STATUS_BLOCKED:
      return webtransport::DatagramStatus(
          webtransport::DatagramStatusCode::kBlocked,
          "QUIC connection write-blocked");
    case MESSAGE_STATUS_TOO_LARGE:
      return webtransport::DatagramStatus(
          webtransport::DatagramStatusCode::kTooBig,
          "Datagram payload exceeded maximum allowed size");
    case MESSAGE_STATUS_ENCRYPTION_NOT_ESTABLISHED:
    case MESSAGE_STATUS_INTERNAL_ERROR:
    case MESSAGE_STATUS_UNSUPPORTED:
      return webtransport::DatagramStatus(
          webtransport::DatagramStatusCode::kInternalError,
          absl::StrCat("Internal error: ", MessageStatusToString(status)));
    default:
      return webtransport::DatagramStatus(
          webtransport::DatagramStatusCode::kInternalError,
          absl::StrCat("Unknown status: ", MessageStatusToString(status)));
  }
}

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_WEB_TRANSPORT_INTERFACE_H_
