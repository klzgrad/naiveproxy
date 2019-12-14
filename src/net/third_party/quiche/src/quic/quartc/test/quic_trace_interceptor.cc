// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/quartc/test/quic_trace_interceptor.h"

#include <string>
#include <utility>

#include "net/third_party/quiche/src/quic/core/quic_error_codes.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test_output.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_endpoint.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_session.h"

namespace quic {
namespace test {

QuicTraceInterceptor::QuicTraceInterceptor(QuicStringPiece identifier)
    : identifier_(identifier.data(), identifier.size()), delegate_(nullptr) {}

QuicTraceInterceptor::~QuicTraceInterceptor() {
  if (trace_visitor_) {
    QuicRecordTestOutput(identifier_,
                         trace_visitor_->trace()->SerializeAsString());
  }
}

void QuicTraceInterceptor::OnSessionCreated(QuartcSession* session) {
  trace_visitor_ = std::make_unique<QuicTraceVisitor>(session->connection());
  session->connection()->set_debug_visitor(trace_visitor_.get());

  delegate_->OnSessionCreated(session);
}

void QuicTraceInterceptor::OnCryptoHandshakeComplete() {
  delegate_->OnCryptoHandshakeComplete();
}

void QuicTraceInterceptor::OnConnectionWritable() {
  delegate_->OnConnectionWritable();
}

void QuicTraceInterceptor::OnIncomingStream(QuartcStream* stream) {
  delegate_->OnIncomingStream(stream);
}

void QuicTraceInterceptor::OnCongestionControlChange(
    QuicBandwidth bandwidth_estimate,
    QuicBandwidth pacing_rate,
    QuicTime::Delta latest_rtt) {
  delegate_->OnCongestionControlChange(bandwidth_estimate, pacing_rate,
                                       latest_rtt);
}

void QuicTraceInterceptor::OnConnectionClosed(
    const QuicConnectionCloseFrame& frame,
    ConnectionCloseSource source) {
  delegate_->OnConnectionClosed(frame, source);
}

void QuicTraceInterceptor::OnMessageReceived(QuicStringPiece message) {
  delegate_->OnMessageReceived(message);
}

void QuicTraceInterceptor::OnMessageSent(int64_t datagram_id) {
  delegate_->OnMessageSent(datagram_id);
}

void QuicTraceInterceptor::OnMessageAcked(int64_t datagram_id,
                                          QuicTime receive_timestamp) {
  delegate_->OnMessageAcked(datagram_id, receive_timestamp);
}

void QuicTraceInterceptor::OnMessageLost(int64_t datagram_id) {
  delegate_->OnMessageLost(datagram_id);
}

void QuicTraceInterceptor::SetDelegate(QuartcEndpoint::Delegate* delegate) {
  DCHECK(delegate != nullptr);
  delegate_ = delegate;
}

}  // namespace test
}  // namespace quic
