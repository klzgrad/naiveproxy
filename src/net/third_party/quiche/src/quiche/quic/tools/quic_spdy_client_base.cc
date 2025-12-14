// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/tools/quic_spdy_client_base.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>


#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/quic_random.h"
#include "quiche/quic/core/http/spdy_utils.h"
#include "quiche/quic/core/quic_server_id.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/common/quiche_text_utils.h"

using quiche::HttpHeaderBlock;

namespace quic {

QuicSpdyClientBase::QuicSpdyClientBase(
    const QuicServerId& server_id,
    const ParsedQuicVersionVector& supported_versions, const QuicConfig& config,
    QuicConnectionHelperInterface* helper, QuicAlarmFactory* alarm_factory,
    std::unique_ptr<NetworkHelper> network_helper,
    std::unique_ptr<ProofVerifier> proof_verifier,
    std::unique_ptr<SessionCache> session_cache)
    : QuicClientBase(server_id, supported_versions, config, helper,
                     alarm_factory, std::move(network_helper),
                     std::move(proof_verifier), std::move(session_cache)),
      store_response_(false),
      latest_response_code_(-1) {}

QuicSpdyClientBase::~QuicSpdyClientBase() {
  ResetSession();
}

QuicSpdyClientSession* QuicSpdyClientBase::client_session() {
  return static_cast<QuicSpdyClientSession*>(QuicClientBase::session());
}

const QuicSpdyClientSession* QuicSpdyClientBase::client_session() const {
  return static_cast<const QuicSpdyClientSession*>(QuicClientBase::session());
}

void QuicSpdyClientBase::InitializeSession() {
  if (max_inbound_header_list_size_ > 0) {
    client_session()->set_max_inbound_header_list_size(
        max_inbound_header_list_size_);
  }
  client_session()->Initialize();
  client_session()->CryptoConnect();
}

void QuicSpdyClientBase::OnClose(QuicSpdyStream* stream) {
  QUICHE_DCHECK(stream != nullptr);
  QuicSpdyClientStream* client_stream =
      static_cast<QuicSpdyClientStream*>(stream);

  const HttpHeaderBlock& response_headers = client_stream->response_headers();
  if (response_listener_ != nullptr) {
    response_listener_->OnCompleteResponse(stream->id(), response_headers,
                                           client_stream->data());
  }

  // Store response headers and body.
  if (store_response_) {
    auto status = response_headers.find(":status");
    if (status == response_headers.end()) {
      QUIC_LOG(ERROR) << "Missing :status response header";
    } else if (!absl::SimpleAtoi(status->second, &latest_response_code_)) {
      QUIC_LOG(ERROR) << "Invalid :status response header: " << status->second;
    }
    latest_response_headers_ = response_headers.DebugString();
    for (const HttpHeaderBlock& headers :
         client_stream->preliminary_headers()) {
      absl::StrAppend(&preliminary_response_headers_, headers.DebugString());
    }
    latest_response_header_block_ = response_headers.Clone();
    latest_response_body_ = std::string(client_stream->data());
    latest_response_trailers_ =
        client_stream->received_trailers().DebugString();
    latest_ttfb_ = client_stream->time_to_response_headers_received();
    latest_ttlb_ = client_stream->time_to_response_complete();
  }
}

std::unique_ptr<QuicSession> QuicSpdyClientBase::CreateQuicClientSession(
    const quic::ParsedQuicVersionVector& supported_versions,
    QuicConnection* connection) {
  return std::make_unique<QuicSpdyClientSession>(
      *config(), supported_versions, connection, server_id(), crypto_config());
}

void QuicSpdyClientBase::SendRequest(const HttpHeaderBlock& headers,
                                     absl::string_view body, bool fin) {
  if (GetQuicFlag(quic_client_convert_http_header_name_to_lowercase)) {
    QUIC_CODE_COUNT(quic_client_convert_http_header_name_to_lowercase);
    HttpHeaderBlock sanitized_headers;
    for (const auto& p : headers) {
      sanitized_headers[quiche::QuicheTextUtils::ToLower(p.first)] = p.second;
    }

    SendRequestInternal(std::move(sanitized_headers), body, fin);
  } else {
    SendRequestInternal(headers.Clone(), body, fin);
  }
}

void QuicSpdyClientBase::SendRequestInternal(HttpHeaderBlock sanitized_headers,
                                             absl::string_view body, bool fin) {
  QuicSpdyClientStream* stream = CreateClientStream();
  if (stream == nullptr) {
    QUIC_BUG(quic_bug_10949_1) << "stream creation failed!";
    return;
  }
  stream->SendRequest(std::move(sanitized_headers), body, fin);
}

void QuicSpdyClientBase::SendRequestAndWaitForResponse(
    const HttpHeaderBlock& headers, absl::string_view body, bool fin) {
  SendRequest(headers, body, fin);
  while (WaitForEvents()) {
  }
}

void QuicSpdyClientBase::SendRequestsAndWaitForResponse(
    const std::vector<std::string>& url_list) {
  for (size_t i = 0; i < url_list.size(); ++i) {
    HttpHeaderBlock headers;
    if (!SpdyUtils::PopulateHeaderBlockFromUrl(url_list[i], &headers)) {
      QUIC_BUG(quic_bug_10949_2) << "Unable to create request";
      continue;
    }
    SendRequest(headers, "", true);
  }
  while (WaitForEvents()) {
  }
}

QuicSpdyClientStream* QuicSpdyClientBase::CreateClientStream() {
  if (!connected()) {
    return nullptr;
  }
  if (VersionHasIetfQuicFrames(client_session()->transport_version())) {
    // Process MAX_STREAMS from peer or wait for liveness testing succeeds.
    while (!client_session()->CanOpenNextOutgoingBidirectionalStream()) {
      network_helper()->RunEventLoop();
    }
  }
  auto* stream = static_cast<QuicSpdyClientStream*>(
      client_session()->CreateOutgoingBidirectionalStream());
  if (stream) {
    stream->set_visitor(this);
  }
  return stream;
}

bool QuicSpdyClientBase::goaway_received() const {
  return client_session() && client_session()->goaway_received();
}

std::optional<uint64_t> QuicSpdyClientBase::last_received_http3_goaway_id() {
  return client_session() ? client_session()->last_received_http3_goaway_id()
                          : std::nullopt;
}

bool QuicSpdyClientBase::EarlyDataAccepted() {
  return client_session()->EarlyDataAccepted();
}

bool QuicSpdyClientBase::ReceivedInchoateReject() {
  return client_session()->ReceivedInchoateReject();
}

int QuicSpdyClientBase::GetNumSentClientHellosFromSession() {
  return client_session()->GetNumSentClientHellos();
}

int QuicSpdyClientBase::GetNumReceivedServerConfigUpdatesFromSession() {
  return client_session()->GetNumReceivedServerConfigUpdates();
}

int QuicSpdyClientBase::latest_response_code() const {
  QUIC_BUG_IF(quic_bug_10949_3, !store_response_) << "Response not stored!";
  return latest_response_code_;
}

const std::string& QuicSpdyClientBase::latest_response_headers() const {
  QUIC_BUG_IF(quic_bug_10949_4, !store_response_) << "Response not stored!";
  return latest_response_headers_;
}

const std::string& QuicSpdyClientBase::preliminary_response_headers() const {
  QUIC_BUG_IF(quic_bug_10949_5, !store_response_) << "Response not stored!";
  return preliminary_response_headers_;
}

const HttpHeaderBlock& QuicSpdyClientBase::latest_response_header_block()
    const {
  QUIC_BUG_IF(quic_bug_10949_6, !store_response_) << "Response not stored!";
  return latest_response_header_block_;
}

const std::string& QuicSpdyClientBase::latest_response_body() const {
  QUIC_BUG_IF(quic_bug_10949_7, !store_response_) << "Response not stored!";
  return latest_response_body_;
}

const std::string& QuicSpdyClientBase::latest_response_trailers() const {
  QUIC_BUG_IF(quic_bug_10949_8, !store_response_) << "Response not stored!";
  return latest_response_trailers_;
}

bool QuicSpdyClientBase::HasActiveRequests() {
  return client_session()->HasActiveRequestStreams();
}

}  // namespace quic
