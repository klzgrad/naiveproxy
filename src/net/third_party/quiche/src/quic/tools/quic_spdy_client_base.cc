// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/tools/quic_spdy_client_base.h"

#include <utility>

#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/core/http/spdy_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_server_id.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_text_utils.h"

using spdy::SpdyHeaderBlock;

namespace quic {

void QuicSpdyClientBase::ClientQuicDataToResend::Resend() {
  client_->SendRequest(*headers_, body_, fin_);
  headers_ = nullptr;
}

QuicSpdyClientBase::QuicDataToResend::QuicDataToResend(
    std::unique_ptr<SpdyHeaderBlock> headers,
    quiche::QuicheStringPiece body,
    bool fin)
    : headers_(std::move(headers)), body_(body), fin_(fin) {}

QuicSpdyClientBase::QuicDataToResend::~QuicDataToResend() = default;

QuicSpdyClientBase::QuicSpdyClientBase(
    const QuicServerId& server_id,
    const ParsedQuicVersionVector& supported_versions,
    const QuicConfig& config,
    QuicConnectionHelperInterface* helper,
    QuicAlarmFactory* alarm_factory,
    std::unique_ptr<NetworkHelper> network_helper,
    std::unique_ptr<ProofVerifier> proof_verifier,
    std::unique_ptr<SessionCache> session_cache)
    : QuicClientBase(server_id,
                     supported_versions,
                     config,
                     helper,
                     alarm_factory,
                     std::move(network_helper),
                     std::move(proof_verifier),
                     std::move(session_cache)),
      store_response_(false),
      latest_response_code_(-1),
      max_allowed_push_id_(0),
      disable_qpack_dynamic_table_(false) {}

QuicSpdyClientBase::~QuicSpdyClientBase() {
  // We own the push promise index. We need to explicitly kill
  // the session before the push promise index goes out of scope.
  ResetSession();
}

QuicSpdyClientSession* QuicSpdyClientBase::client_session() {
  return static_cast<QuicSpdyClientSession*>(QuicClientBase::session());
}

void QuicSpdyClientBase::InitializeSession() {
  if (disable_qpack_dynamic_table_) {
    client_session()->set_qpack_maximum_dynamic_table_capacity(0);
    client_session()->set_qpack_maximum_blocked_streams(0);
  }
  client_session()->Initialize();
  client_session()->CryptoConnect();
  if (max_allowed_push_id_ > 0 &&
      VersionUsesHttp3(client_session()->transport_version())) {
    client_session()->SetMaxPushId(max_allowed_push_id_);
  }
}

void QuicSpdyClientBase::OnClose(QuicSpdyStream* stream) {
  DCHECK(stream != nullptr);
  QuicSpdyClientStream* client_stream =
      static_cast<QuicSpdyClientStream*>(stream);

  const SpdyHeaderBlock& response_headers = client_stream->response_headers();
  if (response_listener_ != nullptr) {
    response_listener_->OnCompleteResponse(stream->id(), response_headers,
                                           client_stream->data());
  }

  // Store response headers and body.
  if (store_response_) {
    auto status = response_headers.find(":status");
    if (status == response_headers.end()) {
      QUIC_LOG(ERROR) << "Missing :status response header";
    } else if (!quiche::QuicheTextUtils::StringToInt(status->second,
                                                     &latest_response_code_)) {
      QUIC_LOG(ERROR) << "Invalid :status response header: " << status->second;
    }
    latest_response_headers_ = response_headers.DebugString();
    preliminary_response_headers_ =
        client_stream->preliminary_headers().DebugString();
    latest_response_header_block_ = response_headers.Clone();
    latest_response_body_ = client_stream->data();
    latest_response_trailers_ =
        client_stream->received_trailers().DebugString();
  }
}

std::unique_ptr<QuicSession> QuicSpdyClientBase::CreateQuicClientSession(
    const quic::ParsedQuicVersionVector& supported_versions,
    QuicConnection* connection) {
  return std::make_unique<QuicSpdyClientSession>(
      *config(), supported_versions, connection, server_id(), crypto_config(),
      &push_promise_index_);
}

void QuicSpdyClientBase::SendRequest(const SpdyHeaderBlock& headers,
                                     quiche::QuicheStringPiece body,
                                     bool fin) {
  if (GetQuicFlag(FLAGS_quic_client_convert_http_header_name_to_lowercase)) {
    QUIC_CODE_COUNT(quic_client_convert_http_header_name_to_lowercase);
    SpdyHeaderBlock sanitized_headers;
    for (const auto& p : headers) {
      sanitized_headers[quiche::QuicheTextUtils::ToLower(p.first)] = p.second;
    }

    SendRequestInternal(std::move(sanitized_headers), body, fin);
  } else {
    SendRequestInternal(headers.Clone(), body, fin);
  }
}

void QuicSpdyClientBase::SendRequestInternal(SpdyHeaderBlock sanitized_headers,
                                             quiche::QuicheStringPiece body,
                                             bool fin) {
  QuicClientPushPromiseIndex::TryHandle* handle;
  QuicAsyncStatus rv =
      push_promise_index()->Try(sanitized_headers, this, &handle);
  if (rv == QUIC_SUCCESS)
    return;

  if (rv == QUIC_PENDING) {
    // May need to retry request if asynchronous rendezvous fails.
    AddPromiseDataToResend(sanitized_headers, body, fin);
    return;
  }

  QuicSpdyClientStream* stream = CreateClientStream();
  if (stream == nullptr) {
    QUIC_BUG << "stream creation failed!";
    return;
  }
  stream->SendRequest(std::move(sanitized_headers), body, fin);
}

void QuicSpdyClientBase::SendRequestAndWaitForResponse(
    const SpdyHeaderBlock& headers,
    quiche::QuicheStringPiece body,
    bool fin) {
  SendRequest(headers, body, fin);
  while (WaitForEvents()) {
  }
}

void QuicSpdyClientBase::SendRequestsAndWaitForResponse(
    const std::vector<std::string>& url_list) {
  for (size_t i = 0; i < url_list.size(); ++i) {
    SpdyHeaderBlock headers;
    if (!SpdyUtils::PopulateHeaderBlockFromUrl(url_list[i], &headers)) {
      QUIC_BUG << "Unable to create request";
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
    // Process MAX_STREAMS from peer.
    while (!client_session()->CanOpenNextOutgoingBidirectionalStream()) {
      network_helper()->RunEventLoop();
    }
  }
  auto* stream = static_cast<QuicSpdyClientStream*>(
      client_session()->CreateOutgoingBidirectionalStream());
  if (stream) {
    stream->SetPriority(
        spdy::SpdyStreamPrecedence(QuicStream::kDefaultPriority));
    stream->set_visitor(this);
  }
  return stream;
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

void QuicSpdyClientBase::MaybeAddQuicDataToResend(
    std::unique_ptr<QuicDataToResend> data_to_resend) {
  data_to_resend_on_connect_.push_back(std::move(data_to_resend));
}

void QuicSpdyClientBase::ClearDataToResend() {
  data_to_resend_on_connect_.clear();
}

void QuicSpdyClientBase::ResendSavedData() {
  // Calling Resend will re-enqueue the data, so swap out
  //  data_to_resend_on_connect_ before iterating.
  std::vector<std::unique_ptr<QuicDataToResend>> old_data;
  old_data.swap(data_to_resend_on_connect_);
  for (const auto& data : old_data) {
    data->Resend();
  }
}

void QuicSpdyClientBase::AddPromiseDataToResend(const SpdyHeaderBlock& headers,
                                                quiche::QuicheStringPiece body,
                                                bool fin) {
  std::unique_ptr<SpdyHeaderBlock> new_headers(
      new SpdyHeaderBlock(headers.Clone()));
  push_promise_data_to_resend_.reset(
      new ClientQuicDataToResend(std::move(new_headers), body, fin, this));
}

bool QuicSpdyClientBase::CheckVary(
    const SpdyHeaderBlock& /*client_request*/,
    const SpdyHeaderBlock& /*promise_request*/,
    const SpdyHeaderBlock& /*promise_response*/) {
  return true;
}

void QuicSpdyClientBase::OnRendezvousResult(QuicSpdyStream* stream) {
  std::unique_ptr<ClientQuicDataToResend> data_to_resend =
      std::move(push_promise_data_to_resend_);
  if (stream) {
    stream->set_visitor(this);
    stream->OnBodyAvailable();
  } else if (data_to_resend) {
    data_to_resend->Resend();
  }
}

int QuicSpdyClientBase::latest_response_code() const {
  QUIC_BUG_IF(!store_response_) << "Response not stored!";
  return latest_response_code_;
}

const std::string& QuicSpdyClientBase::latest_response_headers() const {
  QUIC_BUG_IF(!store_response_) << "Response not stored!";
  return latest_response_headers_;
}

const std::string& QuicSpdyClientBase::preliminary_response_headers() const {
  QUIC_BUG_IF(!store_response_) << "Response not stored!";
  return preliminary_response_headers_;
}

const SpdyHeaderBlock& QuicSpdyClientBase::latest_response_header_block()
    const {
  QUIC_BUG_IF(!store_response_) << "Response not stored!";
  return latest_response_header_block_;
}

const std::string& QuicSpdyClientBase::latest_response_body() const {
  QUIC_BUG_IF(!store_response_) << "Response not stored!";
  return latest_response_body_;
}

const std::string& QuicSpdyClientBase::latest_response_trailers() const {
  QUIC_BUG_IF(!store_response_) << "Response not stored!";
  return latest_response_trailers_;
}

bool QuicSpdyClientBase::HasActiveRequests() {
  return client_session()->HasActiveRequestStreams();
}

}  // namespace quic
