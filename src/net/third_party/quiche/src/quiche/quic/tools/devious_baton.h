// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TOOLS_DEVIOUS_BATON_H_
#define QUICHE_QUIC_TOOLS_DEVIOUS_BATON_H_

#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_circular_deque.h"
#include "quiche/web_transport/web_transport.h"

namespace quic {

// https://www.ietf.org/id/draft-frindell-webtrans-devious-baton-00.html#name-session-error-codes
inline constexpr webtransport::SessionErrorCode kDeviousBatonErrorDaYamn =
    0x01;  // Insufficient flow control credit
inline constexpr webtransport::SessionErrorCode kDeviousBatonErrorBruh =
    0x02;  // Parse error
inline constexpr webtransport::SessionErrorCode kDeviousBatonErrorSus =
    0x03;  // Unexpected message
inline constexpr webtransport::SessionErrorCode kDeviousBatonErrorBored =
    0x04;  // Timeout

using DeviousBatonValue = uint8_t;

// Implementation of the Devious Baton protocol as described in
// https://www.ietf.org/id/draft-frindell-webtrans-devious-baton-00.html
class DeviousBatonSessionVisitor : public webtransport::SessionVisitor {
 public:
  DeviousBatonSessionVisitor(webtransport::Session* session, bool is_server,
                             int initial_value, int count)
      : session_(session),
        is_server_(is_server),
        initial_value_(initial_value),
        count_(count) {}

  void OnSessionReady() override;
  void OnSessionClosed(webtransport::SessionErrorCode error_code,
                       const std::string& error_message) override;
  void OnIncomingBidirectionalStreamAvailable() override;
  void OnIncomingUnidirectionalStreamAvailable() override;
  void OnDatagramReceived(absl::string_view datagram) override;
  void OnCanCreateNewOutgoingBidirectionalStream() override;
  void OnCanCreateNewOutgoingUnidirectionalStream() override;

 private:
  using SendFunction = void (DeviousBatonSessionVisitor::*)(DeviousBatonValue);
  void SendUnidirectionalBaton(DeviousBatonValue value) {
    outgoing_unidi_batons_.push_back(value);
    OnCanCreateNewOutgoingUnidirectionalStream();
  }
  void SendBidirectionalBaton(DeviousBatonValue value) {
    outgoing_bidi_batons_.push_back(value);
    OnCanCreateNewOutgoingBidirectionalStream();
  }

  // Creates a callback that parses an incoming baton, parses it (while
  // potentially handling parse errors), and then passes it into the
  // `send_function`.
  quiche::SingleUseCallback<void(std::string)> CreateResponseCallback(
      SendFunction send_function);

  webtransport::Session* session_;
  bool is_server_;
  DeviousBatonValue initial_value_;
  int count_;
  quiche::QuicheCircularDeque<DeviousBatonValue> outgoing_unidi_batons_;
  quiche::QuicheCircularDeque<DeviousBatonValue> outgoing_bidi_batons_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_DEVIOUS_BATON_H_
