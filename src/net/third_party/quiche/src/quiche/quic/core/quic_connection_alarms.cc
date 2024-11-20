// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_connection_alarms.h"

#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_connection_context.h"
#include "quiche/quic/core/quic_one_block_arena.h"
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

  void OnAlarm() override {
    connection_->OnSendAlarm();
  }
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

}  // namespace

QuicConnectionAlarms::QuicConnectionAlarms(
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

}  // namespace quic
