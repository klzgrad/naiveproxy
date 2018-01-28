// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/chromium/quic_stream_factory.h"

#include <algorithm>
#include <memory>
#include <tuple>
#include <utility>

#include "base/auto_reset.h"
#include "base/callback_helpers.h"
#include "base/location.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/sparse_histogram.h"
#include "base/rand_util.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/trace_event/memory_allocator_dump.h"
#include "base/trace_event/memory_usage_estimator.h"
#include "base/trace_event/process_memory_dump.h"
#include "base/trace_event/trace_event.h"
#include "base/values.h"
#include "crypto/openssl_util.h"
#include "net/base/ip_address.h"
#include "net/base/net_errors.h"
#include "net/base/trace_constants.h"
#include "net/cert/cert_verifier.h"
#include "net/cert/ct_verifier.h"
#include "net/dns/host_resolver.h"
#include "net/http/bidirectional_stream_impl.h"
#include "net/log/net_log_capture_mode.h"
#include "net/log/net_log_event_type.h"
#include "net/log/net_log_source_type.h"
#include "net/quic/chromium/bidirectional_stream_quic_impl.h"
#include "net/quic/chromium/crypto/channel_id_chromium.h"
#include "net/quic/chromium/crypto/proof_verifier_chromium.h"
#include "net/quic/chromium/properties_based_quic_server_info.h"
#include "net/quic/chromium/quic_chromium_alarm_factory.h"
#include "net/quic/chromium/quic_chromium_connection_helper.h"
#include "net/quic/chromium/quic_chromium_packet_reader.h"
#include "net/quic/chromium/quic_chromium_packet_writer.h"
#include "net/quic/chromium/quic_crypto_client_stream_factory.h"
#include "net/quic/chromium/quic_server_info.h"
#include "net/quic/core/crypto/proof_verifier.h"
#include "net/quic/core/crypto/quic_random.h"
#include "net/quic/core/quic_client_promised_info.h"
#include "net/quic/core/quic_connection.h"
#include "net/quic/platform/api/quic_clock.h"
#include "net/quic/platform/api/quic_flags.h"
#include "net/socket/client_socket_factory.h"
#include "net/socket/next_proto.h"
#include "net/socket/socket_performance_watcher.h"
#include "net/socket/socket_performance_watcher_factory.h"
#include "net/socket/udp_client_socket.h"
#include "net/ssl/token_binding.h"
#include "third_party/boringssl/src/include/openssl/aead.h"
#include "url/gurl.h"
#include "url/url_constants.h"

using NetworkHandle = net::NetworkChangeNotifier::NetworkHandle;

namespace net {

// Returns the estimate of dynamically allocated memory of an IPEndPoint in
// bytes. Used in tracking IPAliasMap.
size_t EstimateMemoryUsage(const IPEndPoint& end_point) {
  return 0;
}

namespace {

enum CreateSessionFailure {
  CREATION_ERROR_CONNECTING_SOCKET,
  CREATION_ERROR_SETTING_RECEIVE_BUFFER,
  CREATION_ERROR_SETTING_SEND_BUFFER,
  CREATION_ERROR_SETTING_DO_NOT_FRAGMENT,
  CREATION_ERROR_MAX
};

enum InitialRttEstimateSource {
  INITIAL_RTT_DEFAULT,
  INITIAL_RTT_CACHED,
  INITIAL_RTT_2G,
  INITIAL_RTT_3G,
  INITIAL_RTT_SOURCE_MAX,
};

// The maximum receive window sizes for QUIC sessions and streams.
const int32_t kQuicSessionMaxRecvWindowSize = 15 * 1024 * 1024;  // 15 MB
const int32_t kQuicStreamMaxRecvWindowSize = 6 * 1024 * 1024;    // 6 MB

// QUIC's socket receive buffer size.
// We should adaptively set this buffer size, but for now, we'll use a size
// that seems large enough to receive data at line rate for most connections,
// and does not consume "too much" memory.
const int32_t kQuicSocketReceiveBufferSize = 1024 * 1024;  // 1MB

// Set the maximum number of undecryptable packets the connection will store.
const int32_t kMaxUndecryptablePackets = 100;

std::unique_ptr<base::Value> NetLogQuicStreamFactoryJobCallback(
    const QuicServerId* server_id,
    NetLogCaptureMode capture_mode) {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  dict->SetString("server_id", server_id->ToString());
  return std::move(dict);
}

std::unique_ptr<base::Value> NetLogQuicConnectionMigrationTriggerCallback(
    std::string trigger,
    NetLogCaptureMode capture_mode) {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  dict->SetString("trigger", trigger);
  return std::move(dict);
}

std::unique_ptr<base::Value> NetLogQuicConnectionMigrationFailureCallback(
    QuicConnectionId connection_id,
    std::string reason,
    NetLogCaptureMode capture_mode) {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  dict->SetString("connection_id", base::Uint64ToString(connection_id));
  dict->SetString("reason", reason);
  return std::move(dict);
}

std::unique_ptr<base::Value> NetLogQuicConnectionMigrationSuccessCallback(
    QuicConnectionId connection_id,
    NetLogCaptureMode capture_mode) {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  dict->SetString("connection_id", base::Uint64ToString(connection_id));
  return std::move(dict);
}

// Helper class that is used to log a connection migration event.
class ScopedConnectionMigrationEventLog {
 public:
  ScopedConnectionMigrationEventLog(NetLog* net_log, std::string trigger)
      : net_log_(NetLogWithSource::Make(
            net_log,
            NetLogSourceType::QUIC_CONNECTION_MIGRATION)) {
    net_log_.BeginEvent(
        NetLogEventType::QUIC_CONNECTION_MIGRATION_TRIGGERED,
        base::Bind(&NetLogQuicConnectionMigrationTriggerCallback, trigger));
  }

  ~ScopedConnectionMigrationEventLog() {
    net_log_.EndEvent(NetLogEventType::QUIC_CONNECTION_MIGRATION_TRIGGERED);
  }

  const NetLogWithSource& net_log() { return net_log_; }

 private:
  const NetLogWithSource net_log_;
};

void HistogramCreateSessionFailure(enum CreateSessionFailure error) {
  UMA_HISTOGRAM_ENUMERATION("Net.QuicSession.CreationError", error,
                            CREATION_ERROR_MAX);
}

void HistogramAndLogMigrationFailure(const NetLogWithSource& net_log,
                                     enum QuicConnectionMigrationStatus status,
                                     QuicConnectionId connection_id,
                                     std::string reason) {
  UMA_HISTOGRAM_ENUMERATION("Net.QuicSession.ConnectionMigration", status,
                            MIGRATION_STATUS_MAX);
  net_log.AddEvent(NetLogEventType::QUIC_CONNECTION_MIGRATION_FAILURE,
                   base::Bind(&NetLogQuicConnectionMigrationFailureCallback,
                              connection_id, reason));
}

void HistogramMigrationStatus(enum QuicConnectionMigrationStatus status) {
  UMA_HISTOGRAM_ENUMERATION("Net.QuicSession.ConnectionMigration", status,
                            MIGRATION_STATUS_MAX);
}

void LogPlatformNotificationInHistogram(
    enum QuicPlatformNotification notification) {
  UMA_HISTOGRAM_ENUMERATION("Net.QuicSession.PlatformNotification",
                            notification, NETWORK_NOTIFICATION_MAX);
}

void SetInitialRttEstimate(base::TimeDelta estimate,
                           enum InitialRttEstimateSource source,
                           QuicConfig* config) {
  UMA_HISTOGRAM_ENUMERATION("Net.QuicSession.InitialRttEsitmateSource", source,
                            INITIAL_RTT_SOURCE_MAX);
  if (estimate != base::TimeDelta())
    config->SetInitialRoundTripTimeUsToSend(estimate.InMicroseconds());
}

QuicConfig InitializeQuicConfig(const QuicTagVector& connection_options,
                                const QuicTagVector& client_connection_options,
                                int idle_connection_timeout_seconds) {
  DCHECK_GT(idle_connection_timeout_seconds, 0);
  QuicConfig config;
  config.SetIdleNetworkTimeout(
      QuicTime::Delta::FromSeconds(idle_connection_timeout_seconds),
      QuicTime::Delta::FromSeconds(idle_connection_timeout_seconds));
  config.SetConnectionOptionsToSend(connection_options);
  config.SetClientConnectionOptions(client_connection_options);
  return config;
}

// An implementation of QuicCryptoClientConfig::ServerIdFilter that wraps
// an |origin_filter|.
class ServerIdOriginFilter : public QuicCryptoClientConfig::ServerIdFilter {
 public:
  ServerIdOriginFilter(const base::Callback<bool(const GURL&)> origin_filter)
      : origin_filter_(origin_filter) {}

  bool Matches(const QuicServerId& server_id) const override {
    if (origin_filter_.is_null())
      return true;

    GURL url(base::StringPrintf("%s%s%s:%d", url::kHttpsScheme,
                                url::kStandardSchemeSeparator,
                                server_id.host().c_str(), server_id.port()));
    DCHECK(url.is_valid());
    return origin_filter_.Run(url);
  }

 private:
  const base::Callback<bool(const GURL&)> origin_filter_;
};

// Returns the estimate of dynamically allocated memory of |server_id|.
size_t EstimateServerIdMemoryUsage(const QuicServerId& server_id) {
  return base::trace_event::EstimateMemoryUsage(server_id.host_port_pair());
}

}  // namespace

// Responsible for verifying the certificates saved in
// QuicCryptoClientConfig, and for notifying any associated requests when
// complete. Results from cert verification are ignored.
class QuicStreamFactory::CertVerifierJob {
 public:
  // ProofVerifierCallbackImpl is passed as the callback method to
  // VerifyCertChain. The ProofVerifier calls this class with the result of cert
  // verification when verification is performed asynchronously.
  class ProofVerifierCallbackImpl : public ProofVerifierCallback {
   public:
    explicit ProofVerifierCallbackImpl(CertVerifierJob* job) : job_(job) {}

    ~ProofVerifierCallbackImpl() override {}

    void Run(bool ok,
             const std::string& error_details,
             std::unique_ptr<ProofVerifyDetails>* details) override {
      if (job_ == nullptr)
        return;
      job_->verify_callback_ = nullptr;
      job_->OnComplete();
    }

    void Cancel() { job_ = nullptr; }

   private:
    CertVerifierJob* job_;
  };

  CertVerifierJob(const QuicServerId& server_id,
                  int cert_verify_flags,
                  const NetLogWithSource& net_log)
      : server_id_(server_id),
        verify_callback_(nullptr),
        verify_context_(
            std::make_unique<ProofVerifyContextChromium>(cert_verify_flags,
                                                         net_log)),
        start_time_(base::TimeTicks::Now()),
        net_log_(net_log),
        weak_factory_(this) {}

  ~CertVerifierJob() {
    if (verify_callback_)
      verify_callback_->Cancel();
  }

  // Starts verification of certs cached in the |crypto_config|.
  QuicAsyncStatus Run(QuicCryptoClientConfig* crypto_config,
                      const CompletionCallback& callback) {
    QuicCryptoClientConfig::CachedState* cached =
        crypto_config->LookupOrCreate(server_id_);
    ProofVerifierCallbackImpl* verify_callback =
        new ProofVerifierCallbackImpl(this);
    QuicAsyncStatus status = crypto_config->proof_verifier()->VerifyCertChain(
        server_id_.host(), cached->certs(), verify_context_.get(),
        &verify_error_details_, &verify_details_,
        std::unique_ptr<ProofVerifierCallback>(verify_callback));
    if (status == QUIC_PENDING) {
      verify_callback_ = verify_callback;
      callback_ = callback;
    }
    return status;
  }

  void OnComplete() {
    UMA_HISTOGRAM_TIMES("Net.QuicSession.CertVerifierJob.CompleteTime",
                        base::TimeTicks::Now() - start_time_);
    if (!callback_.is_null())
      base::ResetAndReturn(&callback_).Run(OK);
  }

  const QuicServerId& server_id() const { return server_id_; }

  size_t EstimateMemoryUsage() const {
    // TODO(xunjieli): crbug.com/669108. Track |verify_context_| and
    // |verify_details_|.
    return base::trace_event::EstimateMemoryUsage(verify_error_details_);
  }

 private:
  const QuicServerId server_id_;
  ProofVerifierCallbackImpl* verify_callback_;
  std::unique_ptr<ProofVerifyContext> verify_context_;
  std::unique_ptr<ProofVerifyDetails> verify_details_;
  std::string verify_error_details_;
  const base::TimeTicks start_time_;
  const NetLogWithSource net_log_;
  CompletionCallback callback_;
  base::WeakPtrFactory<CertVerifierJob> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(CertVerifierJob);
};

// Responsible for creating a new QUIC session to the specified server, and
// for notifying any associated requests when complete.
class QuicStreamFactory::Job {
 public:
  Job(QuicStreamFactory* factory,
      const QuicTransportVersion& quic_version,
      HostResolver* host_resolver,
      const QuicSessionKey& key,
      bool was_alternative_service_recently_broken,
      int cert_verify_flags,
      const NetLogWithSource& net_log);

  ~Job();

  int Run(const CompletionCallback& callback);

  int DoLoop(int rv);
  int DoResolveHost();
  int DoResolveHostComplete(int rv);
  int DoConnect();
  int DoConnectComplete(int rv);

  void OnIOComplete(int rv);

  const QuicSessionKey& key() const { return key_; }

  const NetLogWithSource& net_log() const { return net_log_; }

  base::WeakPtr<Job> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  void PopulateNetErrorDetails(NetErrorDetails* details) const;

  // Returns the estimate of dynamically allocated memory in bytes.
  size_t EstimateMemoryUsage() const;

  void AddRequest(QuicStreamRequest* request) {
    CHECK(request->server_id() == key_.server_id());
    stream_requests_.insert(request);
  }

  void RemoveRequest(QuicStreamRequest* request) {
    auto request_iter = stream_requests_.find(request);
    CHECK(request_iter != stream_requests_.end());
    stream_requests_.erase(request_iter);
  }

  const std::set<QuicStreamRequest*>& stream_requests() {
    return stream_requests_;
  }

 private:
  enum IoState {
    STATE_NONE,
    STATE_RESOLVE_HOST,
    STATE_RESOLVE_HOST_COMPLETE,
    STATE_CONNECT,
    STATE_CONNECT_COMPLETE,
  };

  bool in_loop_;  // Temporary to investigate crbug.com/750271.
  IoState io_state_;
  QuicStreamFactory* factory_;
  QuicTransportVersion quic_version_;
  HostResolver* host_resolver_;
  std::unique_ptr<HostResolver::Request> request_;
  const QuicSessionKey key_;
  const int cert_verify_flags_;
  const bool was_alternative_service_recently_broken_;
  const NetLogWithSource net_log_;
  int num_sent_client_hellos_;
  QuicChromiumClientSession* session_;
  CompletionCallback callback_;
  AddressList address_list_;
  base::TimeTicks dns_resolution_start_time_;
  base::TimeTicks dns_resolution_end_time_;
  std::set<QuicStreamRequest*> stream_requests_;
  base::WeakPtrFactory<Job> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(Job);
};

QuicStreamFactory::Job::Job(QuicStreamFactory* factory,
                            const QuicTransportVersion& quic_version,
                            HostResolver* host_resolver,
                            const QuicSessionKey& key,
                            bool was_alternative_service_recently_broken,
                            int cert_verify_flags,
                            const NetLogWithSource& net_log)
    : in_loop_(false),
      io_state_(STATE_RESOLVE_HOST),
      factory_(factory),
      quic_version_(quic_version),
      host_resolver_(host_resolver),
      key_(key),
      cert_verify_flags_(cert_verify_flags),
      was_alternative_service_recently_broken_(
          was_alternative_service_recently_broken),
      net_log_(
          NetLogWithSource::Make(net_log.net_log(),
                                 NetLogSourceType::QUIC_STREAM_FACTORY_JOB)),
      num_sent_client_hellos_(0),
      session_(nullptr),
      weak_factory_(this) {
  net_log_.BeginEvent(
      NetLogEventType::QUIC_STREAM_FACTORY_JOB,
      base::Bind(&NetLogQuicStreamFactoryJobCallback, &key_.server_id()));
  // Associate |net_log_| with |net_log|.
  net_log_.AddEvent(
      NetLogEventType::QUIC_STREAM_FACTORY_JOB_BOUND_TO_HTTP_STREAM_JOB,
      net_log.source().ToEventParametersCallback());
  net_log.AddEvent(
      NetLogEventType::HTTP_STREAM_JOB_BOUND_TO_QUIC_STREAM_FACTORY_JOB,
      net_log_.source().ToEventParametersCallback());
}

QuicStreamFactory::Job::~Job() {
  net_log_.EndEvent(NetLogEventType::QUIC_STREAM_FACTORY_JOB);
  CHECK(!in_loop_);
  // If |this| is destroyed in QuicStreamFactory's destructor, |callback_| is
  // non-null.
}

int QuicStreamFactory::Job::Run(const CompletionCallback& callback) {
  int rv = DoLoop(OK);
  if (rv == ERR_IO_PENDING)
    callback_ = callback;

  return rv > 0 ? OK : rv;
}

int QuicStreamFactory::Job::DoLoop(int rv) {
  TRACE_EVENT0(kNetTracingCategory, "QuicStreamFactory::Job::DoLoop");
  CHECK(!in_loop_);
  base::AutoReset<bool> auto_reset_in_loop(&in_loop_, true);

  do {
    IoState state = io_state_;
    io_state_ = STATE_NONE;
    switch (state) {
      case STATE_RESOLVE_HOST:
        CHECK_EQ(OK, rv);
        rv = DoResolveHost();
        break;
      case STATE_RESOLVE_HOST_COMPLETE:
        rv = DoResolveHostComplete(rv);
        break;
      case STATE_CONNECT:
        CHECK_EQ(OK, rv);
        rv = DoConnect();
        break;
      case STATE_CONNECT_COMPLETE:
        rv = DoConnectComplete(rv);
        break;
      default:
        NOTREACHED() << "io_state_: " << io_state_;
        break;
    }
  } while (io_state_ != STATE_NONE && rv != ERR_IO_PENDING);
  return rv;
}

void QuicStreamFactory::Job::OnIOComplete(int rv) {
  rv = DoLoop(rv);
  if (rv != ERR_IO_PENDING && !callback_.is_null()) {
    CHECK(!in_loop_);
    base::ResetAndReturn(&callback_).Run(rv);
  }
}

void QuicStreamFactory::Job::PopulateNetErrorDetails(
    NetErrorDetails* details) const {
  if (!session_)
    return;
  details->connection_info = QuicHttpStream::ConnectionInfoFromQuicVersion(
      session_->connection()->transport_version());
  details->quic_connection_error = session_->error();
}

size_t QuicStreamFactory::Job::EstimateMemoryUsage() const {
  return base::trace_event::EstimateMemoryUsage(key_);
}

int QuicStreamFactory::Job::DoResolveHost() {
  dns_resolution_start_time_ = base::TimeTicks::Now();

  io_state_ = STATE_RESOLVE_HOST_COMPLETE;
  return host_resolver_->Resolve(
      HostResolver::RequestInfo(key_.destination()), DEFAULT_PRIORITY,
      &address_list_,
      base::Bind(&QuicStreamFactory::Job::OnIOComplete, GetWeakPtr()),
      &request_, net_log_);
}

int QuicStreamFactory::Job::DoResolveHostComplete(int rv) {
  dns_resolution_end_time_ = base::TimeTicks::Now();
  if (rv != OK)
    return rv;

  DCHECK(!factory_->HasActiveSession(key_.server_id()));

  // Inform the factory of this resolution, which will set up
  // a session alias, if possible.
  if (factory_->OnResolution(key_, address_list_))
    return OK;

  io_state_ = STATE_CONNECT;
  return OK;
}

int QuicStreamFactory::Job::DoConnect() {
  io_state_ = STATE_CONNECT_COMPLETE;

  bool require_confirmation = was_alternative_service_recently_broken_;
  net_log_.BeginEvent(
      NetLogEventType::QUIC_STREAM_FACTORY_JOB_CONNECT,
      NetLog::BoolCallback("require_confirmation", require_confirmation));

  DCHECK_NE(quic_version_, QUIC_VERSION_UNSUPPORTED);
  int rv = factory_->CreateSession(
      key_, quic_version_, cert_verify_flags_, require_confirmation,
      address_list_, dns_resolution_start_time_, dns_resolution_end_time_,
      net_log_, &session_);
  if (rv != OK) {
    DCHECK(rv != ERR_IO_PENDING);
    DCHECK(!session_);
    return rv;
  }

  if (!session_->connection()->connected())
    return ERR_CONNECTION_CLOSED;

  session_->StartReading();
  if (!session_->connection()->connected())
    return ERR_QUIC_PROTOCOL_ERROR;

  rv = session_->CryptoConnect(
      base::Bind(&QuicStreamFactory::Job::OnIOComplete, GetWeakPtr()));

  if (!session_->connection()->connected() &&
      session_->error() == QUIC_PROOF_INVALID) {
    return ERR_QUIC_HANDSHAKE_FAILED;
  }

  return rv;
}

int QuicStreamFactory::Job::DoConnectComplete(int rv) {
  net_log_.EndEvent(NetLogEventType::QUIC_STREAM_FACTORY_JOB_CONNECT);
  if (session_ && session_->error() == QUIC_CRYPTO_HANDSHAKE_STATELESS_REJECT) {
    num_sent_client_hellos_ += session_->GetNumSentClientHellos();
    if (num_sent_client_hellos_ >= QuicCryptoClientStream::kMaxClientHellos)
      return ERR_QUIC_HANDSHAKE_FAILED;
    // The handshake was rejected statelessly, so create another connection
    // to resume the handshake.
    io_state_ = STATE_CONNECT;
    return OK;
  }

  if (was_alternative_service_recently_broken_)
    UMA_HISTOGRAM_BOOLEAN("Net.QuicSession.ConnectAfterBroken", rv == OK);

  if (rv != OK)
    return rv;

  DCHECK(!factory_->HasActiveSession(key_.server_id()));
  // There may well now be an active session for this IP.  If so, use the
  // existing session instead.
  AddressList address(
      session_->connection()->peer_address().impl().socket_address());
  if (factory_->OnResolution(key_, address)) {
    session_->connection()->CloseConnection(
        QUIC_CONNECTION_IP_POOLED, "An active session exists for the given IP.",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    session_ = nullptr;
    return OK;
  }

  factory_->ActivateSession(key_, session_);

  return OK;
}

QuicStreamRequest::QuicStreamRequest(QuicStreamFactory* factory)
    : factory_(factory) {}

QuicStreamRequest::~QuicStreamRequest() {
  if (factory_ && !callback_.is_null())
    factory_->CancelRequest(this);
}

int QuicStreamRequest::Request(const HostPortPair& destination,
                               QuicTransportVersion quic_version,
                               PrivacyMode privacy_mode,
                               int cert_verify_flags,
                               const GURL& url,
                               QuicStringPiece method,
                               const NetLogWithSource& net_log,
                               NetErrorDetails* net_error_details,
                               const CompletionCallback& callback) {
  DCHECK_NE(quic_version, QUIC_VERSION_UNSUPPORTED);
  DCHECK(net_error_details);
  DCHECK(callback_.is_null());
  DCHECK(factory_);

  net_error_details_ = net_error_details;
  server_id_ = QuicServerId(HostPortPair::FromURL(url), privacy_mode);

  int rv = factory_->Create(server_id_, destination, quic_version,
                            cert_verify_flags, url, method, net_log, this);
  if (rv == ERR_IO_PENDING) {
    net_log_ = net_log;
    callback_ = callback;
  } else {
    factory_ = nullptr;
  }
  if (rv == OK)
    DCHECK(session_);
  return rv;
}

void QuicStreamRequest::SetSession(
    std::unique_ptr<QuicChromiumClientSession::Handle> session) {
  session_ = move(session);
}

void QuicStreamRequest::OnRequestComplete(int rv) {
  factory_ = nullptr;
  base::ResetAndReturn(&callback_).Run(rv);
}

base::TimeDelta QuicStreamRequest::GetTimeDelayForWaitingJob() const {
  if (!factory_)
    return base::TimeDelta();
  return factory_->GetTimeDelayForWaitingJob(server_id_);
}

std::unique_ptr<HttpStream> QuicStreamRequest::CreateStream() {
  if (!session_ || !session_->IsConnected())
    return nullptr;

  return std::make_unique<QuicHttpStream>(std::move(session_));
}

std::unique_ptr<BidirectionalStreamImpl>
QuicStreamRequest::CreateBidirectionalStreamImpl() {
  if (!session_ || !session_->IsConnected())
    return nullptr;

  return std::make_unique<BidirectionalStreamQuicImpl>(std::move(session_));
}

QuicStreamFactory::QuicStreamFactory(
    NetLog* net_log,
    HostResolver* host_resolver,
    SSLConfigService* ssl_config_service,
    ClientSocketFactory* client_socket_factory,
    HttpServerProperties* http_server_properties,
    CertVerifier* cert_verifier,
    CTPolicyEnforcer* ct_policy_enforcer,
    ChannelIDService* channel_id_service,
    TransportSecurityState* transport_security_state,
    CTVerifier* cert_transparency_verifier,
    SocketPerformanceWatcherFactory* socket_performance_watcher_factory,
    QuicCryptoClientStreamFactory* quic_crypto_client_stream_factory,
    QuicRandom* random_generator,
    QuicClock* clock,
    size_t max_packet_length,
    const std::string& user_agent_id,
    bool store_server_configs_in_properties,
    bool mark_quic_broken_when_network_blackholes,
    int idle_connection_timeout_seconds,
    int reduced_ping_timeout_seconds,
    bool connect_using_default_network,
    bool migrate_sessions_on_network_change,
    bool migrate_sessions_early,
    bool allow_server_migration,
    bool race_cert_verification,
    bool estimate_initial_rtt,
    const QuicTagVector& connection_options,
    const QuicTagVector& client_connection_options,
    bool enable_token_binding)
    : require_confirmation_(true),
      net_log_(net_log),
      host_resolver_(host_resolver),
      client_socket_factory_(client_socket_factory),
      http_server_properties_(http_server_properties),
      push_delegate_(nullptr),
      transport_security_state_(transport_security_state),
      cert_transparency_verifier_(cert_transparency_verifier),
      quic_crypto_client_stream_factory_(quic_crypto_client_stream_factory),
      random_generator_(random_generator),
      clock_(clock),
      max_packet_length_(max_packet_length),
      clock_skew_detector_(base::TimeTicks::Now(), base::Time::Now()),
      socket_performance_watcher_factory_(socket_performance_watcher_factory),
      config_(InitializeQuicConfig(connection_options,
                                   client_connection_options,
                                   idle_connection_timeout_seconds)),
      crypto_config_(
          std::make_unique<ProofVerifierChromium>(cert_verifier,
                                                  ct_policy_enforcer,
                                                  transport_security_state,
                                                  cert_transparency_verifier)),
      mark_quic_broken_when_network_blackholes_(
          mark_quic_broken_when_network_blackholes),
      store_server_configs_in_properties_(store_server_configs_in_properties),
      ping_timeout_(QuicTime::Delta::FromSeconds(kPingTimeoutSecs)),
      reduced_ping_timeout_(
          QuicTime::Delta::FromSeconds(reduced_ping_timeout_seconds)),
      most_recent_path_degrading_timestamp_(base::TimeTicks()),
      most_recent_network_disconnected_timestamp_(base::TimeTicks()),
      most_recent_write_error_(0),
      most_recent_write_error_timestamp_(base::TimeTicks()),
      yield_after_packets_(kQuicYieldAfterPacketsRead),
      yield_after_duration_(QuicTime::Delta::FromMilliseconds(
          kQuicYieldAfterDurationMilliseconds)),
      connect_using_default_network_(
          connect_using_default_network &&
          NetworkChangeNotifier::AreNetworkHandlesSupported()),
      migrate_sessions_on_network_change_(migrate_sessions_on_network_change),
      migrate_sessions_early_(migrate_sessions_early &&
                              migrate_sessions_on_network_change_),
      allow_server_migration_(allow_server_migration),
      race_cert_verification_(race_cert_verification),
      estimate_initial_rtt(estimate_initial_rtt),
      need_to_check_persisted_supports_quic_(true),
      num_push_streams_created_(0),
      task_runner_(nullptr),
      ssl_config_service_(ssl_config_service),
      weak_factory_(this) {
  if (ssl_config_service_.get())
    ssl_config_service_->AddObserver(this);
  DCHECK(transport_security_state_);
  DCHECK(http_server_properties_);
  crypto_config_.set_user_agent_id(user_agent_id);
  crypto_config_.AddCanonicalSuffix(".c.youtube.com");
  crypto_config_.AddCanonicalSuffix(".ggpht.com");
  crypto_config_.AddCanonicalSuffix(".googlevideo.com");
  crypto_config_.AddCanonicalSuffix(".googleusercontent.com");
  // TODO(rtenneti): http://crbug.com/487355. Temporary fix for b/20760730 until
  // channel_id_service is supported in cronet.
  if (channel_id_service) {
    crypto_config_.SetChannelIDSource(
        new ChannelIDSourceChromium(channel_id_service));
  }
  if (enable_token_binding && channel_id_service)
    crypto_config_.tb_key_params.push_back(kTB10);
  crypto::EnsureOpenSSLInit();
  bool has_aes_hardware_support = !!EVP_has_aes_hardware();
  UMA_HISTOGRAM_BOOLEAN("Net.QuicSession.PreferAesGcm",
                        has_aes_hardware_support);
  if (has_aes_hardware_support)
    crypto_config_.PreferAesGcm();

  // migrate_sessions_early should only be set to true if
  // migrate_sessions_on_network_change is set to true.
  if (migrate_sessions_early)
    DCHECK(migrate_sessions_on_network_change);

  NetworkChangeNotifier::AddIPAddressObserver(this);
  if (NetworkChangeNotifier::AreNetworkHandlesSupported())
    NetworkChangeNotifier::AddNetworkObserver(this);
}

QuicStreamFactory::~QuicStreamFactory() {
  CloseAllSessions(ERR_ABORTED, QUIC_CONNECTION_CANCELLED);
  while (!all_sessions_.empty()) {
    delete all_sessions_.begin()->first;
    all_sessions_.erase(all_sessions_.begin());
  }
  active_jobs_.clear();
  while (!active_cert_verifier_jobs_.empty())
    active_cert_verifier_jobs_.erase(active_cert_verifier_jobs_.begin());
  if (ssl_config_service_.get())
    ssl_config_service_->RemoveObserver(this);
  NetworkChangeNotifier::RemoveIPAddressObserver(this);
  if (NetworkChangeNotifier::AreNetworkHandlesSupported())
    NetworkChangeNotifier::RemoveNetworkObserver(this);
}

void QuicStreamFactory::set_require_confirmation(bool require_confirmation) {
  require_confirmation_ = require_confirmation;
  if (!(local_address_ == IPEndPoint())) {
    http_server_properties_->SetSupportsQuic(!require_confirmation,
                                             local_address_.address());
  }
}

base::TimeDelta QuicStreamFactory::GetTimeDelayForWaitingJob(
    const QuicServerId& server_id) {
  if (require_confirmation_) {
    IPAddress last_address;
    if (!need_to_check_persisted_supports_quic_ ||
        !http_server_properties_->GetSupportsQuic(&last_address)) {
      return base::TimeDelta();
    }
  }

  int64_t srtt =
      1.5 * GetServerNetworkStatsSmoothedRttInMicroseconds(server_id);
  // Picked 300ms based on mean time from
  // Net.QuicSession.HostResolution.HandshakeConfirmedTime histogram.
  const int kDefaultRTT = 300 * kNumMicrosPerMilli;
  if (!srtt)
    srtt = kDefaultRTT;
  return base::TimeDelta::FromMicroseconds(srtt);
}

void QuicStreamFactory::DumpMemoryStats(
    base::trace_event::ProcessMemoryDump* pmd,
    const std::string& parent_absolute_name) const {
  if (all_sessions_.empty() && active_jobs_.empty())
    return;
  base::trace_event::MemoryAllocatorDump* factory_dump =
      pmd->CreateAllocatorDump(parent_absolute_name + "/quic_stream_factory");
  size_t memory_estimate =
      base::trace_event::EstimateMemoryUsage(all_sessions_) +
      base::trace_event::EstimateMemoryUsage(active_sessions_) +
      base::trace_event::EstimateMemoryUsage(session_aliases_) +
      base::trace_event::EstimateMemoryUsage(ip_aliases_) +
      base::trace_event::EstimateMemoryUsage(session_peer_ip_) +
      base::trace_event::EstimateMemoryUsage(gone_away_aliases_) +
      base::trace_event::EstimateMemoryUsage(active_jobs_) +
      base::trace_event::EstimateMemoryUsage(active_cert_verifier_jobs_);
  factory_dump->AddScalar(base::trace_event::MemoryAllocatorDump::kNameSize,
                          base::trace_event::MemoryAllocatorDump::kUnitsBytes,
                          memory_estimate);
  factory_dump->AddScalar("all_sessions",
                          base::trace_event::MemoryAllocatorDump::kUnitsObjects,
                          all_sessions_.size());
  factory_dump->AddScalar("active_jobs",
                          base::trace_event::MemoryAllocatorDump::kUnitsObjects,
                          active_jobs_.size());
  factory_dump->AddScalar("active_cert_jobs",
                          base::trace_event::MemoryAllocatorDump::kUnitsObjects,
                          active_cert_verifier_jobs_.size());
}

bool QuicStreamFactory::CanUseExistingSession(const QuicServerId& server_id,
                                              const HostPortPair& destination) {
  // TODO(zhongyi): delete active_sessions_.empty() checks once the
  // android crash issue(crbug.com/498823) is resolved.
  if (active_sessions_.empty())
    return false;

  if (base::ContainsKey(active_sessions_, server_id))
    return true;

  for (const auto& key_value : active_sessions_) {
    QuicChromiumClientSession* session = key_value.second;
    if (destination.Equals(all_sessions_[session].destination()) &&
        session->CanPool(server_id.host(), server_id.privacy_mode())) {
      return true;
    }
  }

  return false;
}

int QuicStreamFactory::Create(const QuicServerId& server_id,
                              const HostPortPair& destination,
                              QuicTransportVersion quic_version,
                              int cert_verify_flags,
                              const GURL& url,
                              QuicStringPiece method,
                              const NetLogWithSource& net_log,
                              QuicStreamRequest* request) {
  if (clock_skew_detector_.ClockSkewDetected(base::TimeTicks::Now(),
                                             base::Time::Now())) {
    while (!active_sessions_.empty()) {
      QuicChromiumClientSession* session = active_sessions_.begin()->second;
      OnSessionGoingAway(session);
      // TODO(rch): actually close the session?
    }
  }
  DCHECK(server_id.host_port_pair().Equals(HostPortPair::FromURL(url)));
  // Enforce session affinity for promised streams.
  QuicClientPromisedInfo* promised =
      push_promise_index_.GetPromised(url.spec());
  if (promised) {
    QuicChromiumClientSession* session =
        static_cast<QuicChromiumClientSession*>(promised->session());
    DCHECK(session);
    if (session->server_id().privacy_mode() == server_id.privacy_mode()) {
      request->SetSession(session->CreateHandle());
      ++num_push_streams_created_;
      return OK;
    }
    // This should happen extremely rarely (if ever), but if somehow a
    // request comes in with a mismatched privacy mode, consider the
    // promise borked.
    promised->Cancel();
  }

  // Use active session for |server_id| if such exists.
  // TODO(rtenneti): crbug.com/498823 - delete active_sessions_.empty() checks.
  if (!active_sessions_.empty()) {
    SessionMap::iterator it = active_sessions_.find(server_id);
    if (it != active_sessions_.end()) {
      QuicChromiumClientSession* session = it->second;
      request->SetSession(session->CreateHandle());
      return OK;
    }
  }

  // Associate with active job to |server_id| if such exists.
  auto it = active_jobs_.find(server_id);
  if (it != active_jobs_.end()) {
    const NetLogWithSource& job_net_log = it->second->net_log();
    job_net_log.AddEvent(
        NetLogEventType::QUIC_STREAM_FACTORY_JOB_BOUND_TO_HTTP_STREAM_JOB,
        net_log.source().ToEventParametersCallback());
    net_log.AddEvent(
        NetLogEventType::HTTP_STREAM_JOB_BOUND_TO_QUIC_STREAM_FACTORY_JOB,
        job_net_log.source().ToEventParametersCallback());
    it->second->AddRequest(request);
    return ERR_IO_PENDING;
  }

  // Pool to active session to |destination| if possible.
  if (!active_sessions_.empty()) {
    for (const auto& key_value : active_sessions_) {
      QuicChromiumClientSession* session = key_value.second;
      if (destination.Equals(all_sessions_[session].destination()) &&
          session->CanPool(server_id.host(), server_id.privacy_mode())) {
        request->SetSession(session->CreateHandle());
        return OK;
      }
    }
  }

  // TODO(rtenneti): |task_runner_| is used by the Job. Initialize task_runner_
  // in the constructor after WebRequestActionWithThreadsTest.* tests are fixed.
  if (!task_runner_)
    task_runner_ = base::ThreadTaskRunnerHandle::Get().get();

  ignore_result(StartCertVerifyJob(server_id, cert_verify_flags, net_log));

  QuicSessionKey key(destination, server_id);
  std::unique_ptr<Job> job = std::make_unique<Job>(
      this, quic_version, host_resolver_, key, WasQuicRecentlyBroken(server_id),
      cert_verify_flags, net_log);
  int rv = job->Run(base::Bind(&QuicStreamFactory::OnJobComplete,
                               base::Unretained(this), job.get()));
  if (rv == ERR_IO_PENDING) {
    job->AddRequest(request);
    active_jobs_[server_id] = std::move(job);
    return rv;
  }
  if (rv == OK) {
    // TODO(rtenneti): crbug.com/498823 - revert active_sessions_.empty()
    // related changes.
    if (active_sessions_.empty())
      return ERR_QUIC_PROTOCOL_ERROR;
    SessionMap::iterator it = active_sessions_.find(server_id);
    DCHECK(it != active_sessions_.end());
    if (it == active_sessions_.end())
      return ERR_QUIC_PROTOCOL_ERROR;
    QuicChromiumClientSession* session = it->second;
    request->SetSession(session->CreateHandle());
  }
  return rv;
}

QuicStreamFactory::QuicSessionKey::QuicSessionKey(
    const HostPortPair& destination,
    const QuicServerId& server_id)
    : destination_(destination), server_id_(server_id) {}

bool QuicStreamFactory::QuicSessionKey::operator<(
    const QuicSessionKey& other) const {
  return std::tie(destination_, server_id_) <
         std::tie(other.destination_, other.server_id_);
}

bool QuicStreamFactory::QuicSessionKey::operator==(
    const QuicSessionKey& other) const {
  return destination_.Equals(other.destination_) &&
         server_id_ == other.server_id_;
}

size_t QuicStreamFactory::QuicSessionKey::EstimateMemoryUsage() const {
  return base::trace_event::EstimateMemoryUsage(destination_) +
         EstimateServerIdMemoryUsage(server_id_);
}

bool QuicStreamFactory::OnResolution(const QuicSessionKey& key,
                                     const AddressList& address_list) {
  const QuicServerId& server_id(key.server_id());
  DCHECK(!HasActiveSession(server_id));
  for (const IPEndPoint& address : address_list) {
    if (!base::ContainsKey(ip_aliases_, address))
      continue;

    const SessionSet& sessions = ip_aliases_[address];
    for (QuicChromiumClientSession* session : sessions) {
      if (!session->CanPool(server_id.host(), server_id.privacy_mode()))
        continue;
      active_sessions_[server_id] = session;
      session_aliases_[session].insert(key);
      return true;
    }
  }
  return false;
}

void QuicStreamFactory::OnJobComplete(Job* job, int rv) {
  auto iter = active_jobs_.find(job->key().server_id());
  // TODO(xunjieli): Change following CHECKs back to DCHECKs after
  // crbug.com/750271 is fixed.
  CHECK(iter != active_jobs_.end());
  if (rv == OK) {
    set_require_confirmation(false);

    SessionMap::iterator session_it =
        active_sessions_.find(job->key().server_id());
    CHECK(session_it != active_sessions_.end());
    QuicChromiumClientSession* session = session_it->second;
    for (auto* request : iter->second->stream_requests()) {
      CHECK(request->server_id() == job->key().server_id());
      // Do not notify |request| yet.
      request->SetSession(session->CreateHandle());
    }
  }

  for (auto* request : iter->second->stream_requests()) {
    // Even though we're invoking callbacks here, we don't need to worry
    // about |this| being deleted, because the factory is owned by the
    // profile which can not be deleted via callbacks.
    if (rv < 0) {
      job->PopulateNetErrorDetails(request->net_error_details());
    }
    request->OnRequestComplete(rv);
  }
  active_jobs_.erase(iter);
}

void QuicStreamFactory::OnCertVerifyJobComplete(CertVerifierJob* job, int rv) {
  active_cert_verifier_jobs_.erase(job->server_id());
}

bool QuicStreamFactory::IsQuicBroken(QuicChromiumClientSession* session) {
  const AlternativeService alternative_service(
      kProtoQUIC, session->server_id().host_port_pair());
  if (!http_server_properties_->IsAlternativeServiceBroken(
          alternative_service)) {
    return false;
  }
  // No longer send requests to a server for which QUIC is broken, but
  // continue to service existing requests.
  OnSessionGoingAway(session);
  return true;
}

void QuicStreamFactory::OnIdleSession(QuicChromiumClientSession* session) {}

void QuicStreamFactory::OnSessionGoingAway(QuicChromiumClientSession* session) {
  const AliasSet& aliases = session_aliases_[session];
  for (AliasSet::const_iterator it = aliases.begin(); it != aliases.end();
       ++it) {
    const QuicServerId& server_id = it->server_id();
    DCHECK(active_sessions_.count(server_id));
    DCHECK_EQ(session, active_sessions_[server_id]);
    // Track sessions which have recently gone away so that we can disable
    // port suggestions.
    if (session->goaway_received())
      gone_away_aliases_.insert(*it);

    active_sessions_.erase(server_id);
    ProcessGoingAwaySession(session, server_id, true);
  }
  ProcessGoingAwaySession(session, all_sessions_[session].server_id(), false);
  if (!aliases.empty()) {
    DCHECK(base::ContainsKey(session_peer_ip_, session));
    const IPEndPoint peer_address = session_peer_ip_[session];
    ip_aliases_[peer_address].erase(session);
    if (ip_aliases_[peer_address].empty())
      ip_aliases_.erase(peer_address);
    session_peer_ip_.erase(session);
  }
  session_aliases_.erase(session);
}

void QuicStreamFactory::OnSessionClosed(QuicChromiumClientSession* session) {
  DCHECK_EQ(0u, session->GetNumActiveStreams());
  OnSessionGoingAway(session);
  delete session;
  all_sessions_.erase(session);
}

void QuicStreamFactory::OnBlackholeAfterHandshakeConfirmed(
    QuicChromiumClientSession* session) {
  // Reduce PING timeout when connection blackholes after the handshake.
  if (ping_timeout_ > reduced_ping_timeout_)
    ping_timeout_ = reduced_ping_timeout_;

  if (mark_quic_broken_when_network_blackholes_) {
    http_server_properties_->MarkAlternativeServiceBroken(
        AlternativeService(kProtoQUIC, session->server_id().host_port_pair()));
  }
}

void QuicStreamFactory::CancelRequest(QuicStreamRequest* request) {
  auto job_iter = active_jobs_.find(request->server_id());
  CHECK(job_iter != active_jobs_.end());
  job_iter->second->RemoveRequest(request);
}

void QuicStreamFactory::CloseAllSessions(int error, QuicErrorCode quic_error) {
  UMA_HISTOGRAM_SPARSE_SLOWLY("Net.QuicSession.CloseAllSessionsError", -error);
  while (!active_sessions_.empty()) {
    size_t initial_size = active_sessions_.size();
    active_sessions_.begin()->second->CloseSessionOnError(error, quic_error);
    DCHECK_NE(initial_size, active_sessions_.size());
  }
  while (!all_sessions_.empty()) {
    size_t initial_size = all_sessions_.size();
    all_sessions_.begin()->first->CloseSessionOnError(error, quic_error);
    DCHECK_NE(initial_size, all_sessions_.size());
  }
  DCHECK(all_sessions_.empty());
}

std::unique_ptr<base::Value> QuicStreamFactory::QuicStreamFactoryInfoToValue()
    const {
  std::unique_ptr<base::ListValue> list(new base::ListValue());

  for (SessionMap::const_iterator it = active_sessions_.begin();
       it != active_sessions_.end(); ++it) {
    const QuicServerId& server_id = it->first;
    QuicChromiumClientSession* session = it->second;
    const AliasSet& aliases = session_aliases_.find(session)->second;
    // Only add a session to the list once.
    if (server_id == aliases.begin()->server_id()) {
      std::set<HostPortPair> hosts;
      for (AliasSet::const_iterator alias_it = aliases.begin();
           alias_it != aliases.end(); ++alias_it) {
        hosts.insert(alias_it->server_id().host_port_pair());
      }
      list->Append(session->GetInfoAsValue(hosts));
    }
  }
  return std::move(list);
}

void QuicStreamFactory::ClearCachedStatesInCryptoConfig(
    const base::Callback<bool(const GURL&)>& origin_filter) {
  ServerIdOriginFilter filter(origin_filter);
  crypto_config_.ClearCachedStates(filter);
}

void QuicStreamFactory::OnIPAddressChanged() {
  LogPlatformNotificationInHistogram(NETWORK_IP_ADDRESS_CHANGED);
  // Do nothing if connection migration is in use.
  if (migrate_sessions_on_network_change_)
    return;
  CloseAllSessions(ERR_NETWORK_CHANGED, QUIC_IP_ADDRESS_CHANGED);
  set_require_confirmation(true);
}

void QuicStreamFactory::OnNetworkConnected(NetworkHandle network) {
  LogPlatformNotificationInHistogram(NETWORK_CONNECTED);
  if (!migrate_sessions_on_network_change_)
    return;
  ScopedConnectionMigrationEventLog scoped_event_log(net_log_,
                                                     "OnNetworkConnected");
  QuicStreamFactory::SessionIdMap::iterator it = all_sessions_.begin();
  // Sessions may be deleted while iterating through the map.
  while (it != all_sessions_.end()) {
    QuicChromiumClientSession* session = it->first;
    ++it;
    session->OnNetworkConnected(network, scoped_event_log.net_log());
  }
}

void QuicStreamFactory::OnNetworkMadeDefault(NetworkHandle network) {
  LogPlatformNotificationInHistogram(NETWORK_MADE_DEFAULT);
  if (most_recent_path_degrading_timestamp_ != base::TimeTicks()) {
    if (most_recent_network_disconnected_timestamp_ != base::TimeTicks()) {
      // NetworkDiscconected happens before NetworkMadeDefault, the platform
      // is dropping WiFi.
      base::TimeTicks now = base::TimeTicks::Now();
      base::TimeDelta disconnection_duration =
          now - most_recent_network_disconnected_timestamp_;
      base::TimeDelta degrading_duration =
          now - most_recent_path_degrading_timestamp_;
      UMA_HISTOGRAM_CUSTOM_TIMES("Net.QuicNetworkDisconnectionDuration",
                                 disconnection_duration,
                                 base::TimeDelta::FromMilliseconds(1),
                                 base::TimeDelta::FromMinutes(10), 100);
      UMA_HISTOGRAM_CUSTOM_TIMES(
          "Net.QuicNetworkDegradingDurationTillNewNetworkMadeDefault",
          degrading_duration, base::TimeDelta::FromMilliseconds(1),
          base::TimeDelta::FromMinutes(10), 100);
      most_recent_network_disconnected_timestamp_ = base::TimeTicks();
    }
    most_recent_path_degrading_timestamp_ = base::TimeTicks();
  }

  if (!migrate_sessions_on_network_change_)
    return;
  DCHECK_NE(NetworkChangeNotifier::kInvalidNetworkHandle, network);
  ScopedConnectionMigrationEventLog scoped_event_log(net_log_,
                                                     "OnNetworkMadeDefault");
  MaybeMigrateOrCloseSessions(network, /*close_if_cannot_migrate=*/false,
                              scoped_event_log.net_log());
  set_require_confirmation(true);
}

void QuicStreamFactory::OnNetworkDisconnected(NetworkHandle network) {
  LogPlatformNotificationInHistogram(NETWORK_DISCONNECTED);
  if (most_recent_path_degrading_timestamp_ != base::TimeTicks()) {
    most_recent_network_disconnected_timestamp_ = base::TimeTicks::Now();
    base::TimeDelta degrading_duration =
        most_recent_network_disconnected_timestamp_ -
        most_recent_path_degrading_timestamp_;
    UMA_HISTOGRAM_CUSTOM_TIMES(
        "Net.QuicNetworkDegradingDurationTillDisconnected", degrading_duration,
        base::TimeDelta::FromMilliseconds(1), base::TimeDelta::FromMinutes(10),
        100);
  }
  if (most_recent_write_error_timestamp_ != base::TimeTicks()) {
    base::TimeDelta write_error_to_disconnection_gap =
        most_recent_network_disconnected_timestamp_ -
        most_recent_write_error_timestamp_;
    UMA_HISTOGRAM_CUSTOM_TIMES(
        "Net.QuicNetworkGapBetweenWriteErrorAndDisconnection",
        write_error_to_disconnection_gap, base::TimeDelta::FromMilliseconds(1),
        base::TimeDelta::FromMinutes(10), 100);
    UMA_HISTOGRAM_SPARSE_SLOWLY(
        "Net.QuicSession.WriteError.NetworkDisconnected",
        -most_recent_write_error_);
    most_recent_write_error_ = 0;
    most_recent_write_error_timestamp_ = base::TimeTicks();
  }

  if (!migrate_sessions_on_network_change_)
    return;
  ScopedConnectionMigrationEventLog scoped_event_log(net_log_,
                                                     "OnNetworkDisconnected");
  NetworkHandle new_network = FindAlternateNetwork(network);
  MaybeMigrateOrCloseSessions(new_network, /*close_if_cannot_migrate=*/true,
                              scoped_event_log.net_log());
}

// This method is expected to only be called when migrating from Cellular to
// WiFi on Android, and should always be preceded by OnNetworkMadeDefault().
void QuicStreamFactory::OnNetworkSoonToDisconnect(NetworkHandle network) {
  LogPlatformNotificationInHistogram(NETWORK_SOON_TO_DISCONNECT);
}

NetworkHandle QuicStreamFactory::FindAlternateNetwork(
    NetworkHandle old_network) {
  // Find a new network that sessions bound to |old_network| can be migrated to.
  NetworkChangeNotifier::NetworkList network_list;
  NetworkChangeNotifier::GetConnectedNetworks(&network_list);
  for (NetworkHandle new_network : network_list) {
    if (new_network != old_network)
      return new_network;
  }
  return NetworkChangeNotifier::kInvalidNetworkHandle;
}

void QuicStreamFactory::MaybeMigrateOrCloseSessions(
    NetworkHandle new_network,
    bool close_if_cannot_migrate,
    const NetLogWithSource& net_log) {
  QuicStreamFactory::SessionIdMap::iterator it = all_sessions_.begin();
  while (it != all_sessions_.end()) {
    QuicChromiumClientSession* session = it->first;
    ++it;

    // If session is already bound to |new_network|, move on.
    if (session->GetDefaultSocket()->GetBoundNetwork() == new_network) {
      HistogramAndLogMigrationFailure(
          net_log, MIGRATION_STATUS_ALREADY_MIGRATED, session->connection_id(),
          "Already bound to new network");
      continue;
    }

    // Close idle sessions.
    if (session->GetNumActiveStreams() == 0) {
      HistogramAndLogMigrationFailure(
          net_log, MIGRATION_STATUS_NO_MIGRATABLE_STREAMS,
          session->connection_id(), "No active sessions");
      session->CloseSessionOnError(
          ERR_NETWORK_CHANGED, QUIC_CONNECTION_MIGRATION_NO_MIGRATABLE_STREAMS);
      continue;
    }

    // If session has active streams, mark it as going away.
    OnSessionGoingAway(session);

    // Do not migrate sessions where connection migration is disabled.
    if (session->config()->DisableConnectionMigration()) {
      HistogramAndLogMigrationFailure(net_log, MIGRATION_STATUS_DISABLED,
                                      session->connection_id(),
                                      "Migration disabled");
      if (close_if_cannot_migrate) {
        session->CloseSessionOnError(ERR_NETWORK_CHANGED,
                                     QUIC_IP_ADDRESS_CHANGED);
      }
      continue;
    }

    // Do not migrate sessions with non-migratable streams.
    if (session->HasNonMigratableStreams()) {
      HistogramAndLogMigrationFailure(
          net_log, MIGRATION_STATUS_NON_MIGRATABLE_STREAM,
          session->connection_id(), "Non-migratable stream");
      if (close_if_cannot_migrate) {
        session->CloseSessionOnError(
            ERR_NETWORK_CHANGED,
            QUIC_CONNECTION_MIGRATION_NON_MIGRATABLE_STREAM);
      }
      continue;
    }

    // No new network was found. Notify session, so it can wait for a
    // new network.
    if (new_network == NetworkChangeNotifier::kInvalidNetworkHandle) {
      session->OnNoNewNetwork();
      continue;
    }

    MigrateSessionToNewNetwork(session, new_network,
                               /*close_session_on_error=*/true, net_log);
  }
}

MigrationResult QuicStreamFactory::MaybeMigrateSingleSessionOnWriteError(
    QuicChromiumClientSession* session,
    int error_code) {
  most_recent_write_error_timestamp_ = base::TimeTicks::Now();
  most_recent_write_error_ = error_code;

  const NetLogWithSource migration_net_log = NetLogWithSource::Make(
      net_log_, NetLogSourceType::QUIC_CONNECTION_MIGRATION);
  migration_net_log.BeginEvent(
      NetLogEventType::QUIC_CONNECTION_MIGRATION_TRIGGERED,
      base::Bind(&NetLogQuicConnectionMigrationTriggerCallback, "WriteError"));

  MigrationResult result =
      MaybeMigrateSingleSession(session, false, migration_net_log);
  migration_net_log.EndEvent(
      NetLogEventType::QUIC_CONNECTION_MIGRATION_TRIGGERED);
  return result;
}

MigrationResult QuicStreamFactory::MaybeMigrateSingleSessionOnPathDegrading(
    QuicChromiumClientSession* session) {
  if (most_recent_path_degrading_timestamp_ == base::TimeTicks())
    most_recent_path_degrading_timestamp_ = base::TimeTicks::Now();

  const NetLogWithSource migration_net_log = NetLogWithSource::Make(
      net_log_, NetLogSourceType::QUIC_CONNECTION_MIGRATION);
  migration_net_log.BeginEvent(
      NetLogEventType::QUIC_CONNECTION_MIGRATION_TRIGGERED,
      base::Bind(&NetLogQuicConnectionMigrationTriggerCallback,
                 "PathDegrading"));

  MigrationResult result = MigrationResult::FAILURE;
  if (migrate_sessions_early_) {
    result = MaybeMigrateSingleSession(session, true, migration_net_log);
  } else {
    HistogramAndLogMigrationFailure(
        migration_net_log, MIGRATION_STATUS_DISABLED, session->connection_id(),
        "Migration disabled");
  }
  migration_net_log.EndEvent(
      NetLogEventType::QUIC_CONNECTION_MIGRATION_TRIGGERED);
  return result;
}

MigrationResult QuicStreamFactory::MaybeMigrateSingleSession(
    QuicChromiumClientSession* session,
    bool close_session_on_error,
    const NetLogWithSource& net_log) {
  if (!migrate_sessions_on_network_change_ ||
      session->HasNonMigratableStreams() ||
      session->config()->DisableConnectionMigration()) {
    HistogramAndLogMigrationFailure(net_log, MIGRATION_STATUS_DISABLED,
                                    session->connection_id(),
                                    "Migration disabled");
    return MigrationResult::FAILURE;
  }
  NetworkHandle new_network =
      FindAlternateNetwork(session->GetDefaultSocket()->GetBoundNetwork());
  if (new_network == NetworkChangeNotifier::kInvalidNetworkHandle) {
    // No alternate network found.
    HistogramAndLogMigrationFailure(
        net_log, MIGRATION_STATUS_NO_ALTERNATE_NETWORK,
        session->connection_id(), "No alternate network found");
    return MigrationResult::NO_NEW_NETWORK;
  }
  OnSessionGoingAway(session);
  return MigrateSessionToNewNetwork(session, new_network,
                                    close_session_on_error, net_log);
}

void QuicStreamFactory::MigrateSessionToNewPeerAddress(
    QuicChromiumClientSession* session,
    IPEndPoint peer_address,
    const NetLogWithSource& net_log) {
  if (!allow_server_migration_)
    return;

  IPEndPoint old_address;
  session->GetDefaultSocket()->GetPeerAddress(&old_address);
  DCHECK_EQ(old_address.GetFamily(), peer_address.GetFamily());

  // Specifying kInvalidNetworkHandle for the |network| parameter
  // causes the session to use the default network for the new socket.
  MigrateSessionInner(session, peer_address,
                      NetworkChangeNotifier::kInvalidNetworkHandle,
                      /*close_session_on_error=*/true, net_log);
}

MigrationResult QuicStreamFactory::MigrateSessionToNewNetwork(
    QuicChromiumClientSession* session,
    NetworkHandle network,
    bool close_session_on_error,
    const NetLogWithSource& net_log) {
  return MigrateSessionInner(
      session, session->connection()->peer_address().impl().socket_address(),
      network, close_session_on_error, net_log);
}

MigrationResult QuicStreamFactory::MigrateSessionInner(
    QuicChromiumClientSession* session,
    IPEndPoint peer_address,
    NetworkHandle network,
    bool close_session_on_error,
    const NetLogWithSource& net_log) {
  // Use OS-specified port for socket (DEFAULT_BIND) instead of
  // using the PortSuggester since the connection is being migrated
  // and not being newly created.
  std::unique_ptr<DatagramClientSocket> socket(
      client_socket_factory_->CreateDatagramClientSocket(
          DatagramSocket::DEFAULT_BIND, RandIntCallback(),
          session->net_log().net_log(), session->net_log().source()));
  if (ConfigureSocket(socket.get(), peer_address, network) != OK) {
    HistogramAndLogMigrationFailure(net_log, MIGRATION_STATUS_INTERNAL_ERROR,
                                    session->connection_id(),
                                    "Socket configuration failed");
    if (close_session_on_error) {
      session->CloseSessionOnError(ERR_NETWORK_CHANGED, QUIC_INTERNAL_ERROR);
    }
    return MigrationResult::FAILURE;
  }
  std::unique_ptr<QuicChromiumPacketReader> new_reader(
      new QuicChromiumPacketReader(socket.get(), clock_, session,
                                   yield_after_packets_, yield_after_duration_,
                                   session->net_log()));
  std::unique_ptr<QuicChromiumPacketWriter> new_writer(
      new QuicChromiumPacketWriter(socket.get()));
  new_writer->set_delegate(session);

  if (!session->MigrateToSocket(std::move(socket), std::move(new_reader),
                                std::move(new_writer))) {
    HistogramAndLogMigrationFailure(net_log, MIGRATION_STATUS_TOO_MANY_CHANGES,
                                    session->connection_id(),
                                    "Too many migrations");
    if (close_session_on_error) {
      session->CloseSessionOnError(ERR_NETWORK_CHANGED,
                                   QUIC_CONNECTION_MIGRATION_TOO_MANY_CHANGES);
    }
    return MigrationResult::FAILURE;
  }
  HistogramMigrationStatus(MIGRATION_STATUS_SUCCESS);
  net_log.AddEvent(NetLogEventType::QUIC_CONNECTION_MIGRATION_SUCCESS,
                   base::Bind(&NetLogQuicConnectionMigrationSuccessCallback,
                              session->connection_id()));
  return MigrationResult::SUCCESS;
}

void QuicStreamFactory::OnSSLConfigChanged() {
  CloseAllSessions(ERR_CERT_DATABASE_CHANGED, QUIC_CONNECTION_CANCELLED);
}

void QuicStreamFactory::OnCertDBChanged() {
  // We should flush the sessions if we removed trust from a
  // cert, because a previously trusted server may have become
  // untrusted.
  //
  // We should not flush the sessions if we added trust to a cert.
  //
  // Since the OnCertDBChanged method doesn't tell us what
  // kind of change it is, we have to flush the socket
  // pools to be safe.
  CloseAllSessions(ERR_CERT_DATABASE_CHANGED, QUIC_CONNECTION_CANCELLED);
}

bool QuicStreamFactory::HasActiveSession(const QuicServerId& server_id) const {
  // TODO(rtenneti): crbug.com/498823 - delete active_sessions_.empty() check.
  if (active_sessions_.empty())
    return false;
  return base::ContainsKey(active_sessions_, server_id);
}

bool QuicStreamFactory::HasActiveJob(const QuicServerId& server_id) const {
  return base::ContainsKey(active_jobs_, server_id);
}

bool QuicStreamFactory::HasActiveCertVerifierJob(
    const QuicServerId& server_id) const {
  return base::ContainsKey(active_cert_verifier_jobs_, server_id);
}

int QuicStreamFactory::ConfigureSocket(DatagramClientSocket* socket,
                                       IPEndPoint addr,
                                       NetworkHandle network) {
  socket->UseNonBlockingIO();

  int rv;
  if (migrate_sessions_on_network_change_) {
    // If caller leaves network unspecified, use current default network.
    if (network == NetworkChangeNotifier::kInvalidNetworkHandle) {
      rv = socket->ConnectUsingDefaultNetwork(addr);
    } else {
      rv = socket->ConnectUsingNetwork(network, addr);
    }
  } else if (connect_using_default_network_) {
    rv = socket->ConnectUsingDefaultNetwork(addr);
  } else {
    rv = socket->Connect(addr);
  }
  if (rv != OK) {
    HistogramCreateSessionFailure(CREATION_ERROR_CONNECTING_SOCKET);
    return rv;
  }

  rv = socket->SetReceiveBufferSize(kQuicSocketReceiveBufferSize);
  if (rv != OK) {
    HistogramCreateSessionFailure(CREATION_ERROR_SETTING_RECEIVE_BUFFER);
    return rv;
  }

  rv = socket->SetDoNotFragment();
  // SetDoNotFragment is not implemented on all platforms, so ignore errors.
  if (rv != OK && rv != ERR_NOT_IMPLEMENTED) {
    HistogramCreateSessionFailure(CREATION_ERROR_SETTING_DO_NOT_FRAGMENT);
    return rv;
  }

  // Set a buffer large enough to contain the initial CWND's worth of packet
  // to work around the problem with CHLO packets being sent out with the
  // wrong encryption level, when the send buffer is full.
  rv = socket->SetSendBufferSize(kMaxPacketSize * 20);
  if (rv != OK) {
    HistogramCreateSessionFailure(CREATION_ERROR_SETTING_SEND_BUFFER);
    return rv;
  }

  socket->GetLocalAddress(&local_address_);
  if (need_to_check_persisted_supports_quic_) {
    need_to_check_persisted_supports_quic_ = false;
    IPAddress last_address;
    if (http_server_properties_->GetSupportsQuic(&last_address) &&
        last_address == local_address_.address()) {
      require_confirmation_ = false;
      // Clear the persisted IP address, in case the network no longer supports
      // QUIC so the next restart will require confirmation. It will be
      // re-persisted when the first job completes successfully.
      http_server_properties_->SetSupportsQuic(false, last_address);
    }
  }

  return OK;
}

int QuicStreamFactory::CreateSession(const QuicSessionKey& key,
                                     const QuicTransportVersion& quic_version,
                                     int cert_verify_flags,
                                     bool require_confirmation,
                                     const AddressList& address_list,
                                     base::TimeTicks dns_resolution_start_time,
                                     base::TimeTicks dns_resolution_end_time,
                                     const NetLogWithSource& net_log,
                                     QuicChromiumClientSession** session) {
  TRACE_EVENT0(kNetTracingCategory, "QuicStreamFactory::CreateSession");
  IPEndPoint addr = *address_list.begin();
  const QuicServerId& server_id = key.server_id();
  DatagramSocket::BindType bind_type = DatagramSocket::DEFAULT_BIND;
  std::unique_ptr<DatagramClientSocket> socket(
      client_socket_factory_->CreateDatagramClientSocket(
          bind_type, RandIntCallback(), net_log.net_log(), net_log.source()));

  // Passing in kInvalidNetworkHandle binds socket to default network.
  int rv = ConfigureSocket(socket.get(), addr,
                           NetworkChangeNotifier::kInvalidNetworkHandle);
  if (rv != OK)
    return rv;

  if (!helper_.get()) {
    helper_.reset(new QuicChromiumConnectionHelper(clock_, random_generator_));
  }

  if (!alarm_factory_.get()) {
    alarm_factory_.reset(new QuicChromiumAlarmFactory(
        base::ThreadTaskRunnerHandle::Get().get(), clock_));
  }

  QuicConnectionId connection_id = random_generator_->RandUint64();
  std::unique_ptr<QuicServerInfo> server_info;
  if (store_server_configs_in_properties_) {
    server_info = std::make_unique<PropertiesBasedQuicServerInfo>(
        server_id, http_server_properties_);
  }
  InitializeCachedStateInCryptoConfig(server_id, server_info, &connection_id);

  QuicChromiumPacketWriter* writer = new QuicChromiumPacketWriter(socket.get());
  QuicConnection* connection = new QuicConnection(
      connection_id, QuicSocketAddress(QuicSocketAddressImpl(addr)),
      helper_.get(), alarm_factory_.get(), writer, true /* owns_writer */,
      Perspective::IS_CLIENT, {quic_version});
  connection->set_ping_timeout(ping_timeout_);
  connection->SetMaxPacketLength(max_packet_length_);

  QuicConfig config = config_;
  config.set_max_undecryptable_packets(kMaxUndecryptablePackets);
  config.SetInitialSessionFlowControlWindowToSend(
      kQuicSessionMaxRecvWindowSize);
  config.SetInitialStreamFlowControlWindowToSend(kQuicStreamMaxRecvWindowSize);
  config.SetBytesForConnectionIdToSend(0);
  ConfigureInitialRttEstimate(server_id, &config);

  // Use the factory to create a new socket performance watcher, and pass the
  // ownership to QuicChromiumClientSession.
  std::unique_ptr<SocketPerformanceWatcher> socket_performance_watcher;
  if (socket_performance_watcher_factory_) {
    socket_performance_watcher =
        socket_performance_watcher_factory_->CreateSocketPerformanceWatcher(
            SocketPerformanceWatcherFactory::PROTOCOL_QUIC, address_list);
  }

  // Wait for handshake confirmation before allowing streams to be created if
  // either this session or the factory require confirmation.
  if (require_confirmation_)
    require_confirmation = true;

  *session = new QuicChromiumClientSession(
      connection, std::move(socket), this, quic_crypto_client_stream_factory_,
      clock_, transport_security_state_, std::move(server_info), server_id,
      require_confirmation, yield_after_packets_, yield_after_duration_,
      cert_verify_flags, config, &crypto_config_,
      network_connection_.connection_description(), dns_resolution_start_time,
      dns_resolution_end_time, &push_promise_index_, push_delegate_,
      task_runner_, std::move(socket_performance_watcher), net_log.net_log());

  all_sessions_[*session] = key;  // owning pointer
  writer->set_delegate(*session);

  (*session)->Initialize();
  bool closed_during_initialize = !base::ContainsKey(all_sessions_, *session) ||
                                  !(*session)->connection()->connected();
  UMA_HISTOGRAM_BOOLEAN("Net.QuicSession.ClosedDuringInitializeSession",
                        closed_during_initialize);
  if (closed_during_initialize) {
    DLOG(DFATAL) << "Session closed during initialize";
    *session = nullptr;
    return ERR_CONNECTION_CLOSED;
  }
  return OK;
}

void QuicStreamFactory::ActivateSession(const QuicSessionKey& key,
                                        QuicChromiumClientSession* session) {
  const QuicServerId& server_id(key.server_id());
  DCHECK(!HasActiveSession(server_id));
  UMA_HISTOGRAM_COUNTS_1M("Net.QuicActiveSessions", active_sessions_.size());
  active_sessions_[server_id] = session;
  session_aliases_[session].insert(key);
  const IPEndPoint peer_address =
      session->connection()->peer_address().impl().socket_address();
  DCHECK(!base::ContainsKey(ip_aliases_[peer_address], session));
  ip_aliases_[peer_address].insert(session);
  DCHECK(!base::ContainsKey(session_peer_ip_, session));
  session_peer_ip_[session] = peer_address;
}

void QuicStreamFactory::ConfigureInitialRttEstimate(
    const QuicServerId& server_id,
    QuicConfig* config) {
  const base::TimeDelta* srtt = GetServerNetworkStatsSmoothedRtt(server_id);
  if (srtt != nullptr) {
    SetInitialRttEstimate(*srtt, INITIAL_RTT_CACHED, config);
    return;
  }

  NetworkChangeNotifier::ConnectionType type =
      network_connection_.connection_type();
  if (type == NetworkChangeNotifier::CONNECTION_2G) {
    SetInitialRttEstimate(base::TimeDelta::FromMilliseconds(1200),
                          INITIAL_RTT_CACHED, config);
    return;
  }

  if (type == NetworkChangeNotifier::CONNECTION_3G) {
    SetInitialRttEstimate(base::TimeDelta::FromMilliseconds(400),
                          INITIAL_RTT_CACHED, config);
    return;
  }

  SetInitialRttEstimate(base::TimeDelta(), INITIAL_RTT_DEFAULT, config);
}

const base::TimeDelta* QuicStreamFactory::GetServerNetworkStatsSmoothedRtt(
    const QuicServerId& server_id) const {
  url::SchemeHostPort server("https", server_id.host_port_pair().host(),
                             server_id.host_port_pair().port());
  const ServerNetworkStats* stats =
      http_server_properties_->GetServerNetworkStats(server);
  if (stats == nullptr)
    return nullptr;
  return &(stats->srtt);
}

int64_t QuicStreamFactory::GetServerNetworkStatsSmoothedRttInMicroseconds(
    const QuicServerId& server_id) const {
  const base::TimeDelta* srtt = GetServerNetworkStatsSmoothedRtt(server_id);
  return srtt == nullptr ? 0 : srtt->InMicroseconds();
}

bool QuicStreamFactory::WasQuicRecentlyBroken(
    const QuicServerId& server_id) const {
  const AlternativeService alternative_service(kProtoQUIC,
                                               server_id.host_port_pair());
  return http_server_properties_->WasAlternativeServiceRecentlyBroken(
      alternative_service);
}

bool QuicStreamFactory::CryptoConfigCacheIsEmpty(
    const QuicServerId& server_id) {
  QuicCryptoClientConfig::CachedState* cached =
      crypto_config_.LookupOrCreate(server_id);
  return cached->IsEmpty();
}

QuicAsyncStatus QuicStreamFactory::StartCertVerifyJob(
    const QuicServerId& server_id,
    int cert_verify_flags,
    const NetLogWithSource& net_log) {
  if (!race_cert_verification_)
    return QUIC_FAILURE;
  QuicCryptoClientConfig::CachedState* cached =
      crypto_config_.LookupOrCreate(server_id);
  if (!cached || cached->certs().empty() ||
      HasActiveCertVerifierJob(server_id)) {
    return QUIC_FAILURE;
  }
  std::unique_ptr<CertVerifierJob> cert_verifier_job(
      new CertVerifierJob(server_id, cert_verify_flags, net_log));
  QuicAsyncStatus status = cert_verifier_job->Run(
      &crypto_config_,
      base::Bind(&QuicStreamFactory::OnCertVerifyJobComplete,
                 base::Unretained(this), cert_verifier_job.get()));
  if (status == QUIC_PENDING)
    active_cert_verifier_jobs_[server_id] = std::move(cert_verifier_job);
  return status;
}

void QuicStreamFactory::InitializeCachedStateInCryptoConfig(
    const QuicServerId& server_id,
    const std::unique_ptr<QuicServerInfo>& server_info,
    QuicConnectionId* connection_id) {
  QuicCryptoClientConfig::CachedState* cached =
      crypto_config_.LookupOrCreate(server_id);
  if (cached->has_server_designated_connection_id())
    *connection_id = cached->GetNextServerDesignatedConnectionId();

  if (!cached->IsEmpty())
    return;

  if (!server_info || !server_info->Load())
    return;

  cached->Initialize(server_info->state().server_config,
                     server_info->state().source_address_token,
                     server_info->state().certs, server_info->state().cert_sct,
                     server_info->state().chlo_hash,
                     server_info->state().server_config_sig, clock_->WallNow(),
                     QuicWallTime::Zero());
}

void QuicStreamFactory::ProcessGoingAwaySession(
    QuicChromiumClientSession* session,
    const QuicServerId& server_id,
    bool session_was_active) {
  if (!http_server_properties_)
    return;

  const QuicConnectionStats& stats = session->connection()->GetStats();
  const AlternativeService alternative_service(kProtoQUIC,
                                               server_id.host_port_pair());
  url::SchemeHostPort server("https", server_id.host_port_pair().host(),
                             server_id.host_port_pair().port());
  // Do nothing if QUIC is currently marked as broken.
  if (http_server_properties_->IsAlternativeServiceBroken(alternative_service))
    return;

  if (session->IsCryptoHandshakeConfirmed()) {
    http_server_properties_->ConfirmAlternativeService(alternative_service);
    ServerNetworkStats network_stats;
    network_stats.srtt = base::TimeDelta::FromMicroseconds(stats.srtt_us);
    network_stats.bandwidth_estimate = stats.estimated_bandwidth;
    http_server_properties_->SetServerNetworkStats(server, network_stats);
    return;
  }

  http_server_properties_->ClearServerNetworkStats(server);

  UMA_HISTOGRAM_COUNTS_1M("Net.QuicHandshakeNotConfirmedNumPacketsReceived",
                          stats.packets_received);

  if (!session_was_active)
    return;

  // TODO(rch):  In the special case where the session has received no
  // packets from the peer, we should consider blacklisting this
  // differently so that we still race TCP but we don't consider the
  // session connected until the handshake has been confirmed.
  HistogramBrokenAlternateProtocolLocation(
      BROKEN_ALTERNATE_PROTOCOL_LOCATION_QUIC_STREAM_FACTORY);

  // Since the session was active, there's no longer an
  // HttpStreamFactoryImpl::Job running which can mark it broken, unless the TCP
  // job also fails. So to avoid not using QUIC when we otherwise could, we mark
  // it as recently broken, which means that 0-RTT will be disabled but we'll
  // still race.
  http_server_properties_->MarkAlternativeServiceRecentlyBroken(
      alternative_service);
}

}  // namespace net
