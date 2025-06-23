// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_probe_manager.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_stream.h"
#include "quiche/common/wire_serialization.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

namespace {
constexpr quic::QuicByteCount kWriteChunkSize = 4096;
constexpr char kZeroes[kWriteChunkSize] = {0};
}  // namespace

std::optional<ProbeId> MoqtProbeManager::StartProbe(
    quic::QuicByteCount probe_size, quic::QuicTimeDelta timeout,
    Callback callback) {
  if (probe_.has_value()) {
    return std::nullopt;
  }

  ProbeId id = next_probe_id_++;
  webtransport::Stream* stream = session_->OpenOutgoingUnidirectionalStream();
  if (stream == nullptr) {
    return std::nullopt;
  }

  probe_ = PendingProbe{
      id,         clock_->ApproximateNow(), clock_->ApproximateNow() + timeout,
      probe_size, stream->GetStreamId(),    std::move(callback)};
  auto visitor_owned =
      std::make_unique<ProbeStreamVisitor>(this, stream, id, probe_size);
  ProbeStreamVisitor* visitor = visitor_owned.get();
  stream->SetVisitor(std::move(visitor_owned));
  stream->SetPriority(webtransport::StreamPriority{
      /*send_group_id=*/0, /*send_order=*/kMoqtProbeStreamSendOrder});
  visitor->OnCanWrite();
  RescheduleAlarm();
  return id;
}

std::optional<ProbeId> MoqtProbeManager::StopProbe() {
  if (!probe_.has_value()) {
    return std::nullopt;
  }
  ProbeId id = probe_->id;
  ClosePendingProbe(ProbeStatus::kAborted);
  return id;
}

void MoqtProbeManager::ProbeStreamVisitor::OnCanWrite() {
  if (!ValidateProbe() || !stream_->CanWrite()) {
    return;
  }

  if (!header_sent_) {
    absl::Status status = quiche::WriteIntoStream(
        *stream_, *quiche::SerializeIntoString(
                      quiche::WireVarInt62(MoqtDataStreamType::kPadding)));
    QUICHE_DCHECK(status.ok()) << status;  // Should succeed if CanWrite().
    header_sent_ = true;
  }

  while (stream_->CanWrite() && data_remaining_ > 0) {
    quic::QuicByteCount chunk_size = std::min(kWriteChunkSize, data_remaining_);
    absl::string_view chunk(kZeroes, chunk_size);
    quiche::StreamWriteOptions options;
    options.set_send_fin(chunk_size == data_remaining_);
    absl::Status status = stream_->Writev(absl::MakeSpan(&chunk, 1), options);
    QUICHE_DCHECK(status.ok()) << status;  // Should succeed if CanWrite().
    data_remaining_ -= chunk_size;
  }
}

void MoqtProbeManager::ProbeStreamVisitor::OnStopSendingReceived(
    webtransport::StreamErrorCode error) {
  if (!ValidateProbe()) {
    return;
  }
  manager_->ClosePendingProbe(ProbeStatus::kAborted);
}

void MoqtProbeManager::ProbeStreamVisitor::OnWriteSideInDataRecvdState() {
  if (!ValidateProbe()) {
    return;
  }
  manager_->ClosePendingProbe(ProbeStatus::kSuccess);
}

void MoqtProbeManager::RescheduleAlarm() {
  quic::QuicTime deadline =
      probe_.has_value() ? probe_->deadline : quic::QuicTime::Zero();
  timeout_alarm_->Update(deadline, quic::QuicTimeDelta::Zero());
}

void MoqtProbeManager::OnAlarm() {
  if (probe_.has_value()) {
    ClosePendingProbe(ProbeStatus::kTimeout);
  }
  RescheduleAlarm();
}

void MoqtProbeManager::ClosePendingProbe(ProbeStatus status) {
  std::optional<PendingProbe> probe = std::move(probe_);
  if (!probe.has_value()) {
    QUICHE_BUG(ClosePendingProbe_no_probe);
    return;
  }
  if (status != ProbeStatus::kSuccess) {
    webtransport::Stream* stream = session_->GetStreamById(probe->stream_id);
    if (stream != nullptr) {
      // TODO: figure out the error code.
      stream->ResetWithUserCode(0);
    }
  }
  quic::QuicTime now = clock_->ApproximateNow();
  std::move(probe->callback)(
      ProbeResult{probe->id, status, probe->probe_size, now - probe->start});
}
}  // namespace moqt
