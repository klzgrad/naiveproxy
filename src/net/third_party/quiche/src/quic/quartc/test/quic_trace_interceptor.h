// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QUARTC_TEST_QUIC_TRACE_INTERCEPTOR_H_
#define QUICHE_QUIC_QUARTC_TEST_QUIC_TRACE_INTERCEPTOR_H_

#include <string>

#include "net/third_party/quiche/src/quic/core/quic_connection.h"
#include "net/third_party/quiche/src/quic/core/quic_error_codes.h"
#include "net/third_party/quiche/src/quic/core/quic_trace_visitor.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_endpoint.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_session.h"
#include "net/third_party/quiche/src/quic/quartc/test/bidi_test_runner.h"

namespace quic {
namespace test {

class QuicTraceInterceptor : public QuartcEndpointInterceptor {
 public:
  // Creates a trace visitor that records its output using the given identifier.
  // |identifier| is combined with the test name and timestamp to form a
  // filename for the trace.
  explicit QuicTraceInterceptor(QuicStringPiece identifier);
  ~QuicTraceInterceptor() override;

  // QuartcEndpointIntercept overrides.
  void OnSessionCreated(QuartcSession* session) override;
  void OnCryptoHandshakeComplete() override;
  void OnConnectionWritable() override;
  void OnIncomingStream(QuartcStream* stream) override;
  void OnCongestionControlChange(QuicBandwidth bandwidth_estimate,
                                 QuicBandwidth pacing_rate,
                                 QuicTime::Delta latest_rtt) override;
  void OnConnectionClosed(const QuicConnectionCloseFrame& frame,
                          ConnectionCloseSource source) override;
  void OnMessageReceived(QuicStringPiece message) override;
  void OnMessageSent(int64_t datagram_id) override;
  void OnMessageAcked(int64_t datagram_id, QuicTime receive_timestamp) override;
  void OnMessageLost(int64_t datagram_id) override;
  void SetDelegate(QuartcEndpoint::Delegate* delegate) override;

 private:
  const std::string identifier_;
  std::unique_ptr<QuicTraceVisitor> trace_visitor_;
  QuartcEndpoint::Delegate* delegate_;
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_QUARTC_TEST_QUIC_TRACE_INTERCEPTOR_H_
