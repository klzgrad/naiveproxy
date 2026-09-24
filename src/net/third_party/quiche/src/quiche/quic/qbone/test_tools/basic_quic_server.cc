// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/qbone/test_tools/basic_quic_server.h"

#include <array>
#include <cstddef>
#include <memory>
#include <queue>
#include <string>
#include <thread>  // NOLINT (for open-sourceable thread ID)
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/types/span.h"
#include "quiche/quic/core/connection_id_generator.h"
#include "quiche/quic/core/crypto/proof_source.h"
#include "quiche/quic/core/crypto/quic_crypto_server_config.h"
#include "quiche/quic/core/crypto/quic_random.h"
#include "quiche/quic/core/deterministic_connection_id_generator.h"
#include "quiche/quic/core/io/quic_default_event_loop.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/io/quic_server_io_harness.h"
#include "quiche/quic/core/io/socket.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_crypto_server_stream_base.h"
#include "quiche/quic/core/quic_default_clock.h"
#include "quiche/quic/core/quic_default_connection_helper.h"
#include "quiche/quic/core/quic_dispatcher.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_stream.h"
#include "quiche/quic/core/quic_stream_sequencer.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/core/quic_version_manager.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/quic_simple_crypto_server_stream_helper.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/platform/api/quiche_thread.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_mem_slice.h"
#include "quiche/common/quiche_socket_address.h"
#include "quiche/common/quiche_status_utils.h"

namespace quic::test {
namespace {

using ::quiche::QuicheMemSlice;
using ::quiche::QuicheSocketAddress;
using ::quiche::QuicheThread;
using ::quiche::SingleUseCallback;

constexpr auto kSupportedQuicVersions = std::to_array<ParsedQuicVersion>({
    ParsedQuicVersion::RFCv1(),
});

class Stream : public QuicStream {
 public:
  Stream(QuicStreamId id,
         QuicSession* absl_nonnull session ABSL_ATTRIBUTE_LIFETIME_BOUND,
         BasicQuicServer::Handler* absl_nonnull handler
             ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : QuicStream(id, session, /*is_static=*/false,
                   QuicUtils::GetStreamType(
                       id, session->connection()->perspective(),
                       session->IsIncomingStream(id), session->version())),
        handler_(handler) {
    // This stream allows leaving data unconsumed in the sequencer, so need to
    // use level-triggered reading to notify the handler on any increase in
    // readable bytes.
    sequencer()->set_level_triggered(true);
  }

  void OnDataAvailable() override {
    int consumed = handler_->OnStreamDataAvailable(session()->connection_id(),
                                                   id(), *sequencer());
    if (consumed > 0) {
      sequencer()->MarkConsumed(consumed);
    }
  }

 private:
  BasicQuicServer::Handler* absl_nonnull const handler_;
};

class Session : public QuicSession {
 public:
  class DestructionObserver {
   public:
    virtual ~DestructionObserver() = default;

    virtual void OnSessionDestroyed(Session* session) = 0;
  };

  Session(std::unique_ptr<QuicConnection> absl_nonnull connection,
          Visitor* visitor, const QuicConfig& config,
          const QuicCryptoServerConfig* crypto_config,
          BasicQuicServer::Handler* absl_nonnull handler
              ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : QuicSession(connection.get(), visitor, config,
                    connection->supported_versions(),
                    /*num_expected_unidirectional_static_streams=*/0),
        connection_(std::move(connection)),
        handler_(handler) {
    crypto_stream_ = handler_->OnNewSession(this, crypto_config);
    Initialize();
  }

  ~Session() override {
    handler_->OnSessionEnd(connection_id());

    if (destruction_observer_) {
      destruction_observer_->OnSessionDestroyed(this);
    }
  }

  void RegisterDestructionObserver(DestructionObserver* absl_nonnull observer) {
    QUICHE_CHECK(!destruction_observer_);
    destruction_observer_ = observer;
  }

  void UnregisterDestructionObserver() {
    QUICHE_CHECK(destruction_observer_);
    destruction_observer_ = nullptr;
  }

  // QuicSession:

  const QuicCryptoStream* GetCryptoStream() const override {
    return crypto_stream_.get();
  }

  std::vector<absl::string_view>::const_iterator SelectAlpn(
      const std::vector<absl::string_view>& alpns) const override {
    return handler_->SelectAlpn(connection_id(), alpns);
  }

 protected:
  QuicStream* CreateIncomingStream(QuicStreamId id) override {
    if (handler_->OnNewStream(connection_id(), id)) {
      auto stream = std::make_unique<Stream>(id, this, handler_);
      Stream* stream_ptr = stream.get();
      ActivateStream(std::move(stream));
      return stream_ptr;
    } else {
      return nullptr;
    }
  }

  QuicCryptoStream* GetMutableCryptoStream() override {
    return crypto_stream_.get();
  }

  bool ShouldKeepConnectionAlive() const override { return true; }

  void OnDatagramReceived(absl::string_view datagram) override {
    absl::Span<const std::byte> datagram_bytes(
        reinterpret_cast<const std::byte*>(datagram.data()), datagram.size());
    handler_->OnDatagramReceived(connection_id(), datagram_bytes);
  }

 private:
  std::unique_ptr<QuicConnection> connection_;
  std::unique_ptr<QuicCryptoStream> crypto_stream_;
  DestructionObserver* absl_nullable destruction_observer_ = nullptr;
  BasicQuicServer::Handler* absl_nonnull const handler_;
};

class Dispatcher : public QuicDispatcher, public Session::DestructionObserver {
 public:
  Dispatcher(
      const QuicConfig* config ABSL_ATTRIBUTE_LIFETIME_BOUND,
      const QuicCryptoServerConfig* crypto_config ABSL_ATTRIBUTE_LIFETIME_BOUND,
      QuicVersionManager* version_manager ABSL_ATTRIBUTE_LIFETIME_BOUND,
      std::unique_ptr<QuicAlarmFactory> alarm_factory,
      ConnectionIdGeneratorInterface* absl_nonnull connection_id_generator
          ABSL_ATTRIBUTE_LIFETIME_BOUND,
      BasicQuicServer::Handler* absl_nonnull handler
          ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : QuicDispatcher(config, crypto_config, version_manager,
                       std::make_unique<QuicDefaultConnectionHelper>(),
                       std::make_unique<QuicSimpleCryptoServerStreamHelper>(),
                       std::move(alarm_factory), kQuicDefaultConnectionIdLength,
                       *connection_id_generator),
        handler_(handler) {}

  ~Dispatcher() override {
    // Stop sessions from notifying back to this dispatcher on destruction.
    for (auto& [connection_id, session] : sessions_) {
      session->UnregisterDestructionObserver();
    }
  }

  absl::StatusOr<Session*> GetSession(QuicConnectionId connection_id) {
    auto it = sessions_.find(connection_id);
    if (it == sessions_.end()) {
      return absl::NotFoundError("Session not found.");
    } else {
      return it->second;
    }
  }

  // Session::DestructionObserver
  void OnSessionDestroyed(Session* session) override {
    QUICHE_CHECK_EQ(sessions_.erase(session->connection_id()), 1);
  }

 protected:
  std::unique_ptr<QuicSession> CreateQuicSession(
      QuicConnectionId server_connection_id,
      const QuicSocketAddress& self_address,
      const QuicSocketAddress& peer_address, absl::string_view alpn,
      const ParsedQuicVersion& version, const ParsedClientHello& parsed_chlo,
      ConnectionIdGeneratorInterface& connection_id_generator) override {
    QUICHE_LOG(INFO) << "Creating QuicSession for connection "
                     << server_connection_id << " with peer "
                     << peer_address.ToString() << " and ALPN " << alpn;

    auto connection = std::make_unique<QuicConnection>(
        server_connection_id, self_address, peer_address, helper(),
        alarm_factory(), writer(),
        /*owns_writer=*/false, Perspective::IS_SERVER,
        ParsedQuicVersionVector{version}, connection_id_generator);

    auto session = std::make_unique<Session>(
        std::move(connection), this, config(), crypto_config(), handler_);
    auto [it, inserted] =
        sessions_.emplace(server_connection_id, session.get());
    QUICHE_CHECK(inserted);
    session->RegisterDestructionObserver(this);

    return session;
  }

 private:
  // Non-refed Session pointers to allow destruction, but expect the Session to
  // call OnSessionDestroyed to remove itself.
  absl::flat_hash_map<QuicConnectionId, Session*, QuicConnectionIdHash>
      sessions_;

  BasicQuicServer::Handler* absl_nonnull const handler_;
};

}  // namespace

class BasicQuicServer::ServerThread : public QuicheThread {
 public:
  ServerThread(QuicheSocketAddress socket_address,
               const QuicConfig* absl_nonnull config
                   ABSL_ATTRIBUTE_LIFETIME_BOUND,
               const QuicCryptoServerConfig* absl_nonnull crypto_config
                   ABSL_ATTRIBUTE_LIFETIME_BOUND,
               std::unique_ptr<BasicQuicServer::Handler> absl_nonnull handler)
      : QuicheThread("BasicQuicServerThread"),
        socket_address_(std::move(socket_address)),
        config_(config),
        crypto_config_(crypto_config),
        handler_(std::move(handler)) {}

  ~ServerThread() override {
    // Expect/require server to be stopped before destruction.
    QUICHE_CHECK(!running_notification_.HasBeenNotified() ||
                 shutdown_notification_.HasBeenNotified());
    QUICHE_CHECK(!dispatcher_);
  }

  void Run() override {
    QUICHE_CHECK(!running_notification_.HasBeenNotified());
    QUICHE_CHECK(!shutdown_notification_.HasBeenNotified());

    thread_id_ = std::this_thread::get_id();

    QUICHE_CHECK(absl::IsUnknown(initialization_status_));
    initialization_status_ = Initialize();
    if (!initialization_status_.ok()) {
      QUICHE_LOG(ERROR) << "Failed to initialize BasicQuicServer: "
                        << initialization_status_;
      running_notification_.Notify();
      return;
    }

    bound_address_ = io_harness_->local_address();

    QUICHE_LOG(INFO) << "Starting BasicQuicServer on " << bound_address_;
    running_notification_.Notify();
    while (!shutdown_notification_.HasBeenNotified()) {
      ExecuteScheduledActions();
      event_loop_->RunEventLoopOnce(QuicTimeDelta::FromMilliseconds(50));
    }
    thread_id_ = std::thread::id();

    bound_address_ = QuicheSocketAddress();

    dispatcher_->Shutdown();
    io_harness_.reset();
    socket_fd_.reset();
    dispatcher_.reset();

    // Don't reset `event_loop_`, as off-thread callers may attempt to use it
    // to call WakeUp().
  }

  absl::Status WaitForRunning() {
    running_notification_.WaitForNotification();
    QUICHE_CHECK(!absl::IsUnknown(initialization_status_));
    return initialization_status_;
  }

  void Shutdown() {
    QUICHE_CHECK(running_notification_.HasBeenNotified());
    QUICHE_CHECK(!shutdown_notification_.HasBeenNotified());
    QUICHE_CHECK(!IsRunningOnThread());

    shutdown_notification_.Notify();
    if (event_loop_->SupportsWakeUp()) {
      event_loop_->WakeUp();
    }

    Join();
  }

  absl::StatusOr<QuicheSocketAddress> bound_address() const {
    QUICHE_CHECK(IsRunningOnThread());

    return bound_address_;
  }

  absl::StatusOr<int> SendStreamData(QuicConnectionId server_connection_id,
                                     QuicStreamId stream_id,
                                     absl::Span<QuicheMemSlice> data,
                                     bool fin) {
    QUICHE_CHECK(IsRunningOnThread());
    QUICHE_CHECK(dispatcher_);

    QUICHE_ASSIGN_OR_RETURN(QuicSession * session,
                            dispatcher_->GetSession(server_connection_id));

    QuicStream* stream = session->GetActiveStream(stream_id);
    if (!stream) {
      return absl::NotFoundError("Stream not found.");
    }
    return stream->WriteMemSlices(data, fin).bytes_consumed;
  }

  absl::Status SendDatagram(QuicConnectionId server_connection_id,
                            absl::Span<QuicheMemSlice> data) {
    QUICHE_CHECK(IsRunningOnThread());
    QUICHE_CHECK(dispatcher_);

    QUICHE_ASSIGN_OR_RETURN(QuicSession * session,
                            dispatcher_->GetSession(server_connection_id));

    if (!session->connection() || !session->connection()->connected()) {
      return absl::FailedPreconditionError("Session is not connected.");
    }

    DatagramResult result = session->SendDatagram(data);
    if (result.status == DATAGRAM_STATUS_SUCCESS) {
      return absl::OkStatus();
    } else {
      return absl::InternalError(absl::StrCat(
          "Failed to send datagram: ", DatagramStatusToString(result.status)));
    }
  }

  void Schedule(quiche::SingleUseCallback<void()> callback) {
    QUICHE_CHECK(running_notification_.HasBeenNotified());
    QUICHE_CHECK(!shutdown_notification_.HasBeenNotified());

    {
      absl::MutexLock lock(scheduled_actions_mutex_);
      scheduled_actions_.push(std::move(callback));
    }

    if (IsRunningOnThread()) {
      // Run all scheduled actions immediately if already on the server thread.
      // This is important to avoid deadlocks if the caller of Schedule() waits
      // on signals contained in the callback.
      ExecuteScheduledActions();
    } else if (event_loop_->SupportsWakeUp()) {
      event_loop_->WakeUp();
    }
  }

  void ScheduleAndWaitForCompletion(
      quiche::SingleUseCallback<void()> callback) {
    QUICHE_CHECK(running_notification_.HasBeenNotified());
    QUICHE_CHECK(!shutdown_notification_.HasBeenNotified());

    if (IsRunningOnThread()) {
      ExecuteScheduledActions();
      std::move(callback)();
    } else {
      absl::Notification done;
      Schedule([&callback, &done]() {
        std::move(callback)();
        done.Notify();
      });
      done.WaitForNotification();
    }
  }

  bool IsRunningOnThread() const {
    return running_notification_.HasBeenNotified() &&
           thread_id_ == std::this_thread::get_id();
  }

 private:
  absl::Status Initialize() {
    event_loop_ = GetDefaultEventLoop()->Create(QuicDefaultClock::Get());
    dispatcher_ =
        std::make_unique<Dispatcher>(config_, crypto_config_, &version_manager_,
                                     event_loop_->CreateAlarmFactory(),
                                     &connection_id_generator_, handler_.get());

    QUICHE_ASSIGN_OR_RETURN(socket_fd_,
                            CreateAndBindServerSocket(socket_address_));
    QUICHE_ASSIGN_OR_RETURN(
        io_harness_,
        QuicServerIoHarness::Create(event_loop_.get(), dispatcher_.get(),
                                    socket_fd_.get()));
    io_harness_->InitializeWriter();

    return absl::OkStatus();
  }

  void ExecuteScheduledActions() {
    QUICHE_CHECK(IsRunningOnThread());

    std::queue<quiche::SingleUseCallback<void()>> actions;
    {
      absl::MutexLock lock(scheduled_actions_mutex_);
      actions.swap(scheduled_actions_);
    }

    while (!actions.empty() && !shutdown_notification_.HasBeenNotified()) {
      std::move(actions.front())();
      actions.pop();
    }
  }

  const QuicheSocketAddress socket_address_;
  const QuicConfig* absl_nonnull const config_;
  const QuicCryptoServerConfig* absl_nonnull const crypto_config_;
  const std::unique_ptr<BasicQuicServer::Handler> absl_nonnull handler_;

  // Must be set before `running_notification_` is notified and not changed
  // after that.
  absl::Status initialization_status_ = absl::UnknownError("Not initialized.");
  std::thread::id thread_id_;

  absl::Notification running_notification_;
  absl::Notification shutdown_notification_;

  absl::Mutex scheduled_actions_mutex_;
  std::queue<quiche::SingleUseCallback<void()>> scheduled_actions_
      ABSL_GUARDED_BY(scheduled_actions_mutex_);

  QuicVersionManager version_manager_{ParsedQuicVersionVector(
      kSupportedQuicVersions.begin(), kSupportedQuicVersions.end())};
  DeterministicConnectionIdGenerator connection_id_generator_{
      kQuicDefaultConnectionIdLength};

  std::unique_ptr<QuicEventLoop> event_loop_;
  std::unique_ptr<Dispatcher> dispatcher_;
  OwnedSocketFd socket_fd_;
  std::unique_ptr<QuicServerIoHarness> io_harness_;

  QuicheSocketAddress bound_address_;
};

BasicQuicServer::BasicQuicServer(
    QuicheSocketAddress socket_address,
    std::unique_ptr<ProofSource> absl_nonnull proof_source)
    : socket_address_(std::move(socket_address)),
      crypto_config_(std::make_unique<QuicCryptoServerConfig>(
          QuicCryptoServerConfig::TESTING, QuicRandom::GetInstance(),
          std::move(proof_source), KeyExchangeSource::Default())) {}

BasicQuicServer::~BasicQuicServer() {
  if (server_thread_) {
    QUICHE_LOG(WARNING) << "BasicQuicServer not stopped before destruction.";
    QUICHE_CHECK_OK(Stop());
  }
}

absl::Status BasicQuicServer::Start(
    std::unique_ptr<Handler> absl_nonnull handler) {
  QUICHE_CHECK(!server_thread_) << "Server already running.";

  server_thread_ = std::make_unique<ServerThread>(
      socket_address_, &config_, crypto_config_.get(), std::move(handler));
  server_thread_->Start();

  absl::Status status = server_thread_->WaitForRunning();
  if (!status.ok()) {
    server_thread_->Shutdown();
    server_thread_.reset();
    return status;
  }

  return absl::OkStatus();
}

absl::Status BasicQuicServer::Stop() {
  QUICHE_CHECK(server_thread_) << "Server not running.";

  server_thread_->Shutdown();
  server_thread_.reset();

  return absl::OkStatus();
}

void BasicQuicServer::Schedule(quiche::SingleUseCallback<void()> callback) {
  QUICHE_CHECK(server_thread_) << "Server not running.";
  server_thread_->Schedule(std::move(callback));
}

void BasicQuicServer::ScheduleAndWaitForCompletion(
    quiche::SingleUseCallback<void()> callback) {
  QUICHE_CHECK(server_thread_) << "Server not running.";
  server_thread_->ScheduleAndWaitForCompletion(std::move(callback));
}

absl::StatusOr<QuicheSocketAddress> BasicQuicServer::bound_address() {
  QUICHE_CHECK(server_thread_) << "Server not running.";

  absl::StatusOr<QuicheSocketAddress> result;
  const ServerThread* server_thread = server_thread_.get();
  ScheduleAndWaitForCompletion(
      [&result, server_thread]() { result = server_thread->bound_address(); });
  return result;
}

absl::StatusOr<int> BasicQuicServer::SendStreamData(
    QuicConnectionId server_connection_id, QuicStreamId stream_id,
    absl::Span<const std::byte> data, bool fin) {
  auto mem_slice = QuicheMemSlice::Copy(absl::string_view(
      reinterpret_cast<const char*>(data.data()), data.size()));
  return SendStreamData(server_connection_id, stream_id,
                        absl::MakeSpan(&mem_slice, 1), fin);
}

absl::StatusOr<int> BasicQuicServer::SendStreamData(
    QuicConnectionId server_connection_id, QuicStreamId stream_id,
    absl::Span<quiche::QuicheMemSlice> data, bool fin) {
  QUICHE_CHECK(server_thread_) << "Server not running.";

  ServerThread* server_thread = server_thread_.get();
  absl::StatusOr<int> result;
  ScheduleAndWaitForCompletion(
      [&result, server_thread, server_connection_id, stream_id, data, fin]() {
        result = server_thread->SendStreamData(server_connection_id, stream_id,
                                               data, fin);
      });
  return result;
}

absl::Status BasicQuicServer::SendDatagram(
    QuicConnectionId server_connection_id, absl::Span<const std::byte> data) {
  auto mem_slice = QuicheMemSlice::Copy(absl::string_view(
      reinterpret_cast<const char*>(data.data()), data.size()));
  return SendDatagram(server_connection_id, absl::MakeSpan(&mem_slice, 1));
}

absl::Status BasicQuicServer::SendDatagram(
    QuicConnectionId server_connection_id,
    absl::Span<quiche::QuicheMemSlice> data) {
  QUICHE_CHECK(server_thread_) << "Server not running.";

  ServerThread* server_thread = server_thread_.get();
  absl::Status result;
  ScheduleAndWaitForCompletion(
      [&result, server_thread, server_connection_id, data]() {
        result = server_thread->SendDatagram(server_connection_id, data);
      });
  return result;
}

}  // namespace quic::test
