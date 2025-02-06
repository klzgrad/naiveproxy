// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_connection_alarms.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/nullability.h"
#include "absl/container/inlined_vector.h"
#include "absl/strings/str_format.h"
#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_connection_context.h"
#include "quiche/quic/core/quic_one_block_arena.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quic {

namespace {

// Base class of all alarms owned by a QuicConnection.
class QuicConnectionAlarmDelegate : public QuicAlarm::Delegate {
 public:
  explicit QuicConnectionAlarmDelegate(QuicConnectionAlarmsDelegate* connection)
      : connection_(connection) {}
  QuicConnectionAlarmDelegate(const QuicConnectionAlarmDelegate&) = delete;
  QuicConnectionAlarmDelegate& operator=(const QuicConnectionAlarmDelegate&) =
      delete;

  QuicConnectionContext* GetConnectionContext() override {
    return (connection_ == nullptr) ? nullptr : connection_->context();
  }

 protected:
  QuicConnectionAlarmsDelegate* connection_;
};

// An alarm that is scheduled to send an ack if a timeout occurs.
class AckAlarmDelegate : public QuicConnectionAlarmDelegate {
 public:
  using QuicConnectionAlarmDelegate::QuicConnectionAlarmDelegate;

  void OnAlarm() override { connection_->OnAckAlarm(); }
};

// This alarm will be scheduled any time a data-bearing packet is sent out.
// When the alarm goes off, the connection checks to see if the oldest packets
// have been acked, and retransmit them if they have not.
class RetransmissionAlarmDelegate : public QuicConnectionAlarmDelegate {
 public:
  using QuicConnectionAlarmDelegate::QuicConnectionAlarmDelegate;

  void OnAlarm() override { connection_->OnRetransmissionAlarm(); }
};

// An alarm that is scheduled when the SentPacketManager requires a delay
// before sending packets and fires when the packet may be sent.
class SendAlarmDelegate : public QuicConnectionAlarmDelegate {
 public:
  using QuicConnectionAlarmDelegate::QuicConnectionAlarmDelegate;

  void OnAlarm() override { connection_->OnSendAlarm(); }
};

class MtuDiscoveryAlarmDelegate : public QuicConnectionAlarmDelegate {
 public:
  using QuicConnectionAlarmDelegate::QuicConnectionAlarmDelegate;

  void OnAlarm() override { connection_->OnMtuDiscoveryAlarm(); }
};

class ProcessUndecryptablePacketsAlarmDelegate
    : public QuicConnectionAlarmDelegate {
 public:
  using QuicConnectionAlarmDelegate::QuicConnectionAlarmDelegate;

  void OnAlarm() override { connection_->OnProcessUndecryptablePacketsAlarm(); }
};

class DiscardPreviousOneRttKeysAlarmDelegate
    : public QuicConnectionAlarmDelegate {
 public:
  using QuicConnectionAlarmDelegate::QuicConnectionAlarmDelegate;

  void OnAlarm() override { connection_->OnDiscardPreviousOneRttKeysAlarm(); }
};

class DiscardZeroRttDecryptionKeysAlarmDelegate
    : public QuicConnectionAlarmDelegate {
 public:
  using QuicConnectionAlarmDelegate::QuicConnectionAlarmDelegate;

  void OnAlarm() override {
    connection_->OnDiscardZeroRttDecryptionKeysAlarm();
  }
};

class MultiPortProbingAlarmDelegate : public QuicConnectionAlarmDelegate {
 public:
  using QuicConnectionAlarmDelegate::QuicConnectionAlarmDelegate;

  void OnAlarm() override {
    QUIC_DLOG(INFO) << "Alternative path probing alarm fired";
    connection_->MaybeProbeMultiPortPath();
  }
};

class IdleDetectorAlarmDelegate : public QuicConnectionAlarmDelegate {
 public:
  using QuicConnectionAlarmDelegate::QuicConnectionAlarmDelegate;

  IdleDetectorAlarmDelegate(const IdleDetectorAlarmDelegate&) = delete;
  IdleDetectorAlarmDelegate& operator=(const IdleDetectorAlarmDelegate&) =
      delete;

  void OnAlarm() override { connection_->OnIdleDetectorAlarm(); }
};

class NetworkBlackholeDetectorAlarmDelegate
    : public QuicConnectionAlarmDelegate {
 public:
  using QuicConnectionAlarmDelegate::QuicConnectionAlarmDelegate;

  NetworkBlackholeDetectorAlarmDelegate(
      const NetworkBlackholeDetectorAlarmDelegate&) = delete;
  NetworkBlackholeDetectorAlarmDelegate& operator=(
      const NetworkBlackholeDetectorAlarmDelegate&) = delete;

  void OnAlarm() override { connection_->OnNetworkBlackholeDetectorAlarm(); }
};

class PingAlarmDelegate : public QuicConnectionAlarmDelegate {
 public:
  using QuicConnectionAlarmDelegate::QuicConnectionAlarmDelegate;

  PingAlarmDelegate(const PingAlarmDelegate&) = delete;
  PingAlarmDelegate& operator=(const PingAlarmDelegate&) = delete;

  void OnAlarm() override { connection_->OnPingAlarm(); }
};

class MultiplexerAlarmDelegate : public QuicAlarm::Delegate {
 public:
  explicit MultiplexerAlarmDelegate(QuicAlarmMultiplexer* multiplexer)
      : multiplexer_(multiplexer) {}
  MultiplexerAlarmDelegate(const QuicConnectionAlarmDelegate&) = delete;
  MultiplexerAlarmDelegate& operator=(const MultiplexerAlarmDelegate&) = delete;

  QuicConnectionContext* GetConnectionContext() override {
    return multiplexer_->delegate()->context();
  }

  void OnAlarm() override { multiplexer_->FireAlarms(); }

 protected:
  QuicAlarmMultiplexer* multiplexer_;
};

}  // namespace

std::string QuicAlarmSlotName(QuicAlarmSlot slot) {
  switch (slot) {
    case QuicAlarmSlot::kAck:
      return "Ack";
    case QuicAlarmSlot::kRetransmission:
      return "Retransmission";
    case QuicAlarmSlot::kSend:
      return "Send";
    case QuicAlarmSlot::kMtuDiscovery:
      return "MtuDiscovery";
    case QuicAlarmSlot::kProcessUndecryptablePackets:
      return "ProcessUndecryptablePackets";
    case QuicAlarmSlot::kDiscardPreviousOneRttKeys:
      return "DiscardPreviousOneRttKeys";
    case QuicAlarmSlot::kDiscardZeroRttDecryptionKeys:
      return "DiscardZeroRttDecryptionKeys";
    case QuicAlarmSlot::kMultiPortProbing:
      return "MultiPortProbing";
    case QuicAlarmSlot::kIdleNetworkDetector:
      return "IdleNetworkDetector";
    case QuicAlarmSlot::kNetworkBlackholeDetector:
      return "NetworkBlackholeDetector";
    case QuicAlarmSlot::kPing:
      return "Ping";
    case QuicAlarmSlot::kSlotCount:
      break;
  }
  return "[unknown]";
}

QuicAlarmMultiplexer::QuicAlarmMultiplexer(
    absl::Nonnull<QuicConnectionAlarmsDelegate*> connection,
    QuicConnectionArena& arena, QuicAlarmFactory& alarm_factory)
    : deadlines_({QuicTime::Zero(), QuicTime::Zero(), QuicTime::Zero(),
                  QuicTime::Zero(), QuicTime::Zero(), QuicTime::Zero(),
                  QuicTime::Zero(), QuicTime::Zero(), QuicTime::Zero(),
                  QuicTime::Zero(), QuicTime::Zero()}),
      now_alarm_(alarm_factory.CreateAlarm(
          arena.New<MultiplexerAlarmDelegate>(this), &arena)),
      later_alarm_(alarm_factory.CreateAlarm(
          arena.New<MultiplexerAlarmDelegate>(this), &arena)),
      connection_(connection),
      underlying_alarm_granularity_(QuicTimeDelta::FromMicroseconds(
          GetQuicFlag(quic_multiplexer_alarm_granularity_us))) {}

void QuicAlarmMultiplexer::Set(QuicAlarmSlot slot, QuicTime new_deadline) {
  QUICHE_DCHECK(!IsSet(slot));
  QUICHE_DCHECK(new_deadline.IsInitialized());
  if (permanently_cancelled_) {
    QUICHE_BUG(quic_alarm_multiplexer_illegal_set)
        << "Set called after alarms are permanently cancelled. new_deadline:"
        << new_deadline;
    return;
  }
  SetDeadlineFor(slot, new_deadline);
  MaybeRescheduleUnderlyingAlarms();
}

void QuicAlarmMultiplexer::Update(QuicAlarmSlot slot, QuicTime new_deadline,
                                  QuicTimeDelta granularity) {
  if (permanently_cancelled_) {
    QUICHE_BUG(quic_alarm_multiplexer_illegal_update)
        << "Update called after alarm is permanently cancelled. new_deadline:"
        << new_deadline << ", granularity:" << granularity;
    return;
  }

  if (!new_deadline.IsInitialized()) {
    Cancel(slot);
    return;
  }
  if (std::abs((new_deadline - GetDeadline(slot)).ToMicroseconds()) <
      granularity.ToMicroseconds()) {
    return;
  }
  SetDeadlineFor(slot, new_deadline);
  MaybeRescheduleUnderlyingAlarms();
}

void QuicAlarmMultiplexer::DeferUnderlyingAlarmScheduling() {
  defer_updates_of_underlying_alarms_ = true;
}

void QuicAlarmMultiplexer::ResumeUnderlyingAlarmScheduling() {
  QUICHE_DCHECK(defer_updates_of_underlying_alarms_);
  defer_updates_of_underlying_alarms_ = false;
  RescheduleUnderlyingAlarms();
}

void QuicAlarmMultiplexer::FireAlarms() {
  if (permanently_cancelled_) {
    QUICHE_BUG(multiplexer_fire_alarms_permanently_cancelled)
        << "FireAlarms() called when all alarms have been permanently "
           "cancelled.";
    return;
  }

  QuicTime now = connection_->clock()->ApproximateNow();

  // Create a fixed list of alarms that are due.
  absl::InlinedVector<QuicAlarmSlot, kNumberOfSlots> scheduled;
  for (size_t slot_number = 0; slot_number < deadlines_.size(); ++slot_number) {
    if (deadlines_[slot_number].IsInitialized() &&
        deadlines_[slot_number] <= now) {
      scheduled.push_back(static_cast<QuicAlarmSlot>(slot_number));
    }
  }

  // Execute them in order of scheduled deadlines.
  absl::c_sort(scheduled, [this](QuicAlarmSlot a, QuicAlarmSlot b) {
    return GetDeadline(a) < GetDeadline(b);
  });
  for (QuicAlarmSlot slot : scheduled) {
    Fire(slot);
  }
  MaybeRescheduleUnderlyingAlarms();
}

void QuicAlarmMultiplexer::RescheduleUnderlyingAlarms() {
  if (permanently_cancelled_) {
    return;
  }

  QuicTime now = connection_->clock()->ApproximateNow();
  bool schedule_now = false;
  QuicTime later_alarm_deadline = QuicTime::Infinite();
  for (const QuicTime& deadline : deadlines_) {
    if (!deadline.IsInitialized()) {
      continue;
    }
    if (deadline <= now) {
      schedule_now = true;
    } else {
      later_alarm_deadline = std::min(later_alarm_deadline, deadline);
    }
  }

  if (schedule_now && !now_alarm_->IsSet()) {
    now_alarm_->Set(now);
  }
  if (!schedule_now && now_alarm_->IsSet()) {
    now_alarm_->Cancel();
  }

  if (later_alarm_deadline != QuicTime::Infinite()) {
    later_alarm_->Update(later_alarm_deadline, underlying_alarm_granularity_);
  } else {
    later_alarm_->Cancel();
  }

  QUICHE_DVLOG(1) << "Rescheduled alarms; now = "
                  << (schedule_now ? "true" : "false")
                  << "; later = " << later_alarm_deadline;
  QUICHE_DVLOG(1) << "Alarms: " << DebugString();
}

void QuicAlarmMultiplexer::Fire(QuicAlarmSlot slot) {
  if (!IsSet(slot)) {
    return;
  }
  SetDeadlineFor(slot, QuicTime::Zero());

  switch (slot) {
    case QuicAlarmSlot::kAck:
      connection_->OnAckAlarm();
      return;
    case QuicAlarmSlot::kRetransmission:
      connection_->OnRetransmissionAlarm();
      return;
    case QuicAlarmSlot::kSend:
      connection_->OnSendAlarm();
      return;
    case QuicAlarmSlot::kMtuDiscovery:
      connection_->OnMtuDiscoveryAlarm();
      return;
    case QuicAlarmSlot::kProcessUndecryptablePackets:
      connection_->OnProcessUndecryptablePacketsAlarm();
      return;
    case QuicAlarmSlot::kDiscardPreviousOneRttKeys:
      connection_->OnDiscardPreviousOneRttKeysAlarm();
      return;
    case QuicAlarmSlot::kDiscardZeroRttDecryptionKeys:
      connection_->OnDiscardZeroRttDecryptionKeysAlarm();
      return;
    case QuicAlarmSlot::kMultiPortProbing:
      connection_->MaybeProbeMultiPortPath();
      return;
    case QuicAlarmSlot::kIdleNetworkDetector:
      connection_->OnIdleDetectorAlarm();
      return;
    case QuicAlarmSlot::kNetworkBlackholeDetector:
      connection_->OnNetworkBlackholeDetectorAlarm();
      return;
    case QuicAlarmSlot::kPing:
      connection_->OnPingAlarm();
      return;
    case QuicAlarmSlot::kSlotCount:
      break;
  }
  QUICHE_NOTREACHED();
}

std::string QuicAlarmMultiplexer::DebugString() {
  std::vector<std::pair<QuicTime, QuicAlarmSlot>> scheduled;
  for (size_t i = 0; i < deadlines_.size(); ++i) {
    if (deadlines_[i].IsInitialized()) {
      scheduled.emplace_back(deadlines_[i], static_cast<QuicAlarmSlot>(i));
    }
  }
  absl::c_sort(scheduled);

  QuicTime now = connection_->clock()->Now();
  std::string result;
  for (const auto& [deadline, slot] : scheduled) {
    QuicTimeDelta relative = deadline - now;
    absl::StrAppendFormat(&result, "        %.1fms --- %s\n",
                          relative.ToMicroseconds() / 1000.f,
                          QuicAlarmSlotName(slot));
  }
  return result;
}

void QuicAlarmMultiplexer::CancelAllAlarms() {
  QUICHE_DVLOG(1) << "Cancelling all QuicConnection alarms.";
  permanently_cancelled_ = true;
  deadlines_.fill(QuicTime::Zero());
  now_alarm_->PermanentCancel();
  later_alarm_->PermanentCancel();
}

QuicConnectionAlarmHolder::QuicConnectionAlarmHolder(
    QuicConnectionAlarmsDelegate* delegate, QuicAlarmFactory& alarm_factory,
    QuicConnectionArena& arena)
    : ack_alarm_(alarm_factory.CreateAlarm(
          arena.New<AckAlarmDelegate>(delegate), &arena)),
      retransmission_alarm_(alarm_factory.CreateAlarm(
          arena.New<RetransmissionAlarmDelegate>(delegate), &arena)),
      send_alarm_(alarm_factory.CreateAlarm(
          arena.New<SendAlarmDelegate>(delegate), &arena)),
      mtu_discovery_alarm_(alarm_factory.CreateAlarm(
          arena.New<MtuDiscoveryAlarmDelegate>(delegate), &arena)),
      process_undecryptable_packets_alarm_(alarm_factory.CreateAlarm(
          arena.New<ProcessUndecryptablePacketsAlarmDelegate>(delegate),
          &arena)),
      discard_previous_one_rtt_keys_alarm_(alarm_factory.CreateAlarm(
          arena.New<DiscardPreviousOneRttKeysAlarmDelegate>(delegate), &arena)),
      discard_zero_rtt_decryption_keys_alarm_(alarm_factory.CreateAlarm(
          arena.New<DiscardZeroRttDecryptionKeysAlarmDelegate>(delegate),
          &arena)),
      multi_port_probing_alarm_(alarm_factory.CreateAlarm(
          arena.New<MultiPortProbingAlarmDelegate>(delegate), &arena)),
      idle_network_detector_alarm_(alarm_factory.CreateAlarm(
          arena.New<IdleDetectorAlarmDelegate>(delegate), &arena)),
      network_blackhole_detector_alarm_(alarm_factory.CreateAlarm(
          arena.New<NetworkBlackholeDetectorAlarmDelegate>(delegate), &arena)),
      ping_alarm_(alarm_factory.CreateAlarm(
          arena.New<PingAlarmDelegate>(delegate), &arena)) {}

QuicConnectionAlarms::QuicConnectionAlarms(
    QuicConnectionAlarmsDelegate* delegate, QuicAlarmFactory& alarm_factory,
    QuicConnectionArena& arena)
    : use_multiplexer_(GetQuicReloadableFlag(quic_use_alarm_multiplexer)) {
  if (use_multiplexer_) {
    multiplexer_.emplace(delegate, arena, alarm_factory);
  } else {
    holder_.emplace(delegate, alarm_factory, arena);
  }
}
}  // namespace quic
