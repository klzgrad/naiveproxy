// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/quic_transport_client.h"

#include "base/threading/thread_task_runner_handle.h"
#include "net/proxy_resolution/configured_proxy_resolution_service.h"
#include "net/proxy_resolution/proxy_resolution_request.h"
#include "net/quic/address_utils.h"
#include "net/quic/crypto/proof_verifier_chromium.h"
#include "net/quic/quic_chromium_alarm_factory.h"
#include "net/third_party/quiche/src/quic/core/quic_connection.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/url_request/url_request_context.h"

namespace net {

namespace {
std::set<std::string> HostsFromOrigins(std::set<HostPortPair> origins) {
  std::set<std::string> hosts;
  for (const auto& origin : origins) {
    hosts.insert(origin.host());
  }
  return hosts;
}
}  // namespace

QuicTransportClient::QuicTransportClient(
    const GURL& url,
    const url::Origin& origin,
    Visitor* visitor,
    const NetworkIsolationKey& isolation_key,
    URLRequestContext* context)
    : url_(url),
      origin_(origin),
      isolation_key_(isolation_key),
      context_(context),
      visitor_(visitor),
      // TODO(vasilvv): pass ClientSocketFactory through QuicContext.
      client_socket_factory_(ClientSocketFactory::GetDefaultFactory()),
      quic_context_(context->quic_context()),
      net_log_(NetLogWithSource::Make(context->net_log(),
                                      NetLogSourceType::QUIC_TRANSPORT_CLIENT)),
      task_runner_(base::ThreadTaskRunnerHandle::Get().get()),
      alarm_factory_(
          std::make_unique<QuicChromiumAlarmFactory>(task_runner_,
                                                     quic_context_->clock())),
      // TODO(vasilvv): proof verifier should have proper error reporting
      // (currently, all certificate verification errors result in "TLS
      // handshake error" even when more detailed message is available).  This
      // requires implementing ProofHandler::OnProofVerifyDetailsAvailable.
      crypto_config_(
          std::make_unique<ProofVerifierChromium>(
              context->cert_verifier(),
              context->ct_policy_enforcer(),
              context->transport_security_state(),
              context->cert_transparency_verifier(),
              std::set<std::string>(HostsFromOrigins(
                  quic_context_->params()->origins_to_force_quic_on))),
          /* session_cache */ nullptr) {}

QuicTransportClient::~QuicTransportClient() = default;

QuicTransportClient::Visitor::~Visitor() = default;

void QuicTransportClient::Connect() {
  if (state_ != NEW || next_connect_state_ != CONNECT_STATE_NONE) {
    NOTREACHED();
    return;
  }

  TransitionToState(CONNECTING);
  next_connect_state_ = CONNECT_STATE_INIT;
  DoLoop(OK);
}

quic::QuicTransportClientSession* QuicTransportClient::session() {
  if (session_ == nullptr || !session_->IsSessionReady())
    return nullptr;
  return session_.get();
}

void QuicTransportClient::DoLoop(int rv) {
  do {
    ConnectState connect_state = next_connect_state_;
    next_connect_state_ = CONNECT_STATE_NONE;
    switch (connect_state) {
      case CONNECT_STATE_INIT:
        DCHECK_EQ(rv, OK);
        rv = DoInit();
        break;
      case CONNECT_STATE_CHECK_PROXY:
        DCHECK_EQ(rv, OK);
        rv = DoCheckProxy();
        break;
      case CONNECT_STATE_CHECK_PROXY_COMPLETE:
        rv = DoCheckProxyComplete(rv);
        break;
      case CONNECT_STATE_RESOLVE_HOST:
        DCHECK_EQ(rv, OK);
        rv = DoResolveHost();
        break;
      case CONNECT_STATE_RESOLVE_HOST_COMPLETE:
        rv = DoResolveHostComplete(rv);
        break;
      case CONNECT_STATE_CONNECT:
        DCHECK_EQ(rv, OK);
        rv = DoConnect();
        break;
      case CONNECT_STATE_CONFIRM_CONNECTION:
        DCHECK_EQ(rv, OK);
        rv = DoConfirmConnection();
        break;
      default:
        NOTREACHED() << "Invalid state reached: " << connect_state;
        rv = ERR_FAILED;
        break;
    }
  } while (rv == OK && next_connect_state_ != CONNECT_STATE_NONE);

  if (rv == OK || rv == ERR_IO_PENDING)
    return;
  if (error_.net_error == OK)
    error_.net_error = rv;
  TransitionToState(FAILED);
}

int QuicTransportClient::DoInit() {
  if (!url_.is_valid())
    return ERR_INVALID_URL;
  if (url_.scheme_piece() != url::kQuicTransportScheme)
    return ERR_DISALLOWED_URL_SCHEME;

  // TODO(vasilvv): check if QUIC is disabled by policy.

  for (quic::ParsedQuicVersion& version :
       quic_context_->params()->supported_versions) {
    // QuicTransport requires TLS-style ALPN.
    if (version.handshake_protocol != quic::PROTOCOL_TLS1_3)
      continue;
    if (!quic::VersionSupportsMessageFrames(version.transport_version))
      continue;
    supported_versions_.push_back(version);
  }
  if (supported_versions_.empty()) {
    DLOG(ERROR) << "Attempted using QuicTransport with no compatible QUIC "
                   "versions available";
    return ERR_NOT_IMPLEMENTED;
  }

  next_connect_state_ = CONNECT_STATE_CHECK_PROXY;
  return OK;
}

int QuicTransportClient::DoCheckProxy() {
  next_connect_state_ = CONNECT_STATE_CHECK_PROXY_COMPLETE;
  return context_->proxy_resolution_service()->ResolveProxy(
      url_, /* method */ "CONNECT", isolation_key_, &proxy_info_,
      base::BindOnce(&QuicTransportClient::DoLoop, base::Unretained(this)),
      &proxy_resolution_request_, net_log_);
}

int QuicTransportClient::DoCheckProxyComplete(int rv) {
  if (rv != OK)
    return rv;

  if (!proxy_info_.is_direct())
    return ERR_TUNNEL_CONNECTION_FAILED;

  next_connect_state_ = CONNECT_STATE_RESOLVE_HOST;
  return OK;
}

int QuicTransportClient::DoResolveHost() {
  next_connect_state_ = CONNECT_STATE_RESOLVE_HOST_COMPLETE;
  HostResolver::ResolveHostParameters parameters;
  resolve_host_request_ = context_->host_resolver()->CreateRequest(
      HostPortPair::FromURL(url_), isolation_key_, net_log_, base::nullopt);
  return resolve_host_request_->Start(
      base::BindOnce(&QuicTransportClient::DoLoop, base::Unretained(this)));
}

int QuicTransportClient::DoResolveHostComplete(int rv) {
  if (rv != OK)
    return rv;

  DCHECK(resolve_host_request_->GetAddressResults());
  next_connect_state_ = CONNECT_STATE_CONNECT;
  return OK;
}

int QuicTransportClient::DoConnect() {
  int rv = OK;

  // TODO(vasilvv): consider unifying parts of this code with QuicSocketFactory
  // (which currently has a lot of code specific to QuicChromiumClientSession).
  socket_ = client_socket_factory_->CreateDatagramClientSocket(
      DatagramSocket::DEFAULT_BIND, net_log_.net_log(), net_log_.source());
  if (quic_context_->params()->enable_socket_recv_optimization)
    socket_->EnableRecvOptimization();
  socket_->UseNonBlockingIO();

  IPEndPoint server_address =
      *resolve_host_request_->GetAddressResults()->begin();
  rv = socket_->Connect(server_address);
  if (rv != OK)
    return rv;

  rv = socket_->SetReceiveBufferSize(kQuicSocketReceiveBufferSize);
  if (rv != OK)
    return rv;

  rv = socket_->SetDoNotFragment();
  if (rv == ERR_NOT_IMPLEMENTED)
    rv = OK;
  if (rv != OK)
    return rv;

  rv = socket_->SetSendBufferSize(quic::kMaxOutgoingPacketSize * 20);
  if (rv != OK)
    return rv;

  quic::QuicConnectionId connection_id =
      quic::QuicUtils::CreateRandomConnectionId(
          quic_context_->random_generator());
  connection_ = std::make_unique<quic::QuicConnection>(
      connection_id, ToQuicSocketAddress(server_address),
      quic_context_->helper(), alarm_factory_.get(),
      new QuicChromiumPacketWriter(socket_.get(), task_runner_),
      /* owns_writer */ true, quic::Perspective::IS_CLIENT,
      supported_versions_);
  connection_->SetMaxPacketLength(quic_context_->params()->max_packet_length);

  session_ = std::make_unique<quic::QuicTransportClientSession>(
      connection_.get(), this, InitializeQuicConfig(*quic_context_->params()),
      supported_versions_, url_, &crypto_config_, origin_, this);

  packet_reader_ = std::make_unique<QuicChromiumPacketReader>(
      socket_.get(), quic_context_->clock(), this, kQuicYieldAfterPacketsRead,
      quic::QuicTime::Delta::FromMilliseconds(
          kQuicYieldAfterDurationMilliseconds),
      net_log_);

  session_->Initialize();
  packet_reader_->StartReading();
  session_->CryptoConnect();

  next_connect_state_ = CONNECT_STATE_CONFIRM_CONNECTION;
  return ERR_IO_PENDING;
}

int QuicTransportClient::DoConfirmConnection() {
  if (!connection_->connected() || !session_->IsSessionReady()) {
    return ERR_QUIC_PROTOCOL_ERROR;
  }

  TransitionToState(CONNECTED);
  return OK;
}

void QuicTransportClient::TransitionToState(State next_state) {
  const State last_state = state_;
  state_ = next_state;
  switch (next_state) {
    case CONNECTING:
      DCHECK_EQ(last_state, NEW);
      break;

    case CONNECTED:
      DCHECK_EQ(last_state, CONNECTING);
      visitor_->OnConnected();
      break;

    case CLOSED:
      DCHECK_EQ(last_state, CONNECTED);
      visitor_->OnClosed();
      break;

    case FAILED:
      // "[T]he user agent that runs untrusted clients MUST NOT provide any
      // detailed error information until the server has confirmed that it is a
      // WebTransport endpoint."
      // https://tools.ietf.org/html/draft-vvv-webtransport-overview-01#section-7
      if (session_ != nullptr) {
        error_.safe_to_report_details = session_->alpn_received();
      }

      DCHECK_NE(error_.net_error, OK);
      if (error_.details.empty()) {
        error_.details = ErrorToString(error_.net_error);
      }

      if (last_state == CONNECTING) {
        visitor_->OnConnectionFailed();
        break;
      }
      DCHECK_EQ(last_state, CONNECTED);
      visitor_->OnError();
      break;

    default:
      NOTREACHED() << "Invalid state reached: " << next_state;
      break;
  }
}

void QuicTransportClient::OnSessionReady() {
  DCHECK_EQ(next_connect_state_, CONNECT_STATE_CONFIRM_CONNECTION);
  DoLoop(OK);
}

void QuicTransportClient::OnIncomingBidirectionalStreamAvailable() {
  visitor_->OnIncomingBidirectionalStreamAvailable();
}

void QuicTransportClient::OnIncomingUnidirectionalStreamAvailable() {
  visitor_->OnIncomingUnidirectionalStreamAvailable();
}

void QuicTransportClient::OnDatagramReceived(
    quiche::QuicheStringPiece datagram) {
  visitor_->OnDatagramReceived(datagram);
}

void QuicTransportClient::OnCanCreateNewOutgoingBidirectionalStream() {
  visitor_->OnCanCreateNewOutgoingBidirectionalStream();
}

void QuicTransportClient::OnCanCreateNewOutgoingUnidirectionalStream() {
  visitor_->OnCanCreateNewOutgoingUnidirectionalStream();
}

void QuicTransportClient::OnReadError(int result,
                                      const DatagramClientSocket* socket) {
  error_.net_error = result;
  connection_->CloseConnection(quic::QUIC_PACKET_READ_ERROR,
                               ErrorToString(result),
                               quic::ConnectionCloseBehavior::SILENT_CLOSE);
}

bool QuicTransportClient::OnPacket(
    const quic::QuicReceivedPacket& packet,
    const quic::QuicSocketAddress& local_address,
    const quic::QuicSocketAddress& peer_address) {
  session_->ProcessUdpPacket(local_address, peer_address, packet);
  return connection_->connected();
}

int QuicTransportClient::HandleWriteError(
    int error_code,
    scoped_refptr<QuicChromiumPacketWriter::ReusableIOBuffer> /*last_packet*/) {
  return error_code;
}

void QuicTransportClient::OnWriteError(int error_code) {
  error_.net_error = error_code;
  connection_->OnWriteError(error_code);
}

void QuicTransportClient::OnWriteUnblocked() {
  connection_->OnCanWrite();
}

void QuicTransportClient::OnConnectionClosed(
    quic::QuicConnectionId /*server_connection_id*/,
    quic::QuicErrorCode error,
    const std::string& error_details,
    quic::ConnectionCloseSource /*source*/) {
  if (error == quic::QUIC_NO_ERROR) {
    TransitionToState(CLOSED);
    return;
  }

  if (error_.net_error == OK)
    error_.net_error = ERR_QUIC_PROTOCOL_ERROR;
  error_.quic_error = error;
  error_.details = error_details;

  if (state_ == CONNECTING) {
    DCHECK_EQ(next_connect_state_, CONNECT_STATE_CONFIRM_CONNECTION);
    DoLoop(OK);
    return;
  }

  TransitionToState(FAILED);
}

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
