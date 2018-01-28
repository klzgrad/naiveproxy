// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/chromium/quic_chromium_client_session.h"

#include <utility>

#include "base/callback_helpers.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/sparse_histogram.h"
#include "base/single_thread_task_runner.h"
#include "base/stl_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/trace_event/memory_usage_estimator.h"
#include "base/values.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/base/network_activity_monitor.h"
#include "net/http/transport_security_state.h"
#include "net/log/net_log_event_type.h"
#include "net/log/net_log_source_type.h"
#include "net/quic/chromium/crypto/proof_verifier_chromium.h"
#include "net/quic/chromium/quic_chromium_connection_helper.h"
#include "net/quic/chromium/quic_chromium_packet_writer.h"
#include "net/quic/chromium/quic_crypto_client_stream_factory.h"
#include "net/quic/chromium/quic_server_info.h"
#include "net/quic/chromium/quic_stream_factory.h"
#include "net/quic/core/quic_client_promised_info.h"
#include "net/quic/core/spdy_utils.h"
#include "net/quic/platform/api/quic_ptr_util.h"
#include "net/socket/datagram_client_socket.h"
#include "net/spdy/chromium/spdy_http_utils.h"
#include "net/spdy/chromium/spdy_log_util.h"
#include "net/spdy/chromium/spdy_session.h"
#include "net/ssl/channel_id_service.h"
#include "net/ssl/ssl_connection_status_flags.h"
#include "net/ssl/ssl_info.h"
#include "net/ssl/token_binding.h"
#include "third_party/boringssl/src/include/openssl/ssl.h"

namespace net {

namespace {

// IPv6 packets have an additional 20 bytes of overhead than IPv4 packets.
const size_t kAdditionalOverheadForIPv6 = 20;

// Maximum number of Readers that are created for any session due to
// connection migration. A new Reader is created every time this endpoint's
// IP address changes.
const size_t kMaxReadersPerQuicSession = 5;

// Size of the MRU cache of Token Binding signatures. Since the material being
// signed is constant and there aren't many keys being used to sign, a fairly
// small number was chosen, somewhat arbitrarily, and to match
// SSLClientSocketImpl.
const size_t kTokenBindingSignatureMapSize = 10;

// Time to wait (in seconds) when no networks are available and
// migrating sessions need to wait for a new network to connect.
const size_t kWaitTimeForNewNetworkSecs = 10;

// The maximum size of uncompressed QUIC headers that will be allowed.
const size_t kMaxUncompressedHeaderSize = 256 * 1024;

// Histograms for tracking down the crashes from http://crbug.com/354669
// Note: these values must be kept in sync with the corresponding values in:
// tools/metrics/histograms/histograms.xml
enum Location {
  DESTRUCTOR = 0,
  ADD_OBSERVER = 1,
  TRY_CREATE_STREAM = 2,
  CREATE_OUTGOING_RELIABLE_STREAM = 3,
  NOTIFY_FACTORY_OF_SESSION_CLOSED_LATER = 4,
  NOTIFY_FACTORY_OF_SESSION_CLOSED = 5,
  NUM_LOCATIONS = 6,
};

void RecordUnexpectedOpenStreams(Location location) {
  UMA_HISTOGRAM_ENUMERATION("Net.QuicSession.UnexpectedOpenStreams", location,
                            NUM_LOCATIONS);
}

void RecordUnexpectedObservers(Location location) {
  UMA_HISTOGRAM_ENUMERATION("Net.QuicSession.UnexpectedObservers", location,
                            NUM_LOCATIONS);
}

void RecordUnexpectedNotGoingAway(Location location) {
  UMA_HISTOGRAM_ENUMERATION("Net.QuicSession.UnexpectedNotGoingAway", location,
                            NUM_LOCATIONS);
}

// Histogram for recording the different reasons that a QUIC session is unable
// to complete the handshake.
enum HandshakeFailureReason {
  HANDSHAKE_FAILURE_UNKNOWN = 0,
  HANDSHAKE_FAILURE_BLACK_HOLE = 1,
  HANDSHAKE_FAILURE_PUBLIC_RESET = 2,
  NUM_HANDSHAKE_FAILURE_REASONS = 3,
};

void RecordHandshakeFailureReason(HandshakeFailureReason reason) {
  UMA_HISTOGRAM_ENUMERATION(
      "Net.QuicSession.ConnectionClose.HandshakeNotConfirmed.Reason", reason,
      NUM_HANDSHAKE_FAILURE_REASONS);
}

// Note: these values must be kept in sync with the corresponding values in:
// tools/metrics/histograms/histograms.xml
enum HandshakeState {
  STATE_STARTED = 0,
  STATE_ENCRYPTION_ESTABLISHED = 1,
  STATE_HANDSHAKE_CONFIRMED = 2,
  STATE_FAILED = 3,
  NUM_HANDSHAKE_STATES = 4
};

void RecordHandshakeState(HandshakeState state) {
  UMA_HISTOGRAM_ENUMERATION("Net.QuicHandshakeState", state,
                            NUM_HANDSHAKE_STATES);
}

std::unique_ptr<base::Value> NetLogQuicClientSessionCallback(
    const QuicServerId* server_id,
    int cert_verify_flags,
    bool require_confirmation,
    NetLogCaptureMode /* capture_mode */) {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  dict->SetString("host", server_id->host());
  dict->SetInteger("port", server_id->port());
  dict->SetBoolean("privacy_mode",
                   server_id->privacy_mode() == PRIVACY_MODE_ENABLED);
  dict->SetBoolean("require_confirmation", require_confirmation);
  dict->SetInteger("cert_verify_flags", cert_verify_flags);
  return std::move(dict);
}

std::unique_ptr<base::Value> NetLogQuicPushPromiseReceivedCallback(
    const SpdyHeaderBlock* headers,
    SpdyStreamId stream_id,
    SpdyStreamId promised_stream_id,
    NetLogCaptureMode capture_mode) {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  dict->Set("headers", ElideSpdyHeaderBlockForNetLog(*headers, capture_mode));
  dict->SetInteger("id", stream_id);
  dict->SetInteger("promised_stream_id", promised_stream_id);
  return std::move(dict);
}

class HpackEncoderDebugVisitor : public QuicHpackDebugVisitor {
  void OnUseEntry(QuicTime::Delta elapsed) override {
    UMA_HISTOGRAM_TIMES(
        "Net.QuicHpackEncoder.IndexedEntryAge",
        base::TimeDelta::FromMicroseconds(elapsed.ToMicroseconds()));
  }
};

class HpackDecoderDebugVisitor : public QuicHpackDebugVisitor {
  void OnUseEntry(QuicTime::Delta elapsed) override {
    UMA_HISTOGRAM_TIMES(
        "Net.QuicHpackDecoder.IndexedEntryAge",
        base::TimeDelta::FromMicroseconds(elapsed.ToMicroseconds()));
  }
};

class QuicServerPushHelper : public ServerPushDelegate::ServerPushHelper {
 public:
  explicit QuicServerPushHelper(
      base::WeakPtr<QuicChromiumClientSession> session,
      const GURL& url)
      : session_(session), request_url_(url) {}

  void Cancel() override {
    if (session_) {
      session_->CancelPush(request_url_);
    }
  }

  const GURL& GetURL() const override { return request_url_; }

 private:
  base::WeakPtr<QuicChromiumClientSession> session_;
  const GURL request_url_;
};

}  // namespace

QuicChromiumClientSession::Handle::Handle(
    const base::WeakPtr<QuicChromiumClientSession>& session)
    : MultiplexedSessionHandle(session),
      session_(session),
      net_log_(session_->net_log()),
      was_handshake_confirmed_(session->IsCryptoHandshakeConfirmed()),
      net_error_(OK),
      quic_error_(QUIC_NO_ERROR),
      port_migration_detected_(false),
      server_id_(session_->server_id()),
      quic_version_(session->connection()->transport_version()),
      push_handle_(nullptr),
      was_ever_used_(false) {
  DCHECK(session_);
  session_->AddHandle(this);
}

QuicChromiumClientSession::Handle::~Handle() {
  if (push_handle_) {
    auto* push_handle = push_handle_;
    push_handle_ = nullptr;
    push_handle->Cancel();
  }

  if (session_)
    session_->RemoveHandle(this);
}

void QuicChromiumClientSession::Handle::OnCryptoHandshakeConfirmed() {
  was_handshake_confirmed_ = true;
}

void QuicChromiumClientSession::Handle::OnSessionClosed(
    QuicTransportVersion quic_version,
    int net_error,
    QuicErrorCode quic_error,
    bool port_migration_detected,
    LoadTimingInfo::ConnectTiming connect_timing,
    bool was_ever_used) {
  session_ = nullptr;
  port_migration_detected_ = port_migration_detected;
  net_error_ = net_error;
  quic_error_ = quic_error;
  quic_version_ = quic_version;
  connect_timing_ = connect_timing;
  push_handle_ = nullptr;
  was_ever_used_ = was_ever_used;
}

bool QuicChromiumClientSession::Handle::IsConnected() const {
  return session_ != nullptr;
}

bool QuicChromiumClientSession::Handle::IsCryptoHandshakeConfirmed() const {
  return was_handshake_confirmed_;
}

const LoadTimingInfo::ConnectTiming&
QuicChromiumClientSession::Handle::GetConnectTiming() {
  if (!session_)
    return connect_timing_;

  return session_->GetConnectTiming();
}

Error QuicChromiumClientSession::Handle::GetTokenBindingSignature(
    crypto::ECPrivateKey* key,
    TokenBindingType tb_type,
    std::vector<uint8_t>* out) {
  if (!session_)
    return ERR_CONNECTION_CLOSED;

  return session_->GetTokenBindingSignature(key, tb_type, out);
}

void QuicChromiumClientSession::Handle::PopulateNetErrorDetails(
    NetErrorDetails* details) const {
  if (session_) {
    session_->PopulateNetErrorDetails(details);
  } else {
    details->quic_port_migration_detected = port_migration_detected_;
    details->quic_connection_error = quic_error_;
  }
}

QuicTransportVersion QuicChromiumClientSession::Handle::GetQuicVersion() const {
  if (!session_)
    return quic_version_;

  return session_->connection()->transport_version();
}

void QuicChromiumClientSession::Handle::ResetPromised(
    QuicStreamId id,
    QuicRstStreamErrorCode error_code) {
  if (session_)
    session_->ResetPromised(id, error_code);
}

std::unique_ptr<QuicConnection::ScopedPacketBundler>
QuicChromiumClientSession::Handle::CreatePacketBundler(
    QuicConnection::AckBundling bundling_mode) {
  if (!session_)
    return nullptr;

  return std::make_unique<QuicConnection::ScopedPacketBundler>(
      session_->connection(), bundling_mode);
}

bool QuicChromiumClientSession::Handle::SharesSameSession(
    const Handle& other) const {
  return session_.get() == other.session_.get();
}

int QuicChromiumClientSession::Handle::RendezvousWithPromised(
    const SpdyHeaderBlock& headers,
    const CompletionCallback& callback) {
  if (!session_)
    return ERR_CONNECTION_CLOSED;

  QuicAsyncStatus push_status =
      session_->push_promise_index()->Try(headers, this, &push_handle_);

  switch (push_status) {
    case QUIC_FAILURE:
      return ERR_FAILED;
    case QUIC_SUCCESS:
      return OK;
    case QUIC_PENDING:
      push_callback_ = callback;
      return ERR_IO_PENDING;
  }
  NOTREACHED();
  return ERR_UNEXPECTED;
}

int QuicChromiumClientSession::Handle::RequestStream(
    bool requires_confirmation,
    const CompletionCallback& callback) {
  DCHECK(!stream_request_);

  if (!session_)
    return ERR_CONNECTION_CLOSED;

  // std::make_unique does not work because the StreamRequest constructor
  // is private.
  stream_request_ = std::unique_ptr<StreamRequest>(
      new StreamRequest(this, requires_confirmation));
  return stream_request_->StartRequest(callback);
}

std::unique_ptr<QuicChromiumClientStream::Handle>
QuicChromiumClientSession::Handle::ReleaseStream() {
  DCHECK(stream_request_);

  auto handle = stream_request_->ReleaseStream();
  stream_request_.reset();
  return handle;
}

std::unique_ptr<QuicChromiumClientStream::Handle>
QuicChromiumClientSession::Handle::ReleasePromisedStream() {
  DCHECK(push_stream_);
  return std::move(push_stream_);
}

int QuicChromiumClientSession::Handle::WaitForHandshakeConfirmation(
    const CompletionCallback& callback) {
  if (!session_)
    return ERR_CONNECTION_CLOSED;

  return session_->WaitForHandshakeConfirmation(callback);
}

void QuicChromiumClientSession::Handle::CancelRequest(StreamRequest* request) {
  if (session_)
    session_->CancelRequest(request);
}

int QuicChromiumClientSession::Handle::TryCreateStream(StreamRequest* request) {
  if (!session_)
    return ERR_CONNECTION_CLOSED;

  return session_->TryCreateStream(request);
}

QuicClientPushPromiseIndex*
QuicChromiumClientSession::Handle::GetPushPromiseIndex() {
  if (!session_)
    return push_promise_index_;

  return session_->push_promise_index();
}

int QuicChromiumClientSession::Handle::GetPeerAddress(
    IPEndPoint* address) const {
  if (!session_)
    return ERR_CONNECTION_CLOSED;

  *address = session_->peer_address().impl().socket_address();
  return OK;
}

int QuicChromiumClientSession::Handle::GetSelfAddress(
    IPEndPoint* address) const {
  if (!session_)
    return ERR_CONNECTION_CLOSED;

  *address = session_->self_address().impl().socket_address();
  return OK;
}

bool QuicChromiumClientSession::Handle::WasEverUsed() const {
  if (!session_)
    return was_ever_used_;

  return session_->WasConnectionEverUsed();
}

bool QuicChromiumClientSession::Handle::CheckVary(
    const SpdyHeaderBlock& client_request,
    const SpdyHeaderBlock& promise_request,
    const SpdyHeaderBlock& promise_response) {
  HttpRequestInfo promise_request_info;
  ConvertHeaderBlockToHttpRequestHeaders(promise_request,
                                         &promise_request_info.extra_headers);
  HttpRequestInfo client_request_info;
  ConvertHeaderBlockToHttpRequestHeaders(client_request,
                                         &client_request_info.extra_headers);

  HttpResponseInfo promise_response_info;
  if (!SpdyHeadersToHttpResponse(promise_response, &promise_response_info)) {
    DLOG(WARNING) << "Invalid headers";
    return false;
  }

  HttpVaryData vary_data;
  if (!vary_data.Init(promise_request_info,
                      *promise_response_info.headers.get())) {
    // Promise didn't contain valid vary info, so URL match was sufficient.
    return true;
  }
  // Now compare the client request for matching.
  return vary_data.MatchesRequest(client_request_info,
                                  *promise_response_info.headers.get());
}

void QuicChromiumClientSession::Handle::OnRendezvousResult(
    QuicSpdyStream* stream) {
  DCHECK(!push_stream_);
  int rv = ERR_FAILED;
  if (stream) {
    rv = OK;
    push_stream_ =
        static_cast<QuicChromiumClientStream*>(stream)->CreateHandle();
  }

  if (push_callback_) {
    DCHECK(push_handle_);
    push_handle_ = nullptr;
    base::ResetAndReturn(&push_callback_).Run(rv);
  }
}

QuicChromiumClientSession::StreamRequest::StreamRequest(
    QuicChromiumClientSession::Handle* session,
    bool requires_confirmation)
    : session_(session),
      requires_confirmation_(requires_confirmation),
      stream_(nullptr),
      weak_factory_(this) {}

QuicChromiumClientSession::StreamRequest::~StreamRequest() {
  if (stream_)
    stream_->Reset(QUIC_STREAM_CANCELLED);

  if (session_)
    session_->CancelRequest(this);
}

int QuicChromiumClientSession::StreamRequest::StartRequest(
    const CompletionCallback& callback) {
  if (!session_->IsConnected())
    return ERR_CONNECTION_CLOSED;

  next_state_ = STATE_WAIT_FOR_CONFIRMATION;
  int rv = DoLoop(OK);
  if (rv == ERR_IO_PENDING)
    callback_ = callback;

  return rv;
}

std::unique_ptr<QuicChromiumClientStream::Handle>
QuicChromiumClientSession::StreamRequest::ReleaseStream() {
  DCHECK(stream_);
  return std::move(stream_);
}

void QuicChromiumClientSession::StreamRequest::OnRequestCompleteSuccess(
    std::unique_ptr<QuicChromiumClientStream::Handle> stream) {
  DCHECK_EQ(STATE_REQUEST_STREAM_COMPLETE, next_state_);

  stream_ = std::move(stream);
  // This method is called even when the request completes synchronously.
  if (callback_)
    DoCallback(OK);
}

void QuicChromiumClientSession::StreamRequest::OnRequestCompleteFailure(
    int rv) {
  DCHECK_EQ(STATE_REQUEST_STREAM_COMPLETE, next_state_);
  // This method is called even when the request completes synchronously.
  if (callback_) {
    // Avoid re-entrancy if the callback calls into the session.
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::Bind(&QuicChromiumClientSession::StreamRequest::DoCallback,
                   weak_factory_.GetWeakPtr(), rv));
  }
}

void QuicChromiumClientSession::StreamRequest::OnIOComplete(int rv) {
  rv = DoLoop(rv);

  if (rv != ERR_IO_PENDING && !callback_.is_null()) {
    DoCallback(rv);
  }
}

void QuicChromiumClientSession::StreamRequest::DoCallback(int rv) {
  CHECK_NE(rv, ERR_IO_PENDING);
  CHECK(!callback_.is_null());

  // The client callback can do anything, including destroying this class,
  // so any pending callback must be issued after everything else is done.
  base::ResetAndReturn(&callback_).Run(rv);
}

int QuicChromiumClientSession::StreamRequest::DoLoop(int rv) {
  do {
    State state = next_state_;
    next_state_ = STATE_NONE;
    switch (state) {
      case STATE_WAIT_FOR_CONFIRMATION:
        CHECK_EQ(OK, rv);
        rv = DoWaitForConfirmation();
        break;
      case STATE_WAIT_FOR_CONFIRMATION_COMPLETE:
        rv = DoWaitForConfirmationComplete(rv);
        break;
      case STATE_REQUEST_STREAM:
        CHECK_EQ(OK, rv);
        rv = DoRequestStream();
        break;
      case STATE_REQUEST_STREAM_COMPLETE:
        rv = DoRequestStreamComplete(rv);
        break;
      default:
        NOTREACHED() << "next_state_: " << next_state_;
        break;
    }
  } while (next_state_ != STATE_NONE && next_state_ && rv != ERR_IO_PENDING);

  return rv;
}

int QuicChromiumClientSession::StreamRequest::DoWaitForConfirmation() {
  next_state_ = STATE_WAIT_FOR_CONFIRMATION_COMPLETE;
  if (requires_confirmation_) {
    return session_->WaitForHandshakeConfirmation(
        base::Bind(&QuicChromiumClientSession::StreamRequest::OnIOComplete,
                   weak_factory_.GetWeakPtr()));
  }

  return OK;
}

int QuicChromiumClientSession::StreamRequest::DoWaitForConfirmationComplete(
    int rv) {
  DCHECK_NE(ERR_IO_PENDING, rv);
  if (rv < 0)
    return rv;

  next_state_ = STATE_REQUEST_STREAM;
  return OK;
}

int QuicChromiumClientSession::StreamRequest::DoRequestStream() {
  next_state_ = STATE_REQUEST_STREAM_COMPLETE;

  return session_->TryCreateStream(this);
}

int QuicChromiumClientSession::StreamRequest::DoRequestStreamComplete(int rv) {
  DCHECK(rv == OK || !stream_);

  return rv;
}

QuicChromiumClientSession::QuicChromiumClientSession(
    QuicConnection* connection,
    std::unique_ptr<DatagramClientSocket> socket,
    QuicStreamFactory* stream_factory,
    QuicCryptoClientStreamFactory* crypto_client_stream_factory,
    QuicClock* clock,
    TransportSecurityState* transport_security_state,
    std::unique_ptr<QuicServerInfo> server_info,
    const QuicServerId& server_id,
    bool require_confirmation,
    int yield_after_packets,
    QuicTime::Delta yield_after_duration,
    int cert_verify_flags,
    const QuicConfig& config,
    QuicCryptoClientConfig* crypto_config,
    const char* const connection_description,
    base::TimeTicks dns_resolution_start_time,
    base::TimeTicks dns_resolution_end_time,
    QuicClientPushPromiseIndex* push_promise_index,
    ServerPushDelegate* push_delegate,
    base::TaskRunner* task_runner,
    std::unique_ptr<SocketPerformanceWatcher> socket_performance_watcher,
    NetLog* net_log)
    : QuicSpdyClientSessionBase(connection, push_promise_index, config),
      server_id_(server_id),
      require_confirmation_(require_confirmation),
      stream_factory_(stream_factory),
      transport_security_state_(transport_security_state),
      server_info_(std::move(server_info)),
      pkp_bypassed_(false),
      num_total_streams_(0),
      task_runner_(task_runner),
      net_log_(NetLogWithSource::Make(net_log, NetLogSourceType::QUIC_SESSION)),
      logger_(new QuicConnectionLogger(this,
                                       connection_description,
                                       std::move(socket_performance_watcher),
                                       net_log_)),
      going_away_(false),
      port_migration_detected_(false),
      token_binding_signatures_(kTokenBindingSignatureMapSize),
      push_delegate_(push_delegate),
      streams_pushed_count_(0),
      streams_pushed_and_claimed_count_(0),
      bytes_pushed_count_(0),
      bytes_pushed_and_unclaimed_count_(0),
      migration_pending_(false),
      weak_factory_(this) {
  sockets_.push_back(std::move(socket));
  packet_readers_.push_back(std::make_unique<QuicChromiumPacketReader>(
      sockets_.back().get(), clock, this, yield_after_packets,
      yield_after_duration, net_log_));
  crypto_stream_.reset(
      crypto_client_stream_factory->CreateQuicCryptoClientStream(
          server_id, this,
          std::make_unique<ProofVerifyContextChromium>(cert_verify_flags,
                                                       net_log_),
          crypto_config));
  connection->set_debug_visitor(logger_.get());
  connection->set_creator_debug_delegate(logger_.get());
  net_log_.BeginEvent(NetLogEventType::QUIC_SESSION,
                      base::Bind(NetLogQuicClientSessionCallback, &server_id,
                                 cert_verify_flags, require_confirmation_));
  IPEndPoint address;
  if (socket && socket->GetLocalAddress(&address) == OK &&
      address.GetFamily() == ADDRESS_FAMILY_IPV6) {
    connection->SetMaxPacketLength(connection->max_packet_length() -
                                   kAdditionalOverheadForIPv6);
  }
  connect_timing_.dns_start = dns_resolution_start_time;
  connect_timing_.dns_end = dns_resolution_end_time;
}

QuicChromiumClientSession::~QuicChromiumClientSession() {
  DCHECK(callback_.is_null());

  net_log_.EndEvent(NetLogEventType::QUIC_SESSION);
  DCHECK(waiting_for_confirmation_callbacks_.empty());
  if (!dynamic_streams().empty())
    RecordUnexpectedOpenStreams(DESTRUCTOR);
  if (!handles_.empty())
    RecordUnexpectedObservers(DESTRUCTOR);
  if (!going_away_)
    RecordUnexpectedNotGoingAway(DESTRUCTOR);

  while (!dynamic_streams().empty() || !handles_.empty() ||
         !stream_requests_.empty()) {
    // The session must be closed before it is destroyed.
    DCHECK(dynamic_streams().empty());
    CloseAllStreams(ERR_UNEXPECTED);
    DCHECK(handles_.empty());
    CloseAllHandles(ERR_UNEXPECTED);
    CancelAllRequests(ERR_UNEXPECTED);

    connection()->set_debug_visitor(nullptr);
  }

  if (connection()->connected()) {
    // Ensure that the connection is closed by the time the session is
    // destroyed.
    connection()->CloseConnection(QUIC_INTERNAL_ERROR, "session torn down",
                                  ConnectionCloseBehavior::SILENT_CLOSE);
  }

  if (IsEncryptionEstablished())
    RecordHandshakeState(STATE_ENCRYPTION_ESTABLISHED);
  if (IsCryptoHandshakeConfirmed())
    RecordHandshakeState(STATE_HANDSHAKE_CONFIRMED);
  else
    RecordHandshakeState(STATE_FAILED);

  UMA_HISTOGRAM_COUNTS_1M("Net.QuicSession.NumTotalStreams",
                          num_total_streams_);
  UMA_HISTOGRAM_COUNTS_1M("Net.QuicNumSentClientHellos",
                          crypto_stream_->num_sent_client_hellos());
  UMA_HISTOGRAM_COUNTS_1M("Net.QuicSession.Pushed", streams_pushed_count_);
  UMA_HISTOGRAM_COUNTS_1M("Net.QuicSession.PushedAndClaimed",
                          streams_pushed_and_claimed_count_);
  UMA_HISTOGRAM_COUNTS_1M("Net.QuicSession.PushedBytes", bytes_pushed_count_);
  DCHECK_LE(bytes_pushed_and_unclaimed_count_, bytes_pushed_count_);
  UMA_HISTOGRAM_COUNTS_1M("Net.QuicSession.PushedAndUnclaimedBytes",
                          bytes_pushed_and_unclaimed_count_);

  if (!IsCryptoHandshakeConfirmed())
    return;

  // Sending one client_hello means we had zero handshake-round-trips.
  int round_trip_handshakes = crypto_stream_->num_sent_client_hellos() - 1;

  // Don't bother with these histogram during tests, which mock out
  // num_sent_client_hellos().
  if (round_trip_handshakes < 0 || !stream_factory_)
    return;

  SSLInfo ssl_info;
  // QUIC supports only secure urls.
  if (GetSSLInfo(&ssl_info) && ssl_info.cert.get()) {
    UMA_HISTOGRAM_CUSTOM_COUNTS("Net.QuicSession.ConnectRandomPortForHTTPS",
                                round_trip_handshakes, 1, 3, 4);
    if (require_confirmation_) {
      UMA_HISTOGRAM_CUSTOM_COUNTS(
          "Net.QuicSession.ConnectRandomPortRequiringConfirmationForHTTPS",
          round_trip_handshakes, 1, 3, 4);
    }
  }

  const QuicConnectionStats stats = connection()->GetStats();

  // The MTU used by QUIC is limited to a fairly small set of predefined values
  // (initial values and MTU discovery values), but does not fare well when
  // bucketed.  Because of that, a sparse histogram is used here.
  UMA_HISTOGRAM_SPARSE_SLOWLY("Net.QuicSession.ClientSideMtu",
                              connection()->max_packet_length());
  UMA_HISTOGRAM_SPARSE_SLOWLY("Net.QuicSession.ServerSideMtu",
                              stats.max_received_packet_size);

  UMA_HISTOGRAM_COUNTS_1M("Net.QuicSession.MtuProbesSent",
                          connection()->mtu_probe_count());

  if (stats.packets_sent >= 100) {
    // Used to monitor for regressions that effect large uploads.
    UMA_HISTOGRAM_COUNTS_1000(
        "Net.QuicSession.PacketRetransmitsPerMille",
        1000 * stats.packets_retransmitted / stats.packets_sent);
  }

  if (stats.max_sequence_reordering == 0)
    return;
  const base::HistogramBase::Sample kMaxReordering = 100;
  base::HistogramBase::Sample reordering = kMaxReordering;
  if (stats.min_rtt_us > 0) {
    reordering = static_cast<base::HistogramBase::Sample>(
        100 * stats.max_time_reordering_us / stats.min_rtt_us);
  }
  UMA_HISTOGRAM_CUSTOM_COUNTS("Net.QuicSession.MaxReorderingTime", reordering,
                              1, kMaxReordering, 50);
  if (stats.min_rtt_us > 100 * 1000) {
    UMA_HISTOGRAM_CUSTOM_COUNTS("Net.QuicSession.MaxReorderingTimeLongRtt",
                                reordering, 1, kMaxReordering, 50);
  }
  UMA_HISTOGRAM_COUNTS_1M(
      "Net.QuicSession.MaxReordering",
      static_cast<base::HistogramBase::Sample>(stats.max_sequence_reordering));
}

void QuicChromiumClientSession::Initialize() {
  QuicSpdyClientSessionBase::Initialize();
  SetHpackEncoderDebugVisitor(std::make_unique<HpackEncoderDebugVisitor>());
  SetHpackDecoderDebugVisitor(std::make_unique<HpackDecoderDebugVisitor>());
  set_max_uncompressed_header_bytes(kMaxUncompressedHeaderSize);
}

void QuicChromiumClientSession::OnHeadersHeadOfLineBlocking(
    QuicTime::Delta delta) {
  UMA_HISTOGRAM_TIMES(
      "Net.QuicSession.HeadersHOLBlockedTime",
      base::TimeDelta::FromMicroseconds(delta.ToMicroseconds()));
}

void QuicChromiumClientSession::OnStreamFrame(const QuicStreamFrame& frame) {
  // Record total number of stream frames.
  UMA_HISTOGRAM_COUNTS_1M("Net.QuicNumStreamFramesInPacket", 1);

  // Record number of frames per stream in packet.
  UMA_HISTOGRAM_COUNTS_1M("Net.QuicNumStreamFramesPerStreamInPacket", 1);

  return QuicSpdySession::OnStreamFrame(frame);
}

void QuicChromiumClientSession::AddHandle(Handle* handle) {
  if (going_away_) {
    RecordUnexpectedObservers(ADD_OBSERVER);
    handle->OnSessionClosed(connection()->transport_version(), ERR_UNEXPECTED,
                            error(), port_migration_detected_,
                            GetConnectTiming(), WasConnectionEverUsed());
    return;
  }

  DCHECK(!base::ContainsKey(handles_, handle));
  handles_.insert(handle);
}

void QuicChromiumClientSession::RemoveHandle(Handle* handle) {
  DCHECK(base::ContainsKey(handles_, handle));
  handles_.erase(handle);
}

int QuicChromiumClientSession::WaitForHandshakeConfirmation(
    const CompletionCallback& callback) {
  if (!connection()->connected())
    return ERR_CONNECTION_CLOSED;

  if (IsCryptoHandshakeConfirmed())
    return OK;

  waiting_for_confirmation_callbacks_.push_back(callback);
  return ERR_IO_PENDING;
}

int QuicChromiumClientSession::TryCreateStream(StreamRequest* request) {
  if (stream_factory_ && stream_factory_->IsQuicBroken(this)) {
    DVLOG(1) << "QUIC broken.";
    return ERR_QUIC_PROTOCOL_ERROR;
  }

  if (goaway_received()) {
    DVLOG(1) << "Going away.";
    return ERR_CONNECTION_CLOSED;
  }

  if (!connection()->connected()) {
    DVLOG(1) << "Already closed.";
    return ERR_CONNECTION_CLOSED;
  }

  if (going_away_) {
    RecordUnexpectedOpenStreams(TRY_CREATE_STREAM);
    return ERR_CONNECTION_CLOSED;
  }

  if (GetNumOpenOutgoingStreams() < max_open_outgoing_streams()) {
    request->stream_ = CreateOutgoingReliableStreamImpl()->CreateHandle();
    return OK;
  }

  request->pending_start_time_ = base::TimeTicks::Now();
  stream_requests_.push_back(request);
  UMA_HISTOGRAM_COUNTS_1000("Net.QuicSession.NumPendingStreamRequests",
                            stream_requests_.size());
  return ERR_IO_PENDING;
}

void QuicChromiumClientSession::CancelRequest(StreamRequest* request) {
  // Remove |request| from the queue while preserving the order of the
  // other elements.
  StreamRequestQueue::iterator it =
      std::find(stream_requests_.begin(), stream_requests_.end(), request);
  if (it != stream_requests_.end()) {
    it = stream_requests_.erase(it);
  }
}

bool QuicChromiumClientSession::ShouldCreateOutgoingDynamicStream() {
  if (!crypto_stream_->encryption_established()) {
    DVLOG(1) << "Encryption not active so no outgoing stream created.";
    return false;
  }
  if (GetNumOpenOutgoingStreams() >= max_open_outgoing_streams()) {
    DVLOG(1) << "Failed to create a new outgoing stream. "
             << "Already " << GetNumOpenOutgoingStreams() << " open.";
    return false;
  }
  if (goaway_received()) {
    DVLOG(1) << "Failed to create a new outgoing stream. "
             << "Already received goaway.";
    return false;
  }
  if (going_away_) {
    RecordUnexpectedOpenStreams(CREATE_OUTGOING_RELIABLE_STREAM);
    return false;
  }
  return true;
}

bool QuicChromiumClientSession::WasConnectionEverUsed() {
  const QuicConnectionStats& stats = connection()->GetStats();
  return stats.bytes_sent > 0 || stats.bytes_received > 0;
}

QuicChromiumClientStream*
QuicChromiumClientSession::CreateOutgoingDynamicStream() {
  if (!ShouldCreateOutgoingDynamicStream()) {
    return nullptr;
  }
  QuicChromiumClientStream* stream = CreateOutgoingReliableStreamImpl();
  return stream;
}

QuicChromiumClientStream*
QuicChromiumClientSession::CreateOutgoingReliableStreamImpl() {
  DCHECK(connection()->connected());
  QuicChromiumClientStream* stream =
      new QuicChromiumClientStream(GetNextOutgoingStreamId(), this, net_log_);
  ActivateStream(base::WrapUnique(stream));
  ++num_total_streams_;
  UMA_HISTOGRAM_COUNTS_1M("Net.QuicSession.NumOpenStreams",
                          GetNumOpenOutgoingStreams());
  // The previous histogram puts 100 in a bucket betweeen 86-113 which does
  // not shed light on if chrome ever things it has more than 100 streams open.
  UMA_HISTOGRAM_BOOLEAN("Net.QuicSession.TooManyOpenStreams",
                        GetNumOpenOutgoingStreams() > 100);
  return stream;
}

QuicCryptoClientStream* QuicChromiumClientSession::GetMutableCryptoStream() {
  return crypto_stream_.get();
}

const QuicCryptoClientStream* QuicChromiumClientSession::GetCryptoStream()
    const {
  return crypto_stream_.get();
}

bool QuicChromiumClientSession::GetRemoteEndpoint(IPEndPoint* endpoint) {
  *endpoint = peer_address().impl().socket_address();
  return true;
}

// TODO(rtenneti): Add unittests for GetSSLInfo which exercise the various ways
// we learn about SSL info (sync vs async vs cached).
bool QuicChromiumClientSession::GetSSLInfo(SSLInfo* ssl_info) const {
  ssl_info->Reset();
  if (!cert_verify_result_) {
    return false;
  }

  ssl_info->cert_status = cert_verify_result_->cert_status;
  ssl_info->cert = cert_verify_result_->verified_cert;

  // Map QUIC AEADs to the corresponding TLS 1.3 cipher. OpenSSL's cipher suite
  // numbers begin with a stray 0x03, so mask them off.
  QuicTag aead = crypto_stream_->crypto_negotiated_params().aead;
  uint16_t cipher_suite;
  int security_bits;
  switch (aead) {
    case kAESG:
      cipher_suite = TLS1_CK_AES_128_GCM_SHA256 & 0xffff;
      security_bits = 128;
      break;
    case kCC20:
      cipher_suite = TLS1_CK_CHACHA20_POLY1305_SHA256 & 0xffff;
      security_bits = 256;
      break;
    default:
      NOTREACHED();
      return false;
  }
  int ssl_connection_status = 0;
  SSLConnectionStatusSetCipherSuite(cipher_suite, &ssl_connection_status);
  SSLConnectionStatusSetVersion(SSL_CONNECTION_VERSION_QUIC,
                                &ssl_connection_status);

  // Report the QUIC key exchange as the corresponding TLS curve.
  switch (crypto_stream_->crypto_negotiated_params().key_exchange) {
    case kP256:
      ssl_info->key_exchange_group = SSL_CURVE_SECP256R1;
      break;
    case kC255:
      ssl_info->key_exchange_group = SSL_CURVE_X25519;
      break;
    default:
      NOTREACHED();
      return false;
  }

  ssl_info->public_key_hashes = cert_verify_result_->public_key_hashes;
  ssl_info->is_issued_by_known_root =
      cert_verify_result_->is_issued_by_known_root;
  ssl_info->pkp_bypassed = pkp_bypassed_;

  ssl_info->connection_status = ssl_connection_status;
  ssl_info->client_cert_sent = false;
  ssl_info->channel_id_sent = crypto_stream_->WasChannelIDSent();
  ssl_info->security_bits = security_bits;
  ssl_info->handshake_type = SSLInfo::HANDSHAKE_FULL;
  ssl_info->pinning_failure_log = pinning_failure_log_;

  ssl_info->UpdateCertificateTransparencyInfo(*ct_verify_result_);

  if (crypto_stream_->crypto_negotiated_params().token_binding_key_param ==
      kTB10) {
    ssl_info->token_binding_negotiated = true;
    ssl_info->token_binding_key_param = TB_PARAM_ECDSAP256;
  }

  return true;
}

Error QuicChromiumClientSession::GetTokenBindingSignature(
    crypto::ECPrivateKey* key,
    TokenBindingType tb_type,
    std::vector<uint8_t>* out) {
  // The same key will be used across multiple requests to sign the same value,
  // so the signature is cached.
  std::string raw_public_key;
  if (!key->ExportRawPublicKey(&raw_public_key))
    return ERR_FAILED;
  TokenBindingSignatureMap::iterator it =
      token_binding_signatures_.Get(std::make_pair(tb_type, raw_public_key));
  if (it != token_binding_signatures_.end()) {
    *out = it->second;
    return OK;
  }

  std::string key_material;
  if (!crypto_stream_->ExportTokenBindingKeyingMaterial(&key_material))
    return ERR_FAILED;
  if (!CreateTokenBindingSignature(key_material, tb_type, key, out))
    return ERR_FAILED;
  token_binding_signatures_.Put(std::make_pair(tb_type, raw_public_key), *out);
  return OK;
}

int QuicChromiumClientSession::CryptoConnect(
    const CompletionCallback& callback) {
  connect_timing_.connect_start = base::TimeTicks::Now();
  RecordHandshakeState(STATE_STARTED);
  DCHECK(flow_controller());

  if (!crypto_stream_->CryptoConnect())
    return ERR_QUIC_HANDSHAKE_FAILED;

  if (IsCryptoHandshakeConfirmed()) {
    connect_timing_.connect_end = base::TimeTicks::Now();
    return OK;
  }

  // Unless we require handshake confirmation, activate the session if
  // we have established initial encryption.
  if (!require_confirmation_ && IsEncryptionEstablished())
    return OK;

  callback_ = callback;
  return ERR_IO_PENDING;
}

int QuicChromiumClientSession::GetNumSentClientHellos() const {
  return crypto_stream_->num_sent_client_hellos();
}

bool QuicChromiumClientSession::CanPool(const std::string& hostname,
                                        PrivacyMode privacy_mode) const {
  DCHECK(connection()->connected());
  if (privacy_mode != server_id_.privacy_mode()) {
    // Privacy mode must always match.
    return false;
  }
  SSLInfo ssl_info;
  if (!GetSSLInfo(&ssl_info) || !ssl_info.cert.get()) {
    NOTREACHED() << "QUIC should always have certificates.";
    return false;
  }

  return SpdySession::CanPool(transport_security_state_, ssl_info,
                              server_id_.host(), hostname);
}

bool QuicChromiumClientSession::ShouldCreateIncomingDynamicStream(
    QuicStreamId id) {
  if (!connection()->connected()) {
    LOG(DFATAL) << "ShouldCreateIncomingDynamicStream called when disconnected";
    return false;
  }
  if (goaway_received()) {
    DVLOG(1) << "Cannot create a new outgoing stream. "
             << "Already received goaway.";
    return false;
  }
  if (going_away_) {
    return false;
  }
  if (id % 2 != 0) {
    LOG(WARNING) << "Received invalid push stream id " << id;
    connection()->CloseConnection(
        QUIC_INVALID_STREAM_ID, "Server created odd numbered stream",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }
  return true;
}

QuicChromiumClientStream*
QuicChromiumClientSession::CreateIncomingDynamicStream(QuicStreamId id) {
  if (!ShouldCreateIncomingDynamicStream(id)) {
    return nullptr;
  }
  return CreateIncomingReliableStreamImpl(id);
}

QuicChromiumClientStream*
QuicChromiumClientSession::CreateIncomingReliableStreamImpl(QuicStreamId id) {
  DCHECK(connection()->connected());
  QuicChromiumClientStream* stream =
      new QuicChromiumClientStream(id, this, net_log_);
  stream->CloseWriteSide();
  ActivateStream(base::WrapUnique(stream));
  ++num_total_streams_;
  return stream;
}

void QuicChromiumClientSession::CloseStream(QuicStreamId stream_id) {
  QuicStream* stream = GetOrCreateStream(stream_id);
  if (stream) {
    logger_->UpdateReceivedFrameCounts(stream_id, stream->num_frames_received(),
                                       stream->num_duplicate_frames_received());
    if (stream_id % 2 == 0) {
      // Stream with even stream is initiated by server for PUSH.
      bytes_pushed_count_ += stream->stream_bytes_read();
    }
  }
  QuicSpdySession::CloseStream(stream_id);
  OnClosedStream();
}

void QuicChromiumClientSession::SendRstStream(QuicStreamId id,
                                              QuicRstStreamErrorCode error,
                                              QuicStreamOffset bytes_written) {
  QuicStream* stream = GetOrCreateStream(id);
  if (stream) {
    if (id % 2 == 0) {
      // Stream with even stream is initiated by server for PUSH.
      bytes_pushed_count_ += stream->stream_bytes_read();
    }
  }
  QuicSpdySession::SendRstStream(id, error, bytes_written);
  OnClosedStream();
}

void QuicChromiumClientSession::OnClosedStream() {
  if (GetNumOpenOutgoingStreams() < max_open_outgoing_streams() &&
      !stream_requests_.empty() && crypto_stream_->encryption_established() &&
      !goaway_received() && !going_away_ && connection()->connected()) {
    StreamRequest* request = stream_requests_.front();
    // TODO(ckrasic) - analyze data and then add logic to mark QUIC
    // broken if wait times are excessive.
    UMA_HISTOGRAM_TIMES("Net.QuicSession.PendingStreamsWaitTime",
                        base::TimeTicks::Now() - request->pending_start_time_);
    stream_requests_.pop_front();
    request->OnRequestCompleteSuccess(
        CreateOutgoingReliableStreamImpl()->CreateHandle());
  }

  if (GetNumOpenOutgoingStreams() == 0 && stream_factory_) {
    stream_factory_->OnIdleSession(this);
  }
}

void QuicChromiumClientSession::OnConfigNegotiated() {
  QuicSpdyClientSessionBase::OnConfigNegotiated();
  if (!stream_factory_ || !config()->HasReceivedAlternateServerAddress())
    return;

  // Server has sent an alternate address to connect to.
  IPEndPoint new_address =
      config()->ReceivedAlternateServerAddress().impl().socket_address();
  IPEndPoint old_address;
  GetDefaultSocket()->GetPeerAddress(&old_address);

  // Migrate only if address families match, or if new address family is v6,
  // since a v4 address should be reachable over a v6 network (using a
  // v4-mapped v6 address).
  if (old_address.GetFamily() != new_address.GetFamily() &&
      old_address.GetFamily() == ADDRESS_FAMILY_IPV4) {
    return;
  }

  if (old_address.GetFamily() != new_address.GetFamily()) {
    DCHECK_EQ(old_address.GetFamily(), ADDRESS_FAMILY_IPV6);
    DCHECK_EQ(new_address.GetFamily(), ADDRESS_FAMILY_IPV4);
    // Use a v4-mapped v6 address.
    new_address = IPEndPoint(ConvertIPv4ToIPv4MappedIPv6(new_address.address()),
                             new_address.port());
  }

  stream_factory_->MigrateSessionToNewPeerAddress(this, new_address, net_log_);
}

void QuicChromiumClientSession::OnCryptoHandshakeEvent(
    CryptoHandshakeEvent event) {
  if (!callback_.is_null() &&
      (!require_confirmation_ || event == HANDSHAKE_CONFIRMED ||
       event == ENCRYPTION_REESTABLISHED)) {
    // TODO(rtenneti): Currently for all CryptoHandshakeEvent events, callback_
    // could be called because there are no error events in CryptoHandshakeEvent
    // enum. If error events are added to CryptoHandshakeEvent, then the
    // following code needs to changed.
    base::ResetAndReturn(&callback_).Run(OK);
  }
  if (event == HANDSHAKE_CONFIRMED) {
    if (stream_factory_)
      stream_factory_->set_require_confirmation(false);

    // Update |connect_end| only when handshake is confirmed. This should also
    // take care of any failed 0-RTT request.
    connect_timing_.connect_end = base::TimeTicks::Now();
    DCHECK_LE(connect_timing_.connect_start, connect_timing_.connect_end);
    UMA_HISTOGRAM_TIMES(
        "Net.QuicSession.HandshakeConfirmedTime",
        connect_timing_.connect_end - connect_timing_.connect_start);
    // Track how long it has taken to finish handshake after we have finished
    // DNS host resolution.
    if (!connect_timing_.dns_end.is_null()) {
      UMA_HISTOGRAM_TIMES(
          "Net.QuicSession.HostResolution.HandshakeConfirmedTime",
          base::TimeTicks::Now() - connect_timing_.dns_end);
    }

    HandleSet::iterator it = handles_.begin();
    while (it != handles_.end()) {
      Handle* handle = *it;
      ++it;
      handle->OnCryptoHandshakeConfirmed();
    }

    NotifyRequestsOfConfirmation(OK);
  }
  QuicSpdySession::OnCryptoHandshakeEvent(event);
}

void QuicChromiumClientSession::OnCryptoHandshakeMessageSent(
    const CryptoHandshakeMessage& message) {
  logger_->OnCryptoHandshakeMessageSent(message);
}

void QuicChromiumClientSession::OnCryptoHandshakeMessageReceived(
    const CryptoHandshakeMessage& message) {
  logger_->OnCryptoHandshakeMessageReceived(message);
  if (message.tag() == kREJ || message.tag() == kSREJ) {
    UMA_HISTOGRAM_CUSTOM_COUNTS(
        "Net.QuicSession.RejectLength",
        message.GetSerialized(Perspective::IS_CLIENT).length(), 1000, 10000,
        50);
    QuicStringPiece proof;
    UMA_HISTOGRAM_BOOLEAN("Net.QuicSession.RejectHasProof",
                          message.GetStringPiece(kPROF, &proof));
  }
}

void QuicChromiumClientSession::OnGoAway(const QuicGoAwayFrame& frame) {
  QuicSession::OnGoAway(frame);
  NotifyFactoryOfSessionGoingAway();
  port_migration_detected_ = frame.error_code == QUIC_ERROR_MIGRATING_PORT;
}

void QuicChromiumClientSession::OnRstStream(const QuicRstStreamFrame& frame) {
  QuicSession::OnRstStream(frame);
  OnClosedStream();
}

void QuicChromiumClientSession::OnConnectionClosed(
    QuicErrorCode error,
    const std::string& error_details,
    ConnectionCloseSource source) {
  DCHECK(!connection()->connected());
  logger_->OnConnectionClosed(error, error_details, source);
  if (source == ConnectionCloseSource::FROM_PEER) {
    if (IsCryptoHandshakeConfirmed()) {
      UMA_HISTOGRAM_SPARSE_SLOWLY(
          "Net.QuicSession.ConnectionCloseErrorCodeServer.HandshakeConfirmed",
          error);
      base::HistogramBase* histogram = base::SparseHistogram::FactoryGet(
          "Net.QuicSession.StreamCloseErrorCodeServer.HandshakeConfirmed",
          base::HistogramBase::kUmaTargetedHistogramFlag);
      size_t num_streams = GetNumActiveStreams();
      if (num_streams > 0)
        histogram->AddCount(error, num_streams);
    }
    UMA_HISTOGRAM_SPARSE_SLOWLY(
        "Net.QuicSession.ConnectionCloseErrorCodeServer", error);
  } else {
    if (IsCryptoHandshakeConfirmed()) {
      UMA_HISTOGRAM_SPARSE_SLOWLY(
          "Net.QuicSession.ConnectionCloseErrorCodeClient.HandshakeConfirmed",
          error);
      base::HistogramBase* histogram = base::SparseHistogram::FactoryGet(
          "Net.QuicSession.StreamCloseErrorCodeClient.HandshakeConfirmed",
          base::HistogramBase::kUmaTargetedHistogramFlag);
      size_t num_streams = GetNumActiveStreams();
      if (num_streams > 0)
        histogram->AddCount(error, num_streams);
    }
    UMA_HISTOGRAM_SPARSE_SLOWLY(
        "Net.QuicSession.ConnectionCloseErrorCodeClient", error);
  }

  if (error == QUIC_NETWORK_IDLE_TIMEOUT) {
    UMA_HISTOGRAM_COUNTS_1M(
        "Net.QuicSession.ConnectionClose.NumOpenStreams.TimedOut",
        GetNumOpenOutgoingStreams());
    if (IsCryptoHandshakeConfirmed()) {
      if (GetNumOpenOutgoingStreams() > 0) {
        UMA_HISTOGRAM_BOOLEAN(
            "Net.QuicSession.TimedOutWithOpenStreams.HasUnackedPackets",
            connection()->sent_packet_manager().HasUnackedPackets());
        UMA_HISTOGRAM_COUNTS_1M(
            "Net.QuicSession.TimedOutWithOpenStreams.ConsecutiveRTOCount",
            connection()->sent_packet_manager().GetConsecutiveRtoCount());
        UMA_HISTOGRAM_COUNTS_1M(
            "Net.QuicSession.TimedOutWithOpenStreams.ConsecutiveTLPCount",
            connection()->sent_packet_manager().GetConsecutiveTlpCount());
        UMA_HISTOGRAM_SPARSE_SLOWLY(
            "Net.QuicSession.TimedOutWithOpenStreams.LocalPort",
            connection()->self_address().port());
      }
    } else {
      UMA_HISTOGRAM_COUNTS_1M(
          "Net.QuicSession.ConnectionClose.NumOpenStreams.HandshakeTimedOut",
          GetNumOpenOutgoingStreams());
      UMA_HISTOGRAM_COUNTS_1M(
          "Net.QuicSession.ConnectionClose.NumTotalStreams.HandshakeTimedOut",
          num_total_streams_);
    }
  }

  if (IsCryptoHandshakeConfirmed()) {
    // QUIC connections should not timeout while there are open streams,
    // since PING frames are sent to prevent timeouts. If, however, the
    // connection timed out with open streams then QUIC traffic has become
    // blackholed. Alternatively, if too many retransmission timeouts occur
    // then QUIC traffic has become blackholed.
    if (stream_factory_ &&
        (error == QUIC_TOO_MANY_RTOS || (error == QUIC_NETWORK_IDLE_TIMEOUT &&
                                         GetNumOpenOutgoingStreams() > 0))) {
      stream_factory_->OnBlackholeAfterHandshakeConfirmed(this);
    }
  } else {
    if (error == QUIC_PUBLIC_RESET) {
      RecordHandshakeFailureReason(HANDSHAKE_FAILURE_PUBLIC_RESET);
    } else if (connection()->GetStats().packets_received == 0) {
      RecordHandshakeFailureReason(HANDSHAKE_FAILURE_BLACK_HOLE);
      UMA_HISTOGRAM_SPARSE_SLOWLY(
          "Net.QuicSession.ConnectionClose.HandshakeFailureBlackHole.QuicError",
          error);
    } else {
      RecordHandshakeFailureReason(HANDSHAKE_FAILURE_UNKNOWN);
      UMA_HISTOGRAM_SPARSE_SLOWLY(
          "Net.QuicSession.ConnectionClose.HandshakeFailureUnknown.QuicError",
          error);
    }
  }

  UMA_HISTOGRAM_SPARSE_SLOWLY("Net.QuicSession.QuicVersion",
                              connection()->transport_version());
  NotifyFactoryOfSessionGoingAway();
  QuicSession::OnConnectionClosed(error, error_details, source);

  if (!callback_.is_null()) {
    base::ResetAndReturn(&callback_).Run(ERR_QUIC_PROTOCOL_ERROR);
  }

  for (auto& socket : sockets_) {
    socket->Close();
  }
  DCHECK(dynamic_streams().empty());
  CloseAllStreams(ERR_UNEXPECTED);
  CloseAllHandles(ERR_UNEXPECTED);
  CancelAllRequests(ERR_CONNECTION_CLOSED);
  NotifyRequestsOfConfirmation(ERR_CONNECTION_CLOSED);
  NotifyFactoryOfSessionClosedLater();
}

void QuicChromiumClientSession::OnSuccessfulVersionNegotiation(
    const QuicTransportVersion& version) {
  logger_->OnSuccessfulVersionNegotiation(version);
  QuicSpdySession::OnSuccessfulVersionNegotiation(version);
}

int QuicChromiumClientSession::HandleWriteError(
    int error_code,
    scoped_refptr<QuicChromiumPacketWriter::ReusableIOBuffer> packet) {
  if (stream_factory_ == nullptr ||
      !stream_factory_->migrate_sessions_on_network_change()) {
    return error_code;
  }
  DCHECK(packet != nullptr);
  DCHECK_NE(ERR_IO_PENDING, error_code);
  DCHECK_GT(0, error_code);
  DCHECK(!migration_pending_);
  DCHECK(packet_ == nullptr);

  // Post a task to migrate the session onto a new network.
  task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&QuicChromiumClientSession::MigrateSessionOnWriteError,
                 weak_factory_.GetWeakPtr(), error_code));

  // Store packet in the session since the actual migration and packet rewrite
  // can happen via this posted task or via an async network notification.
  packet_ = std::move(packet);
  migration_pending_ = true;

  // Cause the packet writer to return ERR_IO_PENDING and block so
  // that the actual migration happens from the message loop instead
  // of under the call stack of QuicConnection::WritePacket.
  return ERR_IO_PENDING;
}

void QuicChromiumClientSession::MigrateSessionOnWriteError(int error_code) {
  // If migration_pending_ is false, an earlier task completed migration.
  if (!migration_pending_)
    return;

  MigrationResult result = MigrationResult::FAILURE;
  if (stream_factory_ != nullptr)
    result = stream_factory_->MaybeMigrateSingleSessionOnWriteError(this,
                                                                    error_code);

  if (result == MigrationResult::SUCCESS)
    return;

  if (result == MigrationResult::NO_NEW_NETWORK) {
    OnNoNewNetwork();
    return;
  }

  // Close the connection if migration failed. Do not cause a
  // connection close packet to be sent since socket may be borked.
  connection()->CloseConnection(QUIC_PACKET_WRITE_ERROR,
                                "Write and subsequent migration failed",
                                ConnectionCloseBehavior::SILENT_CLOSE);
}

void QuicChromiumClientSession::OnNoNewNetwork() {
  migration_pending_ = true;

  // Block the packet writer to avoid any writes while migration is in progress.
  static_cast<QuicChromiumPacketWriter*>(connection()->writer())
      ->set_write_blocked(true);

  // Post a task to maybe close the session if the alarm fires.
  task_runner_->PostDelayedTask(
      FROM_HERE, base::Bind(&QuicChromiumClientSession::OnMigrationTimeout,
                            weak_factory_.GetWeakPtr(), sockets_.size()),
      base::TimeDelta::FromSeconds(kWaitTimeForNewNetworkSecs));
}

void QuicChromiumClientSession::WriteToNewSocket() {
  // Prevent any pending migration from executing.
  migration_pending_ = false;
  static_cast<QuicChromiumPacketWriter*>(connection()->writer())
      ->set_write_blocked(false);
  if (packet_ == nullptr) {
    // Unblock the connection before sending a PING packet, since it
    // may have been blocked before the migration started.
    connection()->OnCanWrite();
    connection()->SendPing();
    return;
  }

  // The connection is waiting for the original write to complete
  // asynchronously. The new writer will notify the connection if the
  // write below completes asynchronously, but a synchronous competion
  // must be propagated back to the connection here.
  WriteResult result =
      static_cast<QuicChromiumPacketWriter*>(connection()->writer())
          ->WritePacketToSocket(std::move(packet_));
  if (result.error_code == ERR_IO_PENDING)
    return;

  // All write errors should be mapped into ERR_IO_PENDING by
  // HandleWriteError.
  DCHECK_LT(0, result.error_code);
  connection()->OnCanWrite();
}

void QuicChromiumClientSession::OnMigrationTimeout(size_t num_sockets) {
  // If number of sockets has changed, this migration task is stale.
  if (num_sockets != sockets_.size())
    return;
  UMA_HISTOGRAM_ENUMERATION("Net.QuicSession.ConnectionMigration",
                            MIGRATION_STATUS_NO_ALTERNATE_NETWORK,
                            MIGRATION_STATUS_MAX);
  CloseSessionOnError(ERR_NETWORK_CHANGED,
                      QUIC_CONNECTION_MIGRATION_NO_NEW_NETWORK);
}

void QuicChromiumClientSession::OnNetworkConnected(
    NetworkChangeNotifier::NetworkHandle network,
    const NetLogWithSource& net_log) {
  // If migration_pending_ is false, there was no migration pending or
  // an earlier task completed migration.
  if (!migration_pending_)
    return;

  // TODO(jri): Ensure that OnSessionGoingAway is called consistently,
  // and that it's always called at the same time in the whole
  // migration process. Allows tests to be more uniform.
  stream_factory_->OnSessionGoingAway(this);
  stream_factory_->MigrateSessionToNewNetwork(
      this, network, /*close_session_on_error=*/true, net_log);
}

void QuicChromiumClientSession::OnWriteError(int error_code) {
  DCHECK_NE(ERR_IO_PENDING, error_code);
  DCHECK_GT(0, error_code);
  if (IsCryptoHandshakeConfirmed()) {
    UMA_HISTOGRAM_SPARSE_SLOWLY("Net.QuicSession.WriteError.HandshakeConfirmed",
                                -error_code);
  }
  connection()->OnWriteError(error_code);
}

void QuicChromiumClientSession::OnWriteUnblocked() {
  connection()->OnCanWrite();
}

void QuicChromiumClientSession::OnPathDegrading() {
  if (stream_factory_) {
    stream_factory_->MaybeMigrateSingleSessionOnPathDegrading(this);
  }
}

bool QuicChromiumClientSession::HasOpenDynamicStreams() const {
  return QuicSession::HasOpenDynamicStreams() ||
         GetNumDrainingOutgoingStreams() > 0;
}

void QuicChromiumClientSession::OnProofValid(
    const QuicCryptoClientConfig::CachedState& cached) {
  DCHECK(cached.proof_valid());

  if (!server_info_) {
    return;
  }

  QuicServerInfo::State* state = server_info_->mutable_state();

  state->server_config = cached.server_config();
  state->source_address_token = cached.source_address_token();
  state->cert_sct = cached.cert_sct();
  state->chlo_hash = cached.chlo_hash();
  state->server_config_sig = cached.signature();
  state->certs = cached.certs();

  server_info_->Persist();
}

void QuicChromiumClientSession::OnProofVerifyDetailsAvailable(
    const ProofVerifyDetails& verify_details) {
  const ProofVerifyDetailsChromium* verify_details_chromium =
      reinterpret_cast<const ProofVerifyDetailsChromium*>(&verify_details);
  cert_verify_result_.reset(
      new CertVerifyResult(verify_details_chromium->cert_verify_result));
  pinning_failure_log_ = verify_details_chromium->pinning_failure_log;
  std::unique_ptr<ct::CTVerifyResult> ct_verify_result_copy(
      new ct::CTVerifyResult(verify_details_chromium->ct_verify_result));
  ct_verify_result_ = std::move(ct_verify_result_copy);
  logger_->OnCertificateVerified(*cert_verify_result_);
  pkp_bypassed_ = verify_details_chromium->pkp_bypassed;
}

void QuicChromiumClientSession::StartReading() {
  for (auto& packet_reader : packet_readers_) {
    packet_reader->StartReading();
  }
}

void QuicChromiumClientSession::CloseSessionOnError(int net_error,
                                                    QuicErrorCode quic_error) {
  UMA_HISTOGRAM_SPARSE_SLOWLY("Net.QuicSession.CloseSessionOnError",
                              -net_error);

  if (!callback_.is_null()) {
    base::ResetAndReturn(&callback_).Run(net_error);
  }
  CloseAllStreams(net_error);
  CloseAllHandles(net_error);
  net_log_.AddEvent(NetLogEventType::QUIC_SESSION_CLOSE_ON_ERROR,
                    NetLog::IntCallback("net_error", net_error));

  if (connection()->connected())
    connection()->CloseConnection(quic_error, "net error",
                                  ConnectionCloseBehavior::SILENT_CLOSE);
  DCHECK(!connection()->connected());

  NotifyFactoryOfSessionClosed();
}

void QuicChromiumClientSession::CloseAllStreams(int net_error) {
  while (!dynamic_streams().empty()) {
    QuicStream* stream = dynamic_streams().begin()->second.get();
    QuicStreamId id = stream->id();
    static_cast<QuicChromiumClientStream*>(stream)->OnError(net_error);
    CloseStream(id);
  }
}

void QuicChromiumClientSession::CloseAllHandles(int net_error) {
  while (!handles_.empty()) {
    Handle* handle = *handles_.begin();
    handles_.erase(handle);
    handle->OnSessionClosed(connection()->transport_version(), net_error,
                            error(), port_migration_detected_,
                            GetConnectTiming(), WasConnectionEverUsed());
  }
}

void QuicChromiumClientSession::CancelAllRequests(int net_error) {
  UMA_HISTOGRAM_COUNTS_1000("Net.QuicSession.AbortedPendingStreamRequests",
                            stream_requests_.size());

  while (!stream_requests_.empty()) {
    StreamRequest* request = stream_requests_.front();
    stream_requests_.pop_front();
    request->OnRequestCompleteFailure(net_error);
  }
}

void QuicChromiumClientSession::NotifyRequestsOfConfirmation(int net_error) {
  // Post tasks to avoid reentrancy.
  for (auto callback : waiting_for_confirmation_callbacks_)
    task_runner_->PostTask(FROM_HERE, base::Bind(callback, net_error));

  waiting_for_confirmation_callbacks_.clear();
}

std::unique_ptr<base::Value> QuicChromiumClientSession::GetInfoAsValue(
    const std::set<HostPortPair>& aliases) {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  dict->SetString("version",
                  QuicVersionToString(connection()->transport_version()));
  dict->SetInteger("open_streams", GetNumOpenOutgoingStreams());
  std::unique_ptr<base::ListValue> stream_list(new base::ListValue());
  for (DynamicStreamMap::const_iterator it = dynamic_streams().begin();
       it != dynamic_streams().end(); ++it) {
    stream_list->AppendString(base::UintToString(it->second->id()));
  }
  dict->Set("active_streams", std::move(stream_list));

  dict->SetInteger("total_streams", num_total_streams_);
  dict->SetString("peer_address", peer_address().ToString());
  dict->SetString("connection_id", base::Uint64ToString(connection_id()));
  dict->SetBoolean("connected", connection()->connected());
  const QuicConnectionStats& stats = connection()->GetStats();
  dict->SetInteger("packets_sent", stats.packets_sent);
  dict->SetInteger("packets_received", stats.packets_received);
  dict->SetInteger("packets_lost", stats.packets_lost);
  SSLInfo ssl_info;

  std::unique_ptr<base::ListValue> alias_list(new base::ListValue());
  for (std::set<HostPortPair>::const_iterator it = aliases.begin();
       it != aliases.end(); it++) {
    alias_list->AppendString(it->ToString());
  }
  dict->Set("aliases", std::move(alias_list));

  return std::move(dict);
}

std::unique_ptr<QuicChromiumClientSession::Handle>
QuicChromiumClientSession::CreateHandle() {
  return std::make_unique<QuicChromiumClientSession::Handle>(
      weak_factory_.GetWeakPtr());
}

void QuicChromiumClientSession::OnReadError(
    int result,
    const DatagramClientSocket* socket) {
  DCHECK(socket != nullptr);
  if (socket != GetDefaultSocket()) {
    // Ignore read errors from old sockets that are no longer active.
    // TODO(jri): Maybe clean up old sockets on error.
    return;
  }
  DVLOG(1) << "Closing session on read error: " << result;
  UMA_HISTOGRAM_SPARSE_SLOWLY("Net.QuicSession.ReadError", -result);
  connection()->CloseConnection(QUIC_PACKET_READ_ERROR, ErrorToString(result),
                                ConnectionCloseBehavior::SILENT_CLOSE);
}

bool QuicChromiumClientSession::OnPacket(
    const QuicReceivedPacket& packet,
    const QuicSocketAddress& local_address,
    const QuicSocketAddress& peer_address) {
  ProcessUdpPacket(local_address, peer_address, packet);
  if (!connection()->connected()) {
    NotifyFactoryOfSessionClosedLater();
    return false;
  }
  return true;
}

void QuicChromiumClientSession::NotifyFactoryOfSessionGoingAway() {
  going_away_ = true;
  if (stream_factory_)
    stream_factory_->OnSessionGoingAway(this);
}

void QuicChromiumClientSession::NotifyFactoryOfSessionClosedLater() {
  if (!dynamic_streams().empty())
    RecordUnexpectedOpenStreams(NOTIFY_FACTORY_OF_SESSION_CLOSED_LATER);

  if (!going_away_)
    RecordUnexpectedNotGoingAway(NOTIFY_FACTORY_OF_SESSION_CLOSED_LATER);

  going_away_ = true;
  DCHECK_EQ(0u, GetNumActiveStreams());
  DCHECK(!connection()->connected());
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&QuicChromiumClientSession::NotifyFactoryOfSessionClosed,
                 weak_factory_.GetWeakPtr()));
}

void QuicChromiumClientSession::NotifyFactoryOfSessionClosed() {
  if (!dynamic_streams().empty())
    RecordUnexpectedOpenStreams(NOTIFY_FACTORY_OF_SESSION_CLOSED);

  if (!going_away_)
    RecordUnexpectedNotGoingAway(NOTIFY_FACTORY_OF_SESSION_CLOSED);

  going_away_ = true;
  DCHECK_EQ(0u, GetNumActiveStreams());
  // Will delete |this|.
  if (stream_factory_)
    stream_factory_->OnSessionClosed(this);
}

bool QuicChromiumClientSession::MigrateToSocket(
    std::unique_ptr<DatagramClientSocket> socket,
    std::unique_ptr<QuicChromiumPacketReader> reader,
    std::unique_ptr<QuicChromiumPacketWriter> writer) {
  DCHECK_EQ(sockets_.size(), packet_readers_.size());
  if (sockets_.size() >= kMaxReadersPerQuicSession)
    return false;

  // TODO(jri): Make SetQuicPacketWriter take a scoped_ptr.
  packet_readers_.push_back(std::move(reader));
  sockets_.push_back(std::move(socket));
  StartReading();
  // Block the writer to prevent is being used until WriteToNewSocket
  // completes.
  writer->set_write_blocked(true);
  connection()->SetQuicPacketWriter(writer.release(), /*owns_writer=*/true);

  // Post task to write the pending packet or a PING packet to the new
  // socket. This avoids reentrancy issues if there is a write error
  // on the write to the new socket.
  task_runner_->PostTask(
      FROM_HERE, base::Bind(&QuicChromiumClientSession::WriteToNewSocket,
                            weak_factory_.GetWeakPtr()));
  // Migration completed.
  migration_pending_ = false;
  return true;
}

void QuicChromiumClientSession::PopulateNetErrorDetails(
    NetErrorDetails* details) const {
  details->quic_port_migration_detected = port_migration_detected_;
  details->quic_connection_error = error();
}

const DatagramClientSocket* QuicChromiumClientSession::GetDefaultSocket()
    const {
  DCHECK(sockets_.back().get() != nullptr);
  // The most recently added socket is the currently active one.
  return sockets_.back().get();
}

bool QuicChromiumClientSession::IsAuthorized(const std::string& hostname) {
  bool result = CanPool(hostname, server_id_.privacy_mode());
  if (result)
    streams_pushed_count_++;
  return result;
}

bool QuicChromiumClientSession::HasNonMigratableStreams() const {
  for (const auto& stream : dynamic_streams()) {
    if (!static_cast<QuicChromiumClientStream*>(stream.second.get())
             ->can_migrate()) {
      return true;
    }
  }
  return false;
}

bool QuicChromiumClientSession::HandlePromised(QuicStreamId id,
                                               QuicStreamId promised_id,
                                               const SpdyHeaderBlock& headers) {
  bool result =
      QuicSpdyClientSessionBase::HandlePromised(id, promised_id, headers);
  if (result) {
    // The push promise is accepted, notify the push_delegate that a push
    // promise has been received.
    GURL pushed_url = GetUrlFromHeaderBlock(headers);
    if (push_delegate_) {
      push_delegate_->OnPush(std::make_unique<QuicServerPushHelper>(
                                 weak_factory_.GetWeakPtr(), pushed_url),
                             net_log_);
    }
  }
  net_log_.AddEvent(NetLogEventType::QUIC_SESSION_PUSH_PROMISE_RECEIVED,
                    base::Bind(&NetLogQuicPushPromiseReceivedCallback, &headers,
                               id, promised_id));
  return result;
}

void QuicChromiumClientSession::DeletePromised(
    QuicClientPromisedInfo* promised) {
  if (IsOpenStream(promised->id()))
    streams_pushed_and_claimed_count_++;
  QuicSpdyClientSessionBase::DeletePromised(promised);
}

void QuicChromiumClientSession::OnPushStreamTimedOut(QuicStreamId stream_id) {
  QuicSpdyStream* stream = GetPromisedStream(stream_id);
  if (stream != nullptr)
    bytes_pushed_and_unclaimed_count_ += stream->stream_bytes_read();
}

void QuicChromiumClientSession::CancelPush(const GURL& url) {
  QuicClientPromisedInfo* promised_info =
      QuicSpdyClientSessionBase::GetPromisedByUrl(url.spec());
  if (!promised_info || promised_info->is_validating()) {
    // Push stream has already been claimed or is pending matched to a request.
    return;
  }

  QuicStreamId stream_id = promised_info->id();

  // Collect data on the cancelled push stream.
  QuicSpdyStream* stream = GetPromisedStream(stream_id);
  if (stream != nullptr)
    bytes_pushed_and_unclaimed_count_ += stream->stream_bytes_read();

  // Send the reset and remove the promised info from the promise index.
  QuicSpdyClientSessionBase::ResetPromised(stream_id, QUIC_STREAM_CANCELLED);
  DeletePromised(promised_info);
}

const LoadTimingInfo::ConnectTiming&
QuicChromiumClientSession::GetConnectTiming() {
  connect_timing_.ssl_start = connect_timing_.connect_start;
  connect_timing_.ssl_end = connect_timing_.connect_end;
  return connect_timing_;
}

QuicTransportVersion QuicChromiumClientSession::GetQuicVersion() const {
  return connection()->transport_version();
}

size_t QuicChromiumClientSession::EstimateMemoryUsage() const {
  // TODO(xunjieli): Estimate |crypto_stream_|, QuicSpdySession's
  // QuicHeaderList, QuicSession's QuiCWriteBlockedList, open streams and
  // unacked packet map.
  return base::trace_event::EstimateMemoryUsage(packet_readers_);
}

}  // namespace net
