// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/quic_client_promised_info.h"

#include <string>
#include <utility>

#include "net/third_party/quiche/src/quic/core/http/spdy_server_push_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/spdy/core/spdy_protocol.h"

using spdy::SpdyHeaderBlock;

namespace quic {

QuicClientPromisedInfo::QuicClientPromisedInfo(
    QuicSpdyClientSessionBase* session,
    QuicStreamId id,
    std::string url)
    : session_(session),
      id_(id),
      url_(std::move(url)),
      client_request_delegate_(nullptr) {}

QuicClientPromisedInfo::~QuicClientPromisedInfo() {}

void QuicClientPromisedInfo::CleanupAlarm::OnAlarm() {
  QUIC_DVLOG(1) << "self GC alarm for stream " << promised_->id_;
  promised_->session()->OnPushStreamTimedOut(promised_->id_);
  promised_->Reset(QUIC_PUSH_STREAM_TIMED_OUT);
}

void QuicClientPromisedInfo::Init() {
  cleanup_alarm_.reset(session_->connection()->alarm_factory()->CreateAlarm(
      new QuicClientPromisedInfo::CleanupAlarm(this)));
  cleanup_alarm_->Set(
      session_->connection()->helper()->GetClock()->ApproximateNow() +
      QuicTime::Delta::FromSeconds(kPushPromiseTimeoutSecs));
}

bool QuicClientPromisedInfo::OnPromiseHeaders(const SpdyHeaderBlock& headers) {
  // RFC7540, Section 8.2, requests MUST be safe [RFC7231], Section
  // 4.2.1.  GET and HEAD are the methods that are safe and required.
  SpdyHeaderBlock::const_iterator it = headers.find(spdy::kHttp2MethodHeader);
  if (it == headers.end()) {
    QUIC_DVLOG(1) << "Promise for stream " << id_ << " has no method";
    Reset(QUIC_INVALID_PROMISE_METHOD);
    return false;
  }
  if (!(it->second == "GET" || it->second == "HEAD")) {
    QUIC_DVLOG(1) << "Promise for stream " << id_ << " has invalid method "
                  << it->second;
    Reset(QUIC_INVALID_PROMISE_METHOD);
    return false;
  }
  if (!SpdyServerPushUtils::PromisedUrlIsValid(headers)) {
    QUIC_DVLOG(1) << "Promise for stream " << id_ << " has invalid URL "
                  << url_;
    Reset(QUIC_INVALID_PROMISE_URL);
    return false;
  }
  if (!session_->IsAuthorized(
          SpdyServerPushUtils::GetPromisedHostNameFromHeaders(headers))) {
    Reset(QUIC_UNAUTHORIZED_PROMISE_URL);
    return false;
  }
  request_headers_ = headers.Clone();
  return true;
}

void QuicClientPromisedInfo::OnResponseHeaders(const SpdyHeaderBlock& headers) {
  response_headers_ = std::make_unique<SpdyHeaderBlock>(headers.Clone());
  if (client_request_delegate_) {
    // We already have a client request waiting.
    FinalValidation();
  }
}

void QuicClientPromisedInfo::Reset(QuicRstStreamErrorCode error_code) {
  QuicClientPushPromiseIndex::Delegate* delegate = client_request_delegate_;
  session_->ResetPromised(id_, error_code);
  session_->DeletePromised(this);
  if (delegate) {
    delegate->OnRendezvousResult(nullptr);
  }
}

QuicAsyncStatus QuicClientPromisedInfo::FinalValidation() {
  if (!client_request_delegate_->CheckVary(
          client_request_headers_, request_headers_, *response_headers_)) {
    Reset(QUIC_PROMISE_VARY_MISMATCH);
    return QUIC_FAILURE;
  }
  QuicSpdyStream* stream = session_->GetPromisedStream(id_);
  if (!stream) {
    // This shouldn't be possible, as |ClientRequest| guards against
    // closed stream for the synchronous case.  And in the
    // asynchronous case, a RST can only be caught by |OnAlarm()|.
    QUIC_BUG << "missing promised stream" << id_;
  }
  QuicClientPushPromiseIndex::Delegate* delegate = client_request_delegate_;
  session_->DeletePromised(this);
  // Stream can start draining now
  if (delegate) {
    delegate->OnRendezvousResult(stream);
  }
  return QUIC_SUCCESS;
}

QuicAsyncStatus QuicClientPromisedInfo::HandleClientRequest(
    const SpdyHeaderBlock& request_headers,
    QuicClientPushPromiseIndex::Delegate* delegate) {
  if (session_->IsClosedStream(id_)) {
    // There was a RST on the response stream.
    session_->DeletePromised(this);
    return QUIC_FAILURE;
  }

  if (is_validating()) {
    // The push promise has already been matched to another request though
    // pending for validation. Returns QUIC_FAILURE to the caller as it couldn't
    // match a new request any more. This will not affect the validation of the
    // other request.
    return QUIC_FAILURE;
  }

  client_request_delegate_ = delegate;
  client_request_headers_ = request_headers.Clone();
  if (response_headers_ == nullptr) {
    return QUIC_PENDING;
  }
  return FinalValidation();
}

void QuicClientPromisedInfo::Cancel() {
  // Don't fire OnRendezvousResult() for client initiated cancel.
  client_request_delegate_ = nullptr;
  Reset(QUIC_STREAM_CANCELLED);
}

}  // namespace quic
