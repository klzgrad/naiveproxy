// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_TRACE_RECORDER_H_
#define QUICHE_QUIC_MOQT_MOQT_TRACE_RECORDER_H_

#include <cstdint>

#include "absl/base/nullability.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quic_trace/quic_trace.pb.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

// Records MOQT-specific information into the provided QUIC trace proto.  The
// wrapped trace can be nullptr, in which case no recording takes place.
class MoqtTraceRecorder {
 public:
  MoqtTraceRecorder() : trace_(nullptr) {}
  explicit MoqtTraceRecorder(quic_trace::Trace* absl_nullable trace)
      : trace_(trace) {}

  MoqtTraceRecorder(const MoqtTraceRecorder&) = delete;
  MoqtTraceRecorder(MoqtTraceRecorder&&) = delete;
  MoqtTraceRecorder& operator=(const MoqtTraceRecorder&) = delete;
  MoqtTraceRecorder& operator=(MoqtTraceRecorder&&) = delete;

  void set_trace(quic_trace::Trace* absl_nullable trace) { trace_ = trace; }

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

 private:
  quic_trace::Trace* absl_nullable trace_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_TRACE_RECORDER_H_
