// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_QUARTC_QUARTC_FAKES_H_
#define NET_THIRD_PARTY_QUIC_QUARTC_QUARTC_FAKES_H_

#include "net/third_party/quic/core/quic_error_codes.h"
#include "net/third_party/quic/core/quic_types.h"
#include "net/third_party/quic/platform/api/quic_clock.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/quartc/quartc_endpoint.h"
#include "net/third_party/quic/quartc/quartc_session.h"
#include "net/third_party/quic/quartc/quartc_stream.h"

namespace quic {

class FakeQuartcEndpointDelegate : public QuartcEndpoint::Delegate {
 public:
  explicit FakeQuartcEndpointDelegate(QuartcSession::Delegate* session_delegate)
      : session_delegate_(session_delegate) {}

  void OnSessionCreated(QuartcSession* session) override {
    CHECK_EQ(session_, nullptr);
    CHECK_NE(session, nullptr);
    session_ = session;
    session_->SetDelegate(session_delegate_);
    session_->StartCryptoHandshake();
  }

  void OnConnectError(QuicErrorCode error,
                      const QuicString& error_details) override {
    LOG(FATAL) << "Unexpected error during QuartcEndpoint::Connect(); error="
               << error << ", error_details=" << error_details;
  }

  QuartcSession* session() { return session_; }

 private:
  QuartcSession::Delegate* session_delegate_;
  QuartcSession* session_ = nullptr;
};

class FakeQuartcSessionDelegate : public QuartcSession::Delegate {
 public:
  explicit FakeQuartcSessionDelegate(QuartcStream::Delegate* stream_delegate,
                                     const QuicClock* clock)
      : stream_delegate_(stream_delegate), clock_(clock) {}

  void OnConnectionWritable() override {
    LOG(INFO) << "Connection writable!";
    if (!writable_time_.IsInitialized()) {
      writable_time_ = clock_->Now();
    }
  }

  // Called when peers have established forward-secure encryption
  void OnCryptoHandshakeComplete() override {
    LOG(INFO) << "Crypto handshake complete!";
    crypto_handshake_time_ = clock_->Now();
  }

  // Called when connection closes locally, or remotely by peer.
  void OnConnectionClosed(QuicErrorCode error_code,
                          const QuicString& error_details,
                          ConnectionCloseSource source) override {
    connected_ = false;
  }

  // Called when an incoming QUIC stream is created.
  void OnIncomingStream(QuartcStream* quartc_stream) override {
    last_incoming_stream_ = quartc_stream;
    last_incoming_stream_->SetDelegate(stream_delegate_);
  }

  void OnMessageReceived(QuicStringPiece message) override {
    incoming_messages_.emplace_back(message);
  }

  void OnCongestionControlChange(QuicBandwidth bandwidth_estimate,
                                 QuicBandwidth pacing_rate,
                                 QuicTime::Delta latest_rtt) override {}

  QuartcStream* last_incoming_stream() { return last_incoming_stream_; }

  // Returns all received messages.
  const std::vector<QuicString>& incoming_messages() {
    return incoming_messages_;
  }

  bool connected() { return connected_; }
  QuicTime writable_time() const { return writable_time_; }
  QuicTime crypto_handshake_time() const { return crypto_handshake_time_; }

 private:
  QuartcStream* last_incoming_stream_;
  std::vector<QuicString> incoming_messages_;
  bool connected_ = true;
  QuartcStream::Delegate* stream_delegate_;
  QuicTime writable_time_ = QuicTime::Zero();
  QuicTime crypto_handshake_time_ = QuicTime::Zero();
  const QuicClock* clock_;
};

class FakeQuartcStreamDelegate : public QuartcStream::Delegate {
 public:
  size_t OnReceived(QuartcStream* stream,
                    iovec* iov,
                    size_t iov_length,
                    bool fin) override {
    size_t bytes_consumed = 0;
    for (size_t i = 0; i < iov_length; ++i) {
      received_data_[stream->id()] +=
          QuicString(static_cast<const char*>(iov[i].iov_base), iov[i].iov_len);
      bytes_consumed += iov[i].iov_len;
    }
    return bytes_consumed;
  }

  void OnClose(QuartcStream* stream) override {
    errors_[stream->id()] = stream->stream_error();
  }

  void OnBufferChanged(QuartcStream* stream) override {}

  bool has_data() { return !received_data_.empty(); }
  std::map<QuicStreamId, QuicString> data() { return received_data_; }

  QuicRstStreamErrorCode stream_error(QuicStreamId id) { return errors_[id]; }

 private:
  std::map<QuicStreamId, QuicString> received_data_;
  std::map<QuicStreamId, QuicRstStreamErrorCode> errors_;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_QUARTC_QUARTC_FAKES_H_
