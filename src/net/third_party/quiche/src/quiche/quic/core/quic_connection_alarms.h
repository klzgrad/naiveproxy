// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_CONNECTION_ALARMS_H_
#define QUICHE_QUIC_CORE_QUIC_CONNECTION_ALARMS_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "absl/base/nullability.h"
#include "absl/types/variant.h"
#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_arena_scoped_ptr.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_connection_context.h"
#include "quiche/quic/core/quic_one_block_arena.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quic {

class QUICHE_EXPORT QuicConnectionAlarmsDelegate {
 public:
  virtual ~QuicConnectionAlarmsDelegate() = default;

  virtual void OnSendAlarm() = 0;
  virtual void OnAckAlarm() = 0;
  virtual void OnRetransmissionAlarm() = 0;
  virtual void OnMtuDiscoveryAlarm() = 0;
  virtual void OnProcessUndecryptablePacketsAlarm() = 0;
  virtual void OnDiscardPreviousOneRttKeysAlarm() = 0;
  virtual void OnDiscardZeroRttDecryptionKeysAlarm() = 0;
  virtual void MaybeProbeMultiPortPath() = 0;
  virtual void OnIdleDetectorAlarm() = 0;
  virtual void OnNetworkBlackholeDetectorAlarm() = 0;
  virtual void OnPingAlarm() = 0;

  virtual QuicConnectionContext* context() = 0;
  virtual const QuicClock* clock() const = 0;
};

namespace test {
class QuicAlarmMultiplexerPeer;
class QuicConnectionAlarmsPeer;
}  // namespace test

enum class QuicAlarmSlot : uint8_t {
  // An alarm that is scheduled when the SentPacketManager requires a delay
  // before sending packets and fires when the packet may be sent.
  kSend,
  // An alarm that fires when an ACK should be sent to the peer.
  kAck,
  // An alarm that fires when a packet needs to be retransmitted.
  kRetransmission,
  // An alarm that fires when an MTU probe should be sent.
  kMtuDiscovery,
  // An alarm that fires to process undecryptable packets when new decryption
  // keys are available.
  kProcessUndecryptablePackets,
  // An alarm that fires to discard keys for the previous key phase some time
  // after a key update has completed.
  kDiscardPreviousOneRttKeys,
  // An alarm that fires to discard 0-RTT decryption keys some time after the
  // first 1-RTT packet has been decrypted. Only used on server connections with
  // TLS handshaker.
  kDiscardZeroRttDecryptionKeys,
  // An alarm that fires to keep probing the multi-port path.
  kMultiPortProbing,
  // An alarm for QuicIdleNetworkDetector.
  kIdleNetworkDetector,
  // An alarm for QuicNetworkBlackholeDetection.
  kNetworkBlackholeDetector,
  // An alarm for QuicPingManager.
  kPing,

  // Must be the last element.
  kSlotCount
};
std::string QuicAlarmSlotName(QuicAlarmSlot slot);

// QuicAlarmMultiplexer manages the alarms used by the QuicConnection. Its main
// purpose is to minimize the cost of scheduling and rescheduling the multiple
// alarms that QuicConnection has by reducing all of those alarms to just two.
class QUICHE_EXPORT QuicAlarmMultiplexer {
 public:
  // Proxy classes that allow an individual alarm to be accessed via
  // a QuicAlarm-compatible API.
  class AlarmProxy {
   public:
    AlarmProxy(QuicAlarmMultiplexer* multiplexer, QuicAlarmSlot slot)
        : multiplexer_(multiplexer), slot_(slot) {}

    bool IsSet() const { return multiplexer_->IsSet(slot_); }
    QuicTime deadline() const { return multiplexer_->GetDeadline(slot_); }
    bool IsPermanentlyCancelled() const {
      return multiplexer_->IsPermanentlyCancelled();
    }

    void Set(QuicTime new_deadline) { multiplexer_->Set(slot_, new_deadline); }
    void Update(QuicTime new_deadline, QuicTime::Delta granularity) {
      multiplexer_->Update(slot_, new_deadline, granularity);
    }
    void Cancel() { multiplexer_->Cancel(slot_); }

    void PermanentCancel() {}

   private:
    friend class ::quic::test::QuicConnectionAlarmsPeer;

    QuicAlarmMultiplexer* multiplexer_;
    QuicAlarmSlot slot_;
  };

  // Proxy classes that allow an individual alarm to be accessed via
  // a QuicAlarm-compatible API.
  class ConstAlarmProxy {
   public:
    ConstAlarmProxy(const QuicAlarmMultiplexer* multiplexer, QuicAlarmSlot slot)
        : multiplexer_(multiplexer), slot_(slot) {}

    bool IsSet() const { return multiplexer_->IsSet(slot_); }
    QuicTime deadline() const { return multiplexer_->GetDeadline(slot_); }
    bool IsPermanentlyCancelled() const {
      return multiplexer_->IsPermanentlyCancelled();
    }

   private:
    const QuicAlarmMultiplexer* multiplexer_;
    QuicAlarmSlot slot_;
  };

  static constexpr size_t kNumberOfSlots =
      static_cast<size_t>(QuicAlarmSlot::kSlotCount);

  QuicAlarmMultiplexer(absl::Nonnull<QuicConnectionAlarmsDelegate*> connection,
                       QuicConnectionArena& arena,
                       QuicAlarmFactory& alarm_factory);

  // QuicAlarmMultiplexer is not movable, as it has platform alarms that retain
  // a long-term pointer to it.
  QuicAlarmMultiplexer(const QuicAlarmMultiplexer&) = delete;
  QuicAlarmMultiplexer(QuicAlarmMultiplexer&&) = delete;
  QuicAlarmMultiplexer& operator=(const QuicAlarmMultiplexer&) = delete;
  QuicAlarmMultiplexer& operator=(QuicAlarmMultiplexer&&) = delete;

  // Implementation of QuicAlarm methods.
  void Set(QuicAlarmSlot slot, QuicTime new_deadline);
  void Update(QuicAlarmSlot slot, QuicTime new_deadline,
              QuicTimeDelta granularity);
  void Cancel(QuicAlarmSlot slot) {
    SetDeadlineFor(slot, QuicTime::Zero());
    MaybeRescheduleUnderlyingAlarms();
  }
  bool IsSet(QuicAlarmSlot slot) const {
    return GetDeadline(slot).IsInitialized();
  }
  bool IsPermanentlyCancelled() const { return permanently_cancelled_; }
  QuicTime GetDeadline(QuicAlarmSlot slot) const {
    return deadlines_[static_cast<size_t>(slot)];
  }

  void CancelAllAlarms();

  // Executes callbacks for all of the alarms that are currently due.
  void FireAlarms();

  // Methods used by ScopedPacketFlusher to defer updates to the underlying
  // platform alarm.
  void DeferUnderlyingAlarmScheduling();
  void ResumeUnderlyingAlarmScheduling();

  QuicConnectionAlarmsDelegate* delegate() { return connection_; }

  // Outputs a formatted list of active alarms.
  std::string DebugString();

 private:
  friend class ::quic::test::QuicConnectionAlarmsPeer;
  friend class ::quic::test::QuicAlarmMultiplexerPeer;

  void SetDeadlineFor(QuicAlarmSlot slot, QuicTime deadline) {
    deadlines_[static_cast<size_t>(slot)] = deadline;
  }

  // Fires an individual alarm if it is set.
  void Fire(QuicAlarmSlot slot);

  void MaybeRescheduleUnderlyingAlarms() {
    if (defer_updates_of_underlying_alarms_ || permanently_cancelled_) {
      return;
    }
    RescheduleUnderlyingAlarms();
  }
  // Updates the underlying platform alarm.
  void RescheduleUnderlyingAlarms();

  // Deadlines for all of the alarms that can be placed into the multiplexer,
  // indexed by the values of QuicAlarmSlot enum.
  std::array<QuicTime, kNumberOfSlots> deadlines_;

  // Actual alarms provided by the underlying platform. Note that there are two
  // of them: the first is used for alarms that are scheduled for now or
  // earlier, and the latter is used for alarms that are scheduled in the
  // future.  The reason those are split is that QUIC has a lot of alarms that
  // are only fired immediately, and splitting those allows to avoid having
  // extra reschedules.
  QuicArenaScopedPtr<QuicAlarm> now_alarm_;
  QuicArenaScopedPtr<QuicAlarm> later_alarm_;

  // Underlying connection and individual connection components. Not owned.
  QuicConnectionAlarmsDelegate* connection_;

  // Latched value of --quic_multiplexer_alarm_granularity_us.
  QuicTimeDelta underlying_alarm_granularity_;

  // If true, all of the alarms have been permanently cancelled.
  bool permanently_cancelled_ = false;
  // If true, the actual underlying alarms won't be rescheduled until
  // ResumeUnderlyingAlarmScheduling() is called.
  bool defer_updates_of_underlying_alarms_ = false;
};

class QUICHE_EXPORT QuicConnectionAlarmHolder {
 public:
  // Provides a QuicAlarm-like interface to an alarm contained within
  // QuicConnectionAlarms.
  class AlarmProxy {
   public:
    explicit AlarmProxy(absl::Nonnull<QuicAlarm*> alarm) : alarm_(alarm) {}

    bool IsSet() const { return alarm_->IsSet(); }
    QuicTime deadline() const { return alarm_->deadline(); }
    bool IsPermanentlyCancelled() const {
      return alarm_->IsPermanentlyCancelled();
    }

    void Set(QuicTime new_deadline) { alarm_->Set(new_deadline); }
    void Update(QuicTime new_deadline, QuicTime::Delta granularity) {
      alarm_->Update(new_deadline, granularity);
    }
    void Cancel() { alarm_->Cancel(); }
    void PermanentCancel() { alarm_->PermanentCancel(); }

   private:
    friend class ::quic::test::QuicConnectionAlarmsPeer;

    absl::Nonnull<QuicAlarm*> alarm_;
  };

  // Provides a QuicAlarm-like interface to an alarm contained within
  // QuicConnectionAlarms.
  class ConstAlarmProxy {
   public:
    explicit ConstAlarmProxy(const QuicAlarm* alarm) : alarm_(alarm) {}

    bool IsSet() const { return alarm_->IsSet(); }
    QuicTime deadline() const { return alarm_->deadline(); }
    bool IsPermanentlyCancelled() const {
      return alarm_->IsPermanentlyCancelled();
    }

   private:
    friend class ::quic::test::QuicConnectionAlarmsPeer;

    const QuicAlarm* alarm_;
  };

  QuicConnectionAlarmHolder(QuicConnectionAlarmsDelegate* delegate,
                            QuicAlarmFactory& alarm_factory,
                            QuicConnectionArena& arena);

  AlarmProxy ack_alarm() { return AlarmProxy(ack_alarm_.get()); }
  AlarmProxy retransmission_alarm() {
    return AlarmProxy(retransmission_alarm_.get());
  }
  AlarmProxy send_alarm() { return AlarmProxy(send_alarm_.get()); }
  AlarmProxy mtu_discovery_alarm() {
    return AlarmProxy(mtu_discovery_alarm_.get());
  }
  AlarmProxy process_undecryptable_packets_alarm() {
    return AlarmProxy(process_undecryptable_packets_alarm_.get());
  }
  AlarmProxy discard_previous_one_rtt_keys_alarm() {
    return AlarmProxy(discard_previous_one_rtt_keys_alarm_.get());
  }
  AlarmProxy discard_zero_rtt_decryption_keys_alarm() {
    return AlarmProxy(discard_zero_rtt_decryption_keys_alarm_.get());
  }
  AlarmProxy multi_port_probing_alarm() {
    return AlarmProxy(multi_port_probing_alarm_.get());
  }
  AlarmProxy idle_network_detector_alarm() {
    return AlarmProxy(idle_network_detector_alarm_.get());
  }
  AlarmProxy network_blackhole_detector_alarm() {
    return AlarmProxy(network_blackhole_detector_alarm_.get());
  }
  AlarmProxy ping_alarm() { return AlarmProxy(ping_alarm_.get()); }

  ConstAlarmProxy ack_alarm() const {
    return ConstAlarmProxy(ack_alarm_.get());
  }
  ConstAlarmProxy retransmission_alarm() const {
    return ConstAlarmProxy(retransmission_alarm_.get());
  }
  ConstAlarmProxy send_alarm() const {
    return ConstAlarmProxy(send_alarm_.get());
  }
  ConstAlarmProxy mtu_discovery_alarm() const {
    return ConstAlarmProxy(mtu_discovery_alarm_.get());
  }
  ConstAlarmProxy process_undecryptable_packets_alarm() const {
    return ConstAlarmProxy(process_undecryptable_packets_alarm_.get());
  }
  ConstAlarmProxy discard_previous_one_rtt_keys_alarm() const {
    return ConstAlarmProxy(discard_previous_one_rtt_keys_alarm_.get());
  }
  ConstAlarmProxy discard_zero_rtt_decryption_keys_alarm() const {
    return ConstAlarmProxy(discard_zero_rtt_decryption_keys_alarm_.get());
  }
  ConstAlarmProxy multi_port_probing_alarm() const {
    return ConstAlarmProxy(multi_port_probing_alarm_.get());
  }
  ConstAlarmProxy idle_network_detector_alarm() const {
    return ConstAlarmProxy(idle_network_detector_alarm_.get());
  }
  ConstAlarmProxy network_blackhole_detector_alarm() const {
    return ConstAlarmProxy(network_blackhole_detector_alarm_.get());
  }
  ConstAlarmProxy ping_alarm() const {
    return ConstAlarmProxy(ping_alarm_.get());
  }

 private:
  // An alarm that fires when an ACK should be sent to the peer.
  QuicArenaScopedPtr<QuicAlarm> ack_alarm_;
  // An alarm that fires when a packet needs to be retransmitted.
  QuicArenaScopedPtr<QuicAlarm> retransmission_alarm_;
  // An alarm that is scheduled when the SentPacketManager requires a delay
  // before sending packets and fires when the packet may be sent.
  QuicArenaScopedPtr<QuicAlarm> send_alarm_;
  // An alarm that fires when an MTU probe should be sent.
  QuicArenaScopedPtr<QuicAlarm> mtu_discovery_alarm_;
  // An alarm that fires to process undecryptable packets when new decryption
  // keys are available.
  QuicArenaScopedPtr<QuicAlarm> process_undecryptable_packets_alarm_;
  // An alarm that fires to discard keys for the previous key phase some time
  // after a key update has completed.
  QuicArenaScopedPtr<QuicAlarm> discard_previous_one_rtt_keys_alarm_;
  // An alarm that fires to discard 0-RTT decryption keys some time after the
  // first 1-RTT packet has been decrypted. Only used on server connections with
  // TLS handshaker.
  QuicArenaScopedPtr<QuicAlarm> discard_zero_rtt_decryption_keys_alarm_;
  // An alarm that fires to keep probing the multi-port path.
  QuicArenaScopedPtr<QuicAlarm> multi_port_probing_alarm_;
  // An alarm for QuicIdleNetworkDetector.
  QuicArenaScopedPtr<QuicAlarm> idle_network_detector_alarm_;
  // An alarm for QuicNetworkBlackholeDetection.
  QuicArenaScopedPtr<QuicAlarm> network_blackhole_detector_alarm_;
  // An alarm for QuicPingManager.
  QuicArenaScopedPtr<QuicAlarm> ping_alarm_;
};

// A class for holding all QuicAlarms belonging to a single connection.
// Dispatches all calls to either QuicConnectionAlarmHolder of
// QuicAlarmMultiplexer.
class QUICHE_EXPORT QuicConnectionAlarms {
 public:
  // Wraps a ConstAlarmProxy provided by either QuicConnectionAlarmHolder or
  // QuicAlarmMultiplexer.
  class ConstAlarmProxy {
   public:
    explicit ConstAlarmProxy(QuicConnectionAlarmHolder::ConstAlarmProxy alarm)
        : alarm_(alarm) {}
    explicit ConstAlarmProxy(QuicAlarmMultiplexer::ConstAlarmProxy alarm)
        : alarm_(alarm) {}

    bool IsSet() const {
      return absl::visit([](auto& alarm) { return alarm.IsSet(); }, alarm_);
    }
    QuicTime deadline() const {
      return absl::visit([](auto& alarm) { return alarm.deadline(); }, alarm_);
    }
    bool IsPermanentlyCancelled() const {
      return absl::visit(
          [](auto& alarm) { return alarm.IsPermanentlyCancelled(); }, alarm_);
    }

   private:
    friend class ::quic::test::QuicConnectionAlarmsPeer;

    absl::variant<QuicConnectionAlarmHolder::ConstAlarmProxy,
                  QuicAlarmMultiplexer::ConstAlarmProxy>
        alarm_;
  };

  // Wraps an AlarmProxy provided by either QuicConnectionAlarmHolder or
  // QuicAlarmMultiplexer.
  class AlarmProxy {
   public:
    explicit AlarmProxy(QuicConnectionAlarmHolder::AlarmProxy alarm)
        : alarm_(alarm) {}
    explicit AlarmProxy(QuicAlarmMultiplexer::AlarmProxy alarm)
        : alarm_(alarm) {}

    bool IsSet() const {
      return absl::visit([](auto& alarm) { return alarm.IsSet(); }, alarm_);
    }
    QuicTime deadline() const {
      return absl::visit([](auto& alarm) { return alarm.deadline(); }, alarm_);
    }
    bool IsPermanentlyCancelled() const {
      return absl::visit(
          [](auto& alarm) { return alarm.IsPermanentlyCancelled(); }, alarm_);
    }

    void Set(QuicTime new_deadline) {
      absl::visit([&](auto& alarm) { alarm.Set(new_deadline); }, alarm_);
    }
    void Update(QuicTime new_deadline, QuicTime::Delta granularity) {
      absl::visit([&](auto& alarm) { alarm.Update(new_deadline, granularity); },
                  alarm_);
    }
    void Cancel() {
      absl::visit([&](auto& alarm) { alarm.Cancel(); }, alarm_);
    }
    void PermanentCancel() {
      absl::visit([&](auto& alarm) { alarm.PermanentCancel(); }, alarm_);
    }

   private:
    friend class ::quic::test::QuicConnectionAlarmsPeer;

    absl::variant<QuicConnectionAlarmHolder::AlarmProxy,
                  QuicAlarmMultiplexer::AlarmProxy>
        alarm_;
  };

  QuicConnectionAlarms(QuicConnectionAlarmsDelegate* delegate,
                       QuicAlarmFactory& alarm_factory,
                       QuicConnectionArena& arena);

  AlarmProxy ack_alarm() {
    if (use_multiplexer_) {
      return AlarmProxy(QuicAlarmMultiplexer::AlarmProxy(&*multiplexer_,
                                                         QuicAlarmSlot::kAck));
    }
    return AlarmProxy(
        QuicConnectionAlarmHolder::AlarmProxy(holder_->ack_alarm()));
  }
  ConstAlarmProxy ack_alarm() const {
    if (use_multiplexer_) {
      return ConstAlarmProxy(QuicAlarmMultiplexer::ConstAlarmProxy(
          &*multiplexer_, QuicAlarmSlot::kAck));
    }
    return ConstAlarmProxy(
        QuicConnectionAlarmHolder::ConstAlarmProxy(holder_->ack_alarm()));
  }

  AlarmProxy retransmission_alarm() {
    if (use_multiplexer_) {
      return AlarmProxy(QuicAlarmMultiplexer::AlarmProxy(
          &*multiplexer_, QuicAlarmSlot::kRetransmission));
    }
    return AlarmProxy(
        QuicConnectionAlarmHolder::AlarmProxy(holder_->retransmission_alarm()));
  }
  ConstAlarmProxy retransmission_alarm() const {
    if (use_multiplexer_) {
      return ConstAlarmProxy(QuicAlarmMultiplexer::ConstAlarmProxy(
          &*multiplexer_, QuicAlarmSlot::kRetransmission));
    }
    return ConstAlarmProxy(QuicConnectionAlarmHolder::ConstAlarmProxy(
        holder_->retransmission_alarm()));
  }

  AlarmProxy send_alarm() {
    if (use_multiplexer_) {
      return AlarmProxy(QuicAlarmMultiplexer::AlarmProxy(&*multiplexer_,
                                                         QuicAlarmSlot::kSend));
    }
    return AlarmProxy(
        QuicConnectionAlarmHolder::AlarmProxy(holder_->send_alarm()));
  }
  ConstAlarmProxy send_alarm() const {
    if (use_multiplexer_) {
      return ConstAlarmProxy(QuicAlarmMultiplexer::ConstAlarmProxy(
          &*multiplexer_, QuicAlarmSlot::kSend));
    }
    return ConstAlarmProxy(
        QuicConnectionAlarmHolder::ConstAlarmProxy(holder_->send_alarm()));
  }

  AlarmProxy mtu_discovery_alarm() {
    if (use_multiplexer_) {
      return AlarmProxy(QuicAlarmMultiplexer::AlarmProxy(
          &*multiplexer_, QuicAlarmSlot::kMtuDiscovery));
    }
    return AlarmProxy(
        QuicConnectionAlarmHolder::AlarmProxy(holder_->mtu_discovery_alarm()));
  }
  ConstAlarmProxy mtu_discovery_alarm() const {
    if (use_multiplexer_) {
      return ConstAlarmProxy(QuicAlarmMultiplexer::ConstAlarmProxy(
          &*multiplexer_, QuicAlarmSlot::kMtuDiscovery));
    }
    return ConstAlarmProxy(QuicConnectionAlarmHolder::ConstAlarmProxy(
        holder_->mtu_discovery_alarm()));
  }

  AlarmProxy process_undecryptable_packets_alarm() {
    if (use_multiplexer_) {
      return AlarmProxy(QuicAlarmMultiplexer::AlarmProxy(
          &*multiplexer_, QuicAlarmSlot::kProcessUndecryptablePackets));
    }
    return AlarmProxy(QuicConnectionAlarmHolder::AlarmProxy(
        holder_->process_undecryptable_packets_alarm()));
  }
  ConstAlarmProxy process_undecryptable_packets_alarm() const {
    if (use_multiplexer_) {
      return ConstAlarmProxy(QuicAlarmMultiplexer::ConstAlarmProxy(
          &*multiplexer_, QuicAlarmSlot::kProcessUndecryptablePackets));
    }
    return ConstAlarmProxy(QuicConnectionAlarmHolder::ConstAlarmProxy(
        holder_->process_undecryptable_packets_alarm()));
  }

  AlarmProxy discard_previous_one_rtt_keys_alarm() {
    if (use_multiplexer_) {
      return AlarmProxy(QuicAlarmMultiplexer::AlarmProxy(
          &*multiplexer_, QuicAlarmSlot::kDiscardPreviousOneRttKeys));
    }
    return AlarmProxy(QuicConnectionAlarmHolder::AlarmProxy(
        holder_->discard_previous_one_rtt_keys_alarm()));
  }
  ConstAlarmProxy discard_previous_one_rtt_keys_alarm() const {
    if (use_multiplexer_) {
      return ConstAlarmProxy(QuicAlarmMultiplexer::ConstAlarmProxy(
          &*multiplexer_, QuicAlarmSlot::kDiscardPreviousOneRttKeys));
    }
    return ConstAlarmProxy(QuicConnectionAlarmHolder::ConstAlarmProxy(
        holder_->discard_previous_one_rtt_keys_alarm()));
  }

  AlarmProxy discard_zero_rtt_decryption_keys_alarm() {
    if (use_multiplexer_) {
      return AlarmProxy(QuicAlarmMultiplexer::AlarmProxy(
          &*multiplexer_, QuicAlarmSlot::kDiscardZeroRttDecryptionKeys));
    }
    return AlarmProxy(QuicConnectionAlarmHolder::AlarmProxy(
        holder_->discard_zero_rtt_decryption_keys_alarm()));
  }
  ConstAlarmProxy discard_zero_rtt_decryption_keys_alarm() const {
    if (use_multiplexer_) {
      return ConstAlarmProxy(QuicAlarmMultiplexer::ConstAlarmProxy(
          &*multiplexer_, QuicAlarmSlot::kDiscardZeroRttDecryptionKeys));
    }
    return ConstAlarmProxy(QuicConnectionAlarmHolder::ConstAlarmProxy(
        holder_->discard_zero_rtt_decryption_keys_alarm()));
  }

  AlarmProxy multi_port_probing_alarm() {
    if (use_multiplexer_) {
      return AlarmProxy(QuicAlarmMultiplexer::AlarmProxy(
          &*multiplexer_, QuicAlarmSlot::kMultiPortProbing));
    }
    return AlarmProxy(QuicConnectionAlarmHolder::AlarmProxy(
        holder_->multi_port_probing_alarm()));
  }
  ConstAlarmProxy multi_port_probing_alarm() const {
    if (use_multiplexer_) {
      return ConstAlarmProxy(QuicAlarmMultiplexer::ConstAlarmProxy(
          &*multiplexer_, QuicAlarmSlot::kMultiPortProbing));
    }
    return ConstAlarmProxy(QuicConnectionAlarmHolder::ConstAlarmProxy(
        holder_->multi_port_probing_alarm()));
  }

  AlarmProxy idle_network_detector_alarm() {
    if (use_multiplexer_) {
      return AlarmProxy(QuicAlarmMultiplexer::AlarmProxy(
          &*multiplexer_, QuicAlarmSlot::kIdleNetworkDetector));
    }
    return AlarmProxy(QuicConnectionAlarmHolder::AlarmProxy(
        holder_->idle_network_detector_alarm()));
  }
  ConstAlarmProxy idle_network_detector_alarm() const {
    if (use_multiplexer_) {
      return ConstAlarmProxy(QuicAlarmMultiplexer::ConstAlarmProxy(
          &*multiplexer_, QuicAlarmSlot::kIdleNetworkDetector));
    }
    return ConstAlarmProxy(QuicConnectionAlarmHolder::ConstAlarmProxy(
        holder_->idle_network_detector_alarm()));
  }

  AlarmProxy network_blackhole_detector_alarm() {
    if (use_multiplexer_) {
      return AlarmProxy(QuicAlarmMultiplexer::AlarmProxy(
          &*multiplexer_, QuicAlarmSlot::kNetworkBlackholeDetector));
    }
    return AlarmProxy(QuicConnectionAlarmHolder::AlarmProxy(
        holder_->network_blackhole_detector_alarm()));
  }
  ConstAlarmProxy network_blackhole_detector_alarm() const {
    if (use_multiplexer_) {
      return ConstAlarmProxy(QuicAlarmMultiplexer::ConstAlarmProxy(
          &*multiplexer_, QuicAlarmSlot::kNetworkBlackholeDetector));
    }
    return ConstAlarmProxy(QuicConnectionAlarmHolder::ConstAlarmProxy(
        holder_->network_blackhole_detector_alarm()));
  }

  AlarmProxy ping_alarm() {
    if (use_multiplexer_) {
      return AlarmProxy(QuicAlarmMultiplexer::AlarmProxy(&*multiplexer_,
                                                         QuicAlarmSlot::kPing));
    }
    return AlarmProxy(
        QuicConnectionAlarmHolder::AlarmProxy(holder_->ping_alarm()));
  }
  ConstAlarmProxy ping_alarm() const {
    if (use_multiplexer_) {
      return ConstAlarmProxy(QuicAlarmMultiplexer::ConstAlarmProxy(
          &*multiplexer_, QuicAlarmSlot::kPing));
    }
    return ConstAlarmProxy(
        QuicConnectionAlarmHolder::ConstAlarmProxy(holder_->ping_alarm()));
  }

  void CancelAllAlarms() {
    if (use_multiplexer_) {
      multiplexer_->CancelAllAlarms();
    }
  }

  void DeferUnderlyingAlarmScheduling() {
    if (use_multiplexer_) {
      multiplexer_->DeferUnderlyingAlarmScheduling();
    }
  }
  void ResumeUnderlyingAlarmScheduling() {
    if (use_multiplexer_) {
      multiplexer_->ResumeUnderlyingAlarmScheduling();
    }
  }

 private:
  std::optional<QuicConnectionAlarmHolder> holder_;
  std::optional<QuicAlarmMultiplexer> multiplexer_;
  const bool use_multiplexer_;
};

using QuicAlarmProxy = QuicConnectionAlarms::AlarmProxy;
using QuicConstAlarmProxy = QuicConnectionAlarms::ConstAlarmProxy;

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_CONNECTION_ALARMS_H_
