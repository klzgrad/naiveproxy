// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_trace_recorder.h"

#include <cstdint>

#include "quiche/quic/moqt/moqt_messages.h"
#include "quic_trace/quic_trace.pb.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

void MoqtTraceRecorder::RecordControlStreamCreated(
    webtransport::StreamId stream_id) {
  if (trace_ == nullptr) {
    return;
  }
  quic_trace::StreamAnnotation* annotation = trace_->add_stream_annotations();
  annotation->set_stream_id(stream_id);
  annotation->set_moqt_control_stream(true);
}

void MoqtTraceRecorder::RecordSubgroupStreamCreated(
    webtransport::StreamId stream_id, uint64_t track_alias,
    DataStreamIndex index) {
  if (trace_ == nullptr) {
    return;
  }
  quic_trace::StreamAnnotation* annotation = trace_->add_stream_annotations();
  annotation->set_stream_id(stream_id);
  annotation->mutable_moqt_subgroup_stream()->set_track_alias(track_alias);
  annotation->mutable_moqt_subgroup_stream()->set_group_id(index.group);
  annotation->mutable_moqt_subgroup_stream()->set_subgroup_id(index.subgroup);
}

void MoqtTraceRecorder::RecordFetchStreamCreated(
    webtransport::StreamId stream_id) {
  if (trace_ == nullptr) {
    return;
  }
  quic_trace::StreamAnnotation* annotation = trace_->add_stream_annotations();
  annotation->set_stream_id(stream_id);
  annotation->mutable_moqt_fetch_stream();
}

void MoqtTraceRecorder::RecordProbeStreamCreated(
    webtransport::StreamId stream_id, uint64_t probe_id) {
  if (trace_ == nullptr) {
    return;
  }
  quic_trace::StreamAnnotation* annotation = trace_->add_stream_annotations();
  annotation->set_stream_id(stream_id);
  annotation->mutable_moqt_probe_stream()->set_probe_id(probe_id);
}

}  // namespace moqt
