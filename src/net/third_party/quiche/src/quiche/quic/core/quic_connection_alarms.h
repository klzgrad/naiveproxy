// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_CONNECTION_ALARMS_H_
#define QUICHE_QUIC_CORE_QUIC_CONNECTION_ALARMS_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "absl/base/nullability.h"
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
  static constexpr size_t kNumberOfSlots =
      static_cast<size_t>(QuicAlarmSlot::kSlotCount);

  QuicAlarmMultiplexer(QuicConnectionAlarmsDelegate* /*absl_nonnull*/ connection,
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

// Proxy classes that allow an individual alarm to be accessed via
// a QuicAlarm-compatible API.
class QUICHE_EXPORT QuicAlarmProxy {
 public:
  QuicAlarmProxy(QuicAlarmMultiplexer* multiplexer, QuicAlarmSlot slot)
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

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_CONNECTION_ALARMS_H_
