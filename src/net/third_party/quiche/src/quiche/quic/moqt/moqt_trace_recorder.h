// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_TRACE_RECORDER_H_
#define QUICHE_QUIC_MOQT_MOQT_TRACE_RECORDER_H_

#include <cstdint>
#include <memory>

#include "absl/base/nullability.h"
#include "quiche/quic/core/quic_bandwidth.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_trace_visitor.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quic_trace/quic_trace.pb.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

// Records MOQT-specific information into the provided QUIC trace proto.  The
// wrapped trace can be nullptr, in which case no recording takes place.
class MoqtTraceRecorder {
 public:
  MoqtTraceRecorder() : parent_(nullptr) {}
  explicit MoqtTraceRecorder(quic::QuicTraceVisitor* absl_nullable parent)
      : parent_(parent) {}

  MoqtTraceRecorder(const MoqtTraceRecorder&) = delete;
  MoqtTraceRecorder(MoqtTraceRecorder&&) = delete;
  MoqtTraceRecorder& operator=(const MoqtTraceRecorder&) = delete;
  MoqtTraceRecorder& operator=(MoqtTraceRecorder&&) = delete;

  void SetParentRecorder(quic::QuicTraceVisitor* absl_nullable parent) {
    parent_ = parent;
  }

  // Annotates the specified stream as the MOQT control stream.
  void RecordControlStreamCreated(webtransport::StreamId stream_id);

  // Annotates the specified stream as an MOQT subgroup data stream.
  void RecordSubgroupStreamCreated(webtransport::StreamId stream_id,
                                   uint64_t track_alias, DataStreamIndex index);

  // Annotates the specified stream as an MOQT fetch data stream.
  void RecordFetchStreamCreated(webtransport::StreamId stream_id);

  // Annotates the specified stream as an MOQT probe stream.
  void RecordProbeStreamCreated(webtransport::StreamId stream_id,
                                uint64_t probe_id);

  // Records the fact that the application has enqueued a new object.
  void RecordNewObjectAvaliable(uint64_t track_alias,
                                const MoqtTrackPublisher& publisher,
                                Location location, uint64_t subgroup,
                                MoqtPriority publisher_priority);

  // Records an incoming MOQT Object ACK message.
  void RecordObjectAck(uint64_t track_alias, Location location,
                       quic::QuicTimeDelta ack_delta);

  // Records the fact that the MOQT stack has advised the application to change
  // its bitrate.
  void RecordTargetBitrateSet(quic::QuicBandwidth new_bandwidth);

 private:
  // Adds a new event to the trace, and populates the timestamp.
  quic_trace::Event* AddEvent();

  quic::QuicTraceVisitor* absl_nullable parent_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_TRACE_RECORDER_H_
