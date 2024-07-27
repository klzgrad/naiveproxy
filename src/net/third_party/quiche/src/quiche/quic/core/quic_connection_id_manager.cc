// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_connection_id_manager.h"

#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quic {

QuicConnectionIdData::QuicConnectionIdData(
    const QuicConnectionId& connection_id, uint64_t sequence_number,
    const StatelessResetToken& stateless_reset_token)
    : connection_id(connection_id),
      sequence_number(sequence_number),
      stateless_reset_token(stateless_reset_token) {}

namespace {

class RetirePeerIssuedConnectionIdAlarm
    : public QuicAlarm::DelegateWithContext {
 public:
  explicit RetirePeerIssuedConnectionIdAlarm(
      QuicConnectionIdManagerVisitorInterface* visitor,
      QuicConnectionContext* context)
      : QuicAlarm::DelegateWithContext(context), visitor_(visitor) {}
  RetirePeerIssuedConnectionIdAlarm(const RetirePeerIssuedConnectionIdAlarm&) =
      delete;
  RetirePeerIssuedConnectionIdAlarm& operator=(
      const RetirePeerIssuedConnectionIdAlarm&) = delete;

  void OnAlarm() override { visitor_->OnPeerIssuedConnectionIdRetired(); }

 private:
  QuicConnectionIdManagerVisitorInterface* visitor_;
};

std::vector<QuicConnectionIdData>::const_iterator FindConnectionIdData(
    const std::vector<QuicConnectionIdData>& cid_data_vector,
    const QuicConnectionId& cid) {
  return std::find_if(cid_data_vector.begin(), cid_data_vector.end(),
                      [&cid](const QuicConnectionIdData& cid_data) {
                        return cid == cid_data.connection_id;
                      });
}

std::vector<QuicConnectionIdData>::iterator FindConnectionIdData(
    std::vector<QuicConnectionIdData>* cid_data_vector,
    const QuicConnectionId& cid) {
  return std::find_if(cid_data_vector->begin(), cid_data_vector->end(),
                      [&cid](const QuicConnectionIdData& cid_data) {
                        return cid == cid_data.connection_id;
                      });
}

}  // namespace

QuicPeerIssuedConnectionIdManager::QuicPeerIssuedConnectionIdManager(
    size_t active_connection_id_limit,
    const QuicConnectionId& initial_peer_issued_connection_id,
    const QuicClock* clock, QuicAlarmFactory* alarm_factory,
    QuicConnectionIdManagerVisitorInterface* visitor,
    QuicConnectionContext* context)
    : active_connection_id_limit_(active_connection_id_limit),
      clock_(clock),
      retire_connection_id_alarm_(alarm_factory->CreateAlarm(
          new RetirePeerIssuedConnectionIdAlarm(visitor, context))) {
  QUICHE_DCHECK_GE(active_connection_id_limit_, 2u);
  QUICHE_DCHECK(!initial_peer_issued_connection_id.IsEmpty());
  active_connection_id_data_.emplace_back<const QuicConnectionId&, uint64_t,
                                          const StatelessResetToken&>(
      initial_peer_issued_connection_id,
      /*sequence_number=*/0u, {});
  recent_new_connection_id_sequence_numbers_.Add(0u, 1u);
}

QuicPeerIssuedConnectionIdManager::~QuicPeerIssuedConnectionIdManager() {
  retire_connection_id_alarm_->Cancel();
}

bool QuicPeerIssuedConnectionIdManager::IsConnectionIdNew(
    const QuicNewConnectionIdFrame& frame) {
  auto is_old_connection_id = [&frame](const QuicConnectionIdData& cid_data) {
    return cid_data.connection_id == frame.connection_id;
  };
  if (std::any_of(active_connection_id_data_.begin(),
                  active_connection_id_data_.end(), is_old_connection_id)) {
    return false;
  }
  if (std::any_of(unused_connection_id_data_.begin(),
                  unused_connection_id_data_.end(), is_old_connection_id)) {
    return false;
  }
  if (std::any_of(to_be_retired_connection_id_data_.begin(),
                  to_be_retired_connection_id_data_.end(),
                  is_old_connection_id)) {
    return false;
  }
  return true;
}

void QuicPeerIssuedConnectionIdManager::PrepareToRetireConnectionIdPriorTo(
    uint64_t retire_prior_to,
    std::vector<QuicConnectionIdData>* cid_data_vector) {
  auto it2 = cid_data_vector->begin();
  for (auto it = cid_data_vector->begin(); it != cid_data_vector->end(); ++it) {
    if (it->sequence_number >= retire_prior_to) {
      *it2++ = *it;
    } else {
      to_be_retired_connection_id_data_.push_back(*it);
      if (!retire_connection_id_alarm_->IsSet()) {
        retire_connection_id_alarm_->Set(clock_->ApproximateNow());
      }
    }
  }
  cid_data_vector->erase(it2, cid_data_vector->end());
}

QuicErrorCode QuicPeerIssuedConnectionIdManager::OnNewConnectionIdFrame(
    const QuicNewConnectionIdFrame& frame, std::string* error_detail,
    bool* is_duplicate_frame) {
  if (recent_new_connection_id_sequence_numbers_.Contains(
          frame.sequence_number)) {
    // This frame has a recently seen sequence number. Ignore.
    *is_duplicate_frame = true;
    return QUIC_NO_ERROR;
  }
  if (!IsConnectionIdNew(frame)) {
    *error_detail =
        "Received a NEW_CONNECTION_ID frame that reuses a previously seen Id.";
    return IETF_QUIC_PROTOCOL_VIOLATION;
  }

  recent_new_connection_id_sequence_numbers_.AddOptimizedForAppend(
      frame.sequence_number, frame.sequence_number + 1);

  if (recent_new_connection_id_sequence_numbers_.Size() >
      kMaxNumConnectionIdSequenceNumberIntervals) {
    *error_detail =
        "Too many disjoint connection Id sequence number intervals.";
    return IETF_QUIC_PROTOCOL_VIOLATION;
  }

  // QuicFramer::ProcessNewConnectionIdFrame guarantees that
  // frame.sequence_number >= frame.retire_prior_to, and hence there is no need
  // to check that.
  if (frame.sequence_number < max_new_connection_id_frame_retire_prior_to_) {
    // Later frames have asked for retirement of the current frame.
    to_be_retired_connection_id_data_.emplace_back(frame.connection_id,
                                                   frame.sequence_number,
                                                   frame.stateless_reset_token);
    if (!retire_connection_id_alarm_->IsSet()) {
      retire_connection_id_alarm_->Set(clock_->ApproximateNow());
    }
    return QUIC_NO_ERROR;
  }
  if (frame.retire_prior_to > max_new_connection_id_frame_retire_prior_to_) {
    max_new_connection_id_frame_retire_prior_to_ = frame.retire_prior_to;
    PrepareToRetireConnectionIdPriorTo(frame.retire_prior_to,
                                       &active_connection_id_data_);
    PrepareToRetireConnectionIdPriorTo(frame.retire_prior_to,
                                       &unused_connection_id_data_);
  }

  if (active_connection_id_data_.size() + unused_connection_id_data_.size() >=
      active_connection_id_limit_) {
    *error_detail = "Peer provides more connection IDs than the limit.";
    return QUIC_CONNECTION_ID_LIMIT_ERROR;
  }

  unused_connection_id_data_.emplace_back(
      frame.connection_id, frame.sequence_number, frame.stateless_reset_token);
  return QUIC_NO_ERROR;
}

const QuicConnectionIdData*
QuicPeerIssuedConnectionIdManager::ConsumeOneUnusedConnectionId() {
  if (unused_connection_id_data_.empty()) {
    return nullptr;
  }
  active_connection_id_data_.push_back(unused_connection_id_data_.back());
  unused_connection_id_data_.pop_back();
  return &active_connection_id_data_.back();
}

void QuicPeerIssuedConnectionIdManager::PrepareToRetireActiveConnectionId(
    const QuicConnectionId& cid) {
  auto it = FindConnectionIdData(active_connection_id_data_, cid);
  if (it == active_connection_id_data_.end()) {
    // The cid has already been retired.
    return;
  }
  to_be_retired_connection_id_data_.push_back(*it);
  active_connection_id_data_.erase(it);
  if (!retire_connection_id_alarm_->IsSet()) {
    retire_connection_id_alarm_->Set(clock_->ApproximateNow());
  }
}

void QuicPeerIssuedConnectionIdManager::MaybeRetireUnusedConnectionIds(
    const std::vector<QuicConnectionId>& active_connection_ids_on_path) {
  std::vector<QuicConnectionId> cids_to_retire;
  for (const auto& cid_data : active_connection_id_data_) {
    if (std::find(active_connection_ids_on_path.begin(),
                  active_connection_ids_on_path.end(),
                  cid_data.connection_id) ==
        active_connection_ids_on_path.end()) {
      cids_to_retire.push_back(cid_data.connection_id);
    }
  }
  for (const auto& cid : cids_to_retire) {
    PrepareToRetireActiveConnectionId(cid);
  }
}

bool QuicPeerIssuedConnectionIdManager::IsConnectionIdActive(
    const QuicConnectionId& cid) const {
  return FindConnectionIdData(active_connection_id_data_, cid) !=
         active_connection_id_data_.end();
}

std::vector<uint64_t> QuicPeerIssuedConnectionIdManager::
    ConsumeToBeRetiredConnectionIdSequenceNumbers() {
  std::vector<uint64_t> result;
  for (auto const& cid_data : to_be_retired_connection_id_data_) {
    result.push_back(cid_data.sequence_number);
  }
  to_be_retired_connection_id_data_.clear();
  return result;
}

void QuicPeerIssuedConnectionIdManager::ReplaceConnectionId(
    const QuicConnectionId& old_connection_id,
    const QuicConnectionId& new_connection_id) {
  auto it1 =
      FindConnectionIdData(&active_connection_id_data_, old_connection_id);
  if (it1 != active_connection_id_data_.end()) {
    it1->connection_id = new_connection_id;
    return;
  }
  auto it2 = FindConnectionIdData(&to_be_retired_connection_id_data_,
                                  old_connection_id);
  if (it2 != to_be_retired_connection_id_data_.end()) {
    it2->connection_id = new_connection_id;
  }
}

namespace {

class RetireSelfIssuedConnectionIdAlarmDelegate
    : public QuicAlarm::DelegateWithContext {
 public:
  explicit RetireSelfIssuedConnectionIdAlarmDelegate(
      QuicSelfIssuedConnectionIdManager* connection_id_manager,
      QuicConnectionContext* context)
      : QuicAlarm::DelegateWithContext(context),
        connection_id_manager_(connection_id_manager) {}
  RetireSelfIssuedConnectionIdAlarmDelegate(
      const RetireSelfIssuedConnectionIdAlarmDelegate&) = delete;
  RetireSelfIssuedConnectionIdAlarmDelegate& operator=(
      const RetireSelfIssuedConnectionIdAlarmDelegate&) = delete;

  void OnAlarm() override { connection_id_manager_->RetireConnectionId(); }

 private:
  QuicSelfIssuedConnectionIdManager* connection_id_manager_;
};

}  // namespace

QuicSelfIssuedConnectionIdManager::QuicSelfIssuedConnectionIdManager(
    size_t active_connection_id_limit,
    const QuicConnectionId& initial_connection_id, const QuicClock* clock,
    QuicAlarmFactory* alarm_factory,
    QuicConnectionIdManagerVisitorInterface* visitor,
    QuicConnectionContext* context, ConnectionIdGeneratorInterface& generator)
    : active_connection_id_limit_(active_connection_id_limit),
      clock_(clock),
      visitor_(visitor),
      retire_connection_id_alarm_(alarm_factory->CreateAlarm(
          new RetireSelfIssuedConnectionIdAlarmDelegate(this, context))),
      last_connection_id_(initial_connection_id),
      next_connection_id_sequence_number_(1u),
      last_connection_id_consumed_by_self_sequence_number_(0u),
      connection_id_generator_(generator) {
  active_connection_ids_.emplace_back(initial_connection_id, 0u);
}

QuicSelfIssuedConnectionIdManager::~QuicSelfIssuedConnectionIdManager() {
  retire_connection_id_alarm_->Cancel();
}

std::optional<QuicNewConnectionIdFrame>
QuicSelfIssuedConnectionIdManager::MaybeIssueNewConnectionId() {
  std::optional<QuicConnectionId> new_cid =
      connection_id_generator_.GenerateNextConnectionId(last_connection_id_);
  if (!new_cid.has_value()) {
    return {};
  }
  if (!visitor_->MaybeReserveConnectionId(*new_cid)) {
    return {};
  }
  QuicNewConnectionIdFrame frame;
  frame.connection_id = *new_cid;
  frame.sequence_number = next_connection_id_sequence_number_++;
  frame.stateless_reset_token =
      QuicUtils::GenerateStatelessResetToken(frame.connection_id);
  active_connection_ids_.emplace_back(frame.connection_id,
                                      frame.sequence_number);
  frame.retire_prior_to = active_connection_ids_.front().second;
  last_connection_id_ = frame.connection_id;
  return frame;
}

std::optional<QuicNewConnectionIdFrame> QuicSelfIssuedConnectionIdManager::
    MaybeIssueNewConnectionIdForPreferredAddress() {
  std::optional<QuicNewConnectionIdFrame> frame = MaybeIssueNewConnectionId();
  QUICHE_DCHECK(!frame.has_value() || (frame->sequence_number == 1u));
  return frame;
}

QuicErrorCode QuicSelfIssuedConnectionIdManager::OnRetireConnectionIdFrame(
    const QuicRetireConnectionIdFrame& frame, QuicTime::Delta pto_delay,
    std::string* error_detail) {
  QUICHE_DCHECK(!active_connection_ids_.empty());
  if (frame.sequence_number >= next_connection_id_sequence_number_) {
    *error_detail = "To be retired connecton ID is never issued.";
    return IETF_QUIC_PROTOCOL_VIOLATION;
  }

  auto it =
      std::find_if(active_connection_ids_.begin(), active_connection_ids_.end(),
                   [&frame](const std::pair<QuicConnectionId, uint64_t>& p) {
                     return p.second == frame.sequence_number;
                   });
  // The corresponding connection ID has been retired. Ignore.
  if (it == active_connection_ids_.end()) {
    return QUIC_NO_ERROR;
  }

  if (to_be_retired_connection_ids_.size() + active_connection_ids_.size() >=
      kMaxNumConnectonIdsInUse) {
    // Close connection if the number of connection IDs in use will exeed the
    // limit, i.e., peer retires connection ID too fast.
    *error_detail = "There are too many connection IDs in use.";
    return QUIC_TOO_MANY_CONNECTION_ID_WAITING_TO_RETIRE;
  }

  QuicTime retirement_time = clock_->ApproximateNow() + 3 * pto_delay;
  if (!to_be_retired_connection_ids_.empty()) {
    retirement_time =
        std::max(retirement_time, to_be_retired_connection_ids_.back().second);
  }

  to_be_retired_connection_ids_.emplace_back(it->first, retirement_time);
  if (!retire_connection_id_alarm_->IsSet()) {
    retire_connection_id_alarm_->Set(retirement_time);
  }

  active_connection_ids_.erase(it);
  MaybeSendNewConnectionIds();

  return QUIC_NO_ERROR;
}

std::vector<QuicConnectionId>
QuicSelfIssuedConnectionIdManager::GetUnretiredConnectionIds() const {
  std::vector<QuicConnectionId> unretired_ids;
  for (const auto& cid_pair : to_be_retired_connection_ids_) {
    unretired_ids.push_back(cid_pair.first);
  }
  for (const auto& cid_pair : active_connection_ids_) {
    unretired_ids.push_back(cid_pair.first);
  }
  return unretired_ids;
}

QuicConnectionId QuicSelfIssuedConnectionIdManager::GetOneActiveConnectionId()
    const {
  QUICHE_DCHECK(!active_connection_ids_.empty());
  return active_connection_ids_.front().first;
}

void QuicSelfIssuedConnectionIdManager::RetireConnectionId() {
  if (to_be_retired_connection_ids_.empty()) {
    QUIC_BUG(quic_bug_12420_1)
        << "retire_connection_id_alarm fired but there is no connection ID "
           "to be retired.";
    return;
  }
  QuicTime now = clock_->ApproximateNow();
  auto it = to_be_retired_connection_ids_.begin();
  do {
    visitor_->OnSelfIssuedConnectionIdRetired(it->first);
    ++it;
  } while (it != to_be_retired_connection_ids_.end() && it->second <= now);
  to_be_retired_connection_ids_.erase(to_be_retired_connection_ids_.begin(),
                                      it);
  // Set the alarm again if there is another connection ID to be removed.
  if (!to_be_retired_connection_ids_.empty()) {
    retire_connection_id_alarm_->Set(
        to_be_retired_connection_ids_.front().second);
  }
}

void QuicSelfIssuedConnectionIdManager::MaybeSendNewConnectionIds() {
  while (active_connection_ids_.size() < active_connection_id_limit_) {
    std::optional<QuicNewConnectionIdFrame> frame = MaybeIssueNewConnectionId();
    if (!frame.has_value()) {
      break;
    }
    if (!visitor_->SendNewConnectionId(*frame)) {
      break;
    }
  }
}

bool QuicSelfIssuedConnectionIdManager::HasConnectionIdToConsume() const {
  for (const auto& active_cid_data : active_connection_ids_) {
    if (active_cid_data.second >
        last_connection_id_consumed_by_self_sequence_number_) {
      return true;
    }
  }
  return false;
}

std::optional<QuicConnectionId>
QuicSelfIssuedConnectionIdManager::ConsumeOneConnectionId() {
  for (const auto& active_cid_data : active_connection_ids_) {
    if (active_cid_data.second >
        last_connection_id_consumed_by_self_sequence_number_) {
      // Since connection IDs in active_connection_ids_ has monotonically
      // increasing sequence numbers, the returned connection ID has the
      // smallest sequence number among all unconsumed active connection IDs.
      last_connection_id_consumed_by_self_sequence_number_ =
          active_cid_data.second;
      return active_cid_data.first;
    }
  }
  return std::nullopt;
}

bool QuicSelfIssuedConnectionIdManager::IsConnectionIdInUse(
    const QuicConnectionId& cid) const {
  for (const auto& active_cid_data : active_connection_ids_) {
    if (active_cid_data.first == cid) {
      return true;
    }
  }
  for (const auto& to_be_retired_cid_data : to_be_retired_connection_ids_) {
    if (to_be_retired_cid_data.first == cid) {
      return true;
    }
  }
  return false;
}

}  // namespace quic
