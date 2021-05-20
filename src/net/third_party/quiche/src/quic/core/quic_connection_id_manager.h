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
#include "quic/core/frames/quic_new_connection_id_frame.h"
#include "quic/core/frames/quic_retire_connection_id_frame.h"
#include "quic/core/quic_alarm.h"
#include "quic/core/quic_alarm_factory.h"
#include "quic/core/quic_clock.h"
#include "quic/core/quic_connection_id.h"
#include "quic/core/quic_interval_set.h"
#include "quic/platform/api/quic_export.h"
#include "quic/platform/api/quic_uint128.h"

namespace quic {

struct QUIC_EXPORT_PRIVATE QuicConnectionIdData {
  QuicConnectionIdData(const QuicConnectionId& connection_id,
                       uint64_t sequence_number,
                       QuicUint128 stateless_reset_token);

  QuicConnectionId connection_id;
  uint64_t sequence_number;
  QuicUint128 stateless_reset_token;
};

// Used by QuicSelfIssuedConnectionIdManager
// and QuicPeerIssuedConnectionIdManager.
class QUIC_EXPORT_PRIVATE QuicConnectionIdManagerVisitorInterface {
 public:
  virtual ~QuicConnectionIdManagerVisitorInterface() = default;
  virtual void OnPeerIssuedConnectionIdRetired() = 0;
  virtual bool SendNewConnectionId(const QuicNewConnectionIdFrame& frame) = 0;
  virtual void OnNewConnectionIdIssued(
      const QuicConnectionId& connection_id) = 0;
  virtual void OnSelfIssuedConnectionIdRetired(
      const QuicConnectionId& connection_id) = 0;
};

class QUIC_EXPORT_PRIVATE QuicPeerIssuedConnectionIdManager {
 public:
  // QuicPeerIssuedConnectionIdManager should be instantiated only when a peer
  // issued-non empty connection ID is received.
  QuicPeerIssuedConnectionIdManager(
      size_t active_connection_id_limit,
      const QuicConnectionId& initial_peer_issued_connection_id,
      const QuicClock* clock,
      QuicAlarmFactory* alarm_factory,
      QuicConnectionIdManagerVisitorInterface* visitor);

  ~QuicPeerIssuedConnectionIdManager();

  QuicErrorCode OnNewConnectionIdFrame(const QuicNewConnectionIdFrame& frame,
                                       std::string* error_detail);

  // Returns the data associated with an unused connection Id. After the call,
  // the Id is marked as used. Returns nullptr if there is no unused connection
  // Id.
  const QuicConnectionIdData* ConsumeOneUnusedConnectionId();

  // Add the connection Id to the pending retirement connection Id list.
  void PrepareToRetireActiveConnectionId(const QuicConnectionId& cid);

  bool IsConnectionIdActive(const QuicConnectionId& cid) const;

  // Get the sequence numbers of all the connection Ids pending retirement when
  // it is safe to retires these Ids.
  std::vector<uint64_t> ConsumeToBeRetiredConnectionIdSequenceNumbers();

  // If old_connection_id is still tracked by QuicPeerIssuedConnectionIdManager,
  // replace it with new_connection_id. Otherwise, this is a no-op.
  void ReplaceConnectionId(const QuicConnectionId& old_connection_id,
                           const QuicConnectionId& new_connection_id);

 private:
  friend class QuicConnectionIdManagerPeer;

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

class QUIC_EXPORT_PRIVATE QuicSelfIssuedConnectionIdManager {
 public:
  QuicSelfIssuedConnectionIdManager(
      size_t active_connection_id_limit,
      const QuicConnectionId& initial_connection_id,
      const QuicClock* clock,
      QuicAlarmFactory* alarm_factory,
      QuicConnectionIdManagerVisitorInterface* visitor);

  virtual ~QuicSelfIssuedConnectionIdManager();

  QuicNewConnectionIdFrame IssueNewConnectionIdForPreferredAddress();

  QuicErrorCode OnRetireConnectionIdFrame(
      const QuicRetireConnectionIdFrame& frame,
      QuicTime::Delta pto_delay,
      std::string* error_detail);

  std::vector<QuicConnectionId> GetUnretiredConnectionIds() const;

  // Called when the retire_connection_id alarm_ fires. Removes the to be
  // retired connection ID locally.
  void RetireConnectionId();

  // Sends new connection IDs if more can be sent.
  void MaybeSendNewConnectionIds();

  virtual QuicConnectionId GenerateNewConnectionId(
      const QuicConnectionId& old_connection_id) const;

 private:
  friend class QuicConnectionIdManagerPeer;

  QuicNewConnectionIdFrame IssueNewConnectionId();

  // This should be set to the min of:
  // (1) # of connection atcive IDs that peer can maintain.
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
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_CONNECTION_ID_MANAGER_H_
