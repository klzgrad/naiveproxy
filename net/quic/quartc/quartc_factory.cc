// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/quartc/quartc_factory.h"

#include "net/quic/core/crypto/quic_random.h"
#include "net/quic/platform/api/quic_socket_address.h"
#include "net/quic/quartc/quartc_session.h"

namespace net {

namespace {

// Implements the QuicAlarm with QuartcTaskRunnerInterface for the Quartc
//  users other than Chromium. For example, WebRTC will create QuartcAlarm with
// a QuartcTaskRunner implemented by WebRTC.
class QuartcAlarm : public QuicAlarm, public QuartcTaskRunnerInterface::Task {
 public:
  QuartcAlarm(const QuicClock* clock,
              QuartcTaskRunnerInterface* task_runner,
              QuicArenaScopedPtr<QuicAlarm::Delegate> delegate)
      : QuicAlarm(std::move(delegate)),
        clock_(clock),
        task_runner_(task_runner) {}

  ~QuartcAlarm() override {
    // Cancel the scheduled task before getting deleted.
    CancelImpl();
  }

  // QuicAlarm overrides.
  void SetImpl() override {
    DCHECK(deadline().IsInitialized());
    // Cancel it if already set.
    CancelImpl();

    int64_t delay_ms = (deadline() - (clock_->Now())).ToMilliseconds();
    if (delay_ms < 0) {
      delay_ms = 0;
    }

    DCHECK(task_runner_);
    DCHECK(!scheduled_task_);
    scheduled_task_ = task_runner_->Schedule(this, delay_ms);
  }

  void CancelImpl() override {
    if (scheduled_task_) {
      scheduled_task_->Cancel();
      scheduled_task_.reset();
    }
  }

  // QuartcTaskRunner::Task overrides.
  void Run() override {
    // The alarm may have been cancelled.
    if (!deadline().IsInitialized()) {
      return;
    }

    // The alarm may have been re-set to a later time.
    if (clock_->Now() < deadline()) {
      SetImpl();
      return;
    }

    Fire();
  }

 private:
  // Not owned by QuartcAlarm. Owned by the QuartcFactory.
  const QuicClock* clock_;
  // Not owned by QuartcAlarm. Owned by the QuartcFactory.
  QuartcTaskRunnerInterface* task_runner_;
  // Owned by QuartcAlarm.
  std::unique_ptr<QuartcTaskRunnerInterface::ScheduledTask> scheduled_task_;
};

// Adapts QuartcClockInterface (provided by the user) to QuicClock
// (expected by QUIC).
class QuartcClock : public QuicClock {
 public:
  explicit QuartcClock(QuartcClockInterface* clock) : clock_(clock) {}
  QuicTime ApproximateNow() const override { return Now(); }
  QuicTime Now() const override {
    return QuicTime::Zero() +
           QuicTime::Delta::FromMicroseconds(clock_->NowMicroseconds());
  }
  QuicWallTime WallNow() const override {
    return QuicWallTime::FromUNIXMicroseconds(clock_->NowMicroseconds());
  }

 private:
  QuartcClockInterface* clock_;
};

}  // namespace

QuartcFactory::QuartcFactory(const QuartcFactoryConfig& factory_config)
    : task_runner_(factory_config.task_runner),
      clock_(new QuartcClock(factory_config.clock)) {}

QuartcFactory::~QuartcFactory() {}

std::unique_ptr<QuartcSessionInterface> QuartcFactory::CreateQuartcSession(
    const QuartcSessionConfig& quartc_session_config) {
  DCHECK(quartc_session_config.packet_transport);

  Perspective perspective = quartc_session_config.is_server
                                ? Perspective::IS_SERVER
                                : Perspective::IS_CLIENT;
  std::unique_ptr<QuicConnection> quic_connection =
      CreateQuicConnection(quartc_session_config, perspective);
  QuicTagVector copt;
  if (quartc_session_config.congestion_control ==
      QuartcCongestionControl::kBBR) {
    copt.push_back(kTBBR);
  }
  QuicConfig quic_config;
  quic_config.SetConnectionOptionsToSend(copt);
  quic_config.SetClientConnectionOptions(copt);
  return std::unique_ptr<QuartcSessionInterface>(new QuartcSession(
      std::move(quic_connection), quic_config,
      quartc_session_config.unique_remote_server_id, perspective,
      this /*QuicConnectionHelperInterface*/, clock_.get()));
}

std::unique_ptr<QuicConnection> QuartcFactory::CreateQuicConnection(
    const QuartcSessionConfig& quartc_session_config,
    Perspective perspective) {
  // The QuicConnection will take the ownership.
  std::unique_ptr<QuartcPacketWriter> writer(
      new QuartcPacketWriter(quartc_session_config.packet_transport,
                             quartc_session_config.max_packet_size));
  // dummy_id and dummy_address are used because Quartc network layer will not
  // use these two.
  QuicConnectionId dummy_id = 0;
  QuicSocketAddress dummy_address(QuicIpAddress::Any4(), 0 /*Port*/);
  return std::unique_ptr<QuicConnection>(new QuicConnection(
      dummy_id, dummy_address, this, /*QuicConnectionHelperInterface*/
      this /*QuicAlarmFactory*/, writer.release(), true /*own the writer*/,
      perspective, AllSupportedTransportVersions()));
}

QuicAlarm* QuartcFactory::CreateAlarm(QuicAlarm::Delegate* delegate) {
  return new QuartcAlarm(GetClock(), task_runner_,
                         QuicArenaScopedPtr<QuicAlarm::Delegate>(delegate));
}

QuicArenaScopedPtr<QuicAlarm> QuartcFactory::CreateAlarm(
    QuicArenaScopedPtr<QuicAlarm::Delegate> delegate,
    QuicConnectionArena* arena) {
  if (arena != nullptr) {
    return arena->New<QuartcAlarm>(GetClock(), task_runner_,
                                   std::move(delegate));
  }
  return QuicArenaScopedPtr<QuicAlarm>(
      new QuartcAlarm(GetClock(), task_runner_, std::move(delegate)));
}

const QuicClock* QuartcFactory::GetClock() const {
  return clock_.get();
}

QuicRandom* QuartcFactory::GetRandomGenerator() {
  return QuicRandom::GetInstance();
}

QuicBufferAllocator* QuartcFactory::GetStreamFrameBufferAllocator() {
  return &buffer_allocator_;
}

QuicBufferAllocator* QuartcFactory::GetStreamSendBufferAllocator() {
  return &buffer_allocator_;
}

std::unique_ptr<QuartcFactoryInterface> CreateQuartcFactory(
    const QuartcFactoryConfig& factory_config) {
  return std::unique_ptr<QuartcFactoryInterface>(
      new QuartcFactory(factory_config));
}

}  // namespace net
