// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// QuicPeerIssuedConnectionIdManager handles the states associated with receving
// and retiring peer issued connection Ids.
// QuicSelfIssuedConnectionIdManager handles the states associated with
// connection Ids issued by the current end point.

#ifndef QUICHE_QUIC_CORE_QUIC_CONNECTION_ID_MANAGER_H_
#define QUICHE_QUIC_CORE_QUIC_CONNECTION_ID_MANAGER_H_

#include <cstddef>
#include <memory>

#include "absl/types/optional.h"
#include "quiche/quic/core/connection_id_generator.h"
#include "quiche/quic/core/frames/quic_new_connection_id_frame.h"
#include "quiche/quic/core/frames/quic_retire_connection_id_frame.h"
#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_interval_set.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

namespace test {
class QuicConnectionIdManagerPeer;
}  // namespace test

struct QUICHE_EXPORT QuicConnectionIdData {
  QuicConnectionIdData(const QuicConnectionId& connection_id,
                       uint64_t sequence_number,
                       const StatelessResetToken& stateless_reset_token);

  QuicConnectionId connection_id;
  uint64_t sequence_number;
  StatelessResetToken stateless_reset_token;
};

// Used by QuicSelfIssuedConnectionIdManager
// and QuicPeerIssuedConnectionIdManager.
class QUICHE_EXPORT QuicConnectionIdManagerVisitorInterface {
 public:
  virtual ~QuicConnectionIdManagerVisitorInterface() = default;
  virtual void OnPeerIssuedConnectionIdRetired() = 0;
  virtual bool SendNewConnectionId(const QuicNewConnectionIdFrame& frame) = 0;
  virtual bool MaybeReserveConnectionId(
      const QuicConnectionId& connection_id) = 0;
  virtual void OnSelfIssuedConnectionIdRetired(
      const QuicConnectionId& connection_id) = 0;
};

class QUICHE_EXPORT QuicPeerIssuedConnectionIdManager {
 public:
  // QuicPeerIssuedConnectionIdManager should be instantiated only when a peer
  // issued-non empty connection ID is received.
  QuicPeerIssuedConnectionIdManager(
      size_t active_connection_id_limit,
      const QuicConnectionId& initial_peer_issued_connection_id,
      const QuicClock* clock, QuicAlarmFactory* alarm_factory,
      QuicConnectionIdManagerVisitorInterface* visitor,
      QuicConnectionContext* context);

  ~QuicPeerIssuedConnectionIdManager();

  QuicErrorCode OnNewConnectionIdFrame(const QuicNewConnectionIdFrame& frame,
                                       std::string* error_detail,
                                       bool* is_duplicate_frame);

  bool HasUnusedConnectionId() const {
    return !unused_connection_id_data_.empty();
  }

  // Returns the data associated with an unused connection Id. After the call,
  // the Id is marked as used. Returns nullptr if there is no unused connection
  // Id.
  const QuicConnectionIdData* ConsumeOneUnusedConnectionId();

  // Add each active connection Id that is no longer on path to the pending
  // retirement connection Id list.
  void MaybeRetireUnusedConnectionIds(
      const std::vector<QuicConnectionId>& active_connection_ids_on_path);

  bool IsConnectionIdActive(const QuicConnectionId& cid) const;

  // Get the sequence numbers of all the connection Ids pending retirement when
  // it is safe to retires these Ids.
  std::vector<uint64_t> ConsumeToBeRetiredConnectionIdSequenceNumbers();

  // If old_connection_id is still tracked by QuicPeerIssuedConnectionIdManager,
  // replace it with new_connection_id. Otherwise, this is a no-op.
  void ReplaceConnectionId(const QuicConnectionId& old_connection_id,
                           const QuicConnectionId& new_connection_id);

 private:
  friend class test::QuicConnectionIdManagerPeer;

  // Add the connection Id to the pending retirement connection Id list and
  // schedule an alarm if needed.
  void PrepareToRetireActiveConnectionId(const QuicConnectionId& cid);

  bool IsConnectionIdNew(const QuicNewConnectionIdFrame& frame);

  void PrepareToRetireConnectionIdPriorTo(
      uint64_t retire_prior_to,
      std::vector<QuicConnectionIdData>* cid_data_vector);

  size_t active_connection_id_limit_;
  const QuicClock* clock_;
  std::unique_ptr<QuicAlarm> retire_connection_id_alarm_;
  std::vector<QuicConnectionIdData> active_connection_id_data_;
  std::vector<QuicConnectionIdData> unused_connection_id_data_;
  std::vector<QuicConnectionIdData> to_be_retired_connection_id_data_;
  // Track sequence numbers of recent NEW_CONNECTION_ID frames received from
  // the peer.
  QuicIntervalSet<uint64_t> recent_new_connection_id_sequence_numbers_;
  uint64_t max_new_connection_id_frame_retire_prior_to_ = 0u;
};

class QUICHE_EXPORT QuicSelfIssuedConnectionIdManager {
 public:
  QuicSelfIssuedConnectionIdManager(
      size_t active_connection_id_limit,
      const QuicConnectionId& initial_connection_id, const QuicClock* clock,
      QuicAlarmFactory* alarm_factory,
      QuicConnectionIdManagerVisitorInterface* visitor,
      QuicConnectionContext* context,
      ConnectionIdGeneratorInterface& generator);

  virtual ~QuicSelfIssuedConnectionIdManager();

  absl::optional<QuicNewConnectionIdFrame>
  MaybeIssueNewConnectionIdForPreferredAddress();

  QuicErrorCode OnRetireConnectionIdFrame(
      const QuicRetireConnectionIdFrame& frame, QuicTime::Delta pto_delay,
      std::string* error_detail);

  std::vector<QuicConnectionId> GetUnretiredConnectionIds() const;

  QuicConnectionId GetOneActiveConnectionId() const;

  // Called when the retire_connection_id alarm_ fires. Removes the to be
  // retired connection ID locally.
  void RetireConnectionId();

  // Sends new connection IDs if more can be sent.
  void MaybeSendNewConnectionIds();

  // The two functions are called on the client side to associate a client
  // connection ID with a new probing/migration path when client uses
  // non-empty connection ID.
  bool HasConnectionIdToConsume() const;
  absl::optional<QuicConnectionId> ConsumeOneConnectionId();

  // Returns true if the given connection ID is issued by the
  // QuicSelfIssuedConnectionIdManager and not retired locally yet. Called to
  // tell if a received packet has a valid connection ID.
  bool IsConnectionIdInUse(const QuicConnectionId& cid) const;

 private:
  friend class test::QuicConnectionIdManagerPeer;

  // Issue a new connection ID. Can return nullopt.
  absl::optional<QuicNewConnectionIdFrame> MaybeIssueNewConnectionId();

  // This should be set to the min of:
  // (1) # of active connection IDs that peer can maintain.
  // (2) maximum # of active connection IDs self plans to issue.
  size_t active_connection_id_limit_;
  const QuicClock* clock_;
  QuicConnectionIdManagerVisitorInterface* visitor_;
  // This tracks connection IDs issued to the peer but not retired by the peer.
  // Each pair is a connection ID and its sequence number.
  std::vector<std::pair<QuicConnectionId, uint64_t>> active_connection_ids_;
  // This tracks connection IDs retired by the peer but has not been retired
  // locally. Each pair is a connection ID and the time by which it should be
  // retired.
  std::vector<std::pair<QuicConnectionId, QuicTime>>
      to_be_retired_connection_ids_;
  // An alarm that fires when a connection ID should be retired.
  std::unique_ptr<QuicAlarm> retire_connection_id_alarm_;
  // State of the last issued connection Id.
  QuicConnectionId last_connection_id_;
  uint64_t next_connection_id_sequence_number_;
  // The sequence number of last connection ID consumed.
  uint64_t last_connection_id_consumed_by_self_sequence_number_;

  ConnectionIdGeneratorInterface& connection_id_generator_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_CONNECTION_ID_MANAGER_H_
