// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_trace_recorder.h"

#include <cstdint>
#include <optional>

#include "quiche/quic/core/quic_bandwidth.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_object.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quic_trace/quic_trace.pb.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

using ::quic_trace::EventType;

void MoqtTraceRecorder::RecordControlStreamCreated(
    webtransport::StreamId stream_id) {
  if (parent_ == nullptr) {
    return;
  }
  quic_trace::StreamAnnotation* annotation =
      parent_->trace()->add_stream_annotations();
  annotation->set_stream_id(stream_id);
  annotation->set_moqt_control_stream(true);
}

void MoqtTraceRecorder::RecordSubgroupStreamCreated(
    webtransport::StreamId stream_id, uint64_t track_alias,
    DataStreamIndex index) {
  if (parent_ == nullptr) {
    return;
  }
  quic_trace::StreamAnnotation* annotation =
      parent_->trace()->add_stream_annotations();
  annotation->set_stream_id(stream_id);
  annotation->mutable_moqt_subgroup_stream()->set_track_alias(track_alias);
  annotation->mutable_moqt_subgroup_stream()->set_group_id(index.group);
  annotation->mutable_moqt_subgroup_stream()->set_subgroup_id(index.subgroup);
}

void MoqtTraceRecorder::RecordFetchStreamCreated(
    webtransport::StreamId stream_id) {
  if (parent_ == nullptr) {
    return;
  }
  quic_trace::StreamAnnotation* annotation =
      parent_->trace()->add_stream_annotations();
  annotation->set_stream_id(stream_id);
  annotation->mutable_moqt_fetch_stream();
}

void MoqtTraceRecorder::RecordProbeStreamCreated(
    webtransport::StreamId stream_id, uint64_t probe_id) {
  if (parent_ == nullptr) {
    return;
  }
  quic_trace::StreamAnnotation* annotation =
      parent_->trace()->add_stream_annotations();
  annotation->set_stream_id(stream_id);
  annotation->mutable_moqt_probe_stream()->set_probe_id(probe_id);
}

quic_trace::Event* MoqtTraceRecorder::AddEvent() {
  quic_trace::Event* event = parent_->trace()->add_events();
  event->set_time_us(parent_->NowInRecordedFormat());
  return event;
}

void MoqtTraceRecorder::RecordNewObjectAvaliable(
    uint64_t track_alias, const MoqtTrackPublisher& publisher,
    Location location, uint64_t subgroup, MoqtPriority publisher_priority) {
  if (parent_ == nullptr) {
    return;
  }
  quic_trace::Event* event = AddEvent();
  event->set_event_type(EventType::MOQT_OBJECT_ENQUEUED);
  parent_->PopulateTransportState(event->mutable_transport_state());

  quic_trace::MoqtObject* object = event->mutable_moqt_object();
  object->set_track_alias(track_alias);
  object->set_group_id(location.group);
  object->set_object_id(location.object);
  object->set_subgroup_id(subgroup);
  object->set_publisher_priority(publisher_priority);

  std::optional<PublishedObject> object_copy =
      publisher.GetCachedObject(location.group, subgroup, location.object);
  if (object_copy.has_value() && object_copy->metadata.location == location) {
    object->set_payload_size(object_copy->payload.length());
  } else {
    QUICHE_DLOG(WARNING) << "Track " << track_alias << " has marked "
                         << location
                         << " as enqueued, but GetCachedObject was not able to "
                            "return the said object";
  }
}

void MoqtTraceRecorder::RecordObjectAck(uint64_t track_alias, Location location,
                                        quic::QuicTimeDelta ack_delta) {
  if (parent_ == nullptr) {
    return;
  }
  quic_trace::Event* event = AddEvent();
  event->set_event_type(EventType::MOQT_OBJECT_ACKNOWLEDGED);
  event->set_moq_object_ack_time_delta_us(ack_delta.ToMicroseconds());
  event->mutable_moqt_object()->set_track_alias(track_alias);
  event->mutable_moqt_object()->set_group_id(location.group);
  event->mutable_moqt_object()->set_object_id(location.object);
  parent_->PopulateTransportState(event->mutable_transport_state());
}

void MoqtTraceRecorder::RecordTargetBitrateSet(
    quic::QuicBandwidth new_bandwidth) {
  if (parent_ == nullptr) {
    return;
  }
  quic_trace::Event* event = AddEvent();
  event->set_event_type(EventType::MOQT_TARGET_BITRATE_SET);
  event->set_bandwidth_estimate_bps(new_bandwidth.ToBitsPerSecond());
}

}  // namespace moqt
