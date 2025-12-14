// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/test_tools/moqt_simulator.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/casts.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "quiche/quic/core/crypto/quic_random.h"
#include "quiche/quic/core/quic_bandwidth.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/moqt/moqt_bitrate_adjuster.h"
#include "quiche/quic/moqt/moqt_known_track_publisher.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_object.h"
#include "quiche/quic/moqt/moqt_outgoing_queue.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/quic/moqt/moqt_session_interface.h"
#include "quiche/quic/moqt/moqt_trace_recorder.h"
#include "quiche/quic/moqt/test_tools/moqt_simulator_harness.h"
#include "quiche/quic/test_tools/simulator/actor.h"
#include "quiche/quic/test_tools/simulator/link.h"
#include "quiche/quic/test_tools/simulator/port.h"
#include "quiche/quic/test_tools/simulator/simulator.h"
#include "quiche/quic/test_tools/simulator/switch.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_data_reader.h"
#include "quiche/common/quiche_data_writer.h"
#include "quiche/common/quiche_mem_slice.h"
#include "quiche/common/simple_buffer_allocator.h"

namespace moqt::test {
namespace {

using ::quiche::QuicheBuffer;
using ::quiche::QuicheMemSlice;

using ::quic::QuicBandwidth;
using ::quic::QuicByteCount;
using ::quic::QuicTime;
using ::quic::QuicTimeDelta;

using ::quic::simulator::Endpoint;
using ::quic::simulator::Simulator;

// In the simulation, the server link is supposed to be the bottleneck, so this
// value just has to be sufficiently larger than the server link bandwidth.
constexpr QuicBandwidth kClientLinkBandwidth =
    QuicBandwidth::FromBitsPerSecond(10.0e6);
constexpr MoqtVersion kMoqtVersion = kDefaultMoqtVersion;

// Track name used by the simulator.
FullTrackName TrackName() { return FullTrackName("test", "track"); }

std::string FormatPercentage(size_t n, size_t total) {
  float percentage = 100.0f * n / total;
  return absl::StrFormat("%d / %d (%.2f%%)", n, total, percentage);
}

using OutputField = std::pair<absl::string_view, std::string>;

OutputField OutputFraction(absl::string_view key, size_t n, size_t total) {
  float fraction = static_cast<float>(n) / total;
  return OutputField(key, absl::StrCat(fraction));
}

float RandFloat(quic::QuicRandom& rng) {
  uint32_t number;
  rng.RandBytes(&number, sizeof(number));
  return absl::bit_cast<float>((number & 0x7fffff) | 0x3f800000) - 1.0f;
}

}  // namespace

ModificationBox::ModificationBox(Endpoint* wrapped_endpoint,
                                 const SimulationParameters& parameters)
    : Endpoint(wrapped_endpoint->simulator(),
               absl::StrCat(wrapped_endpoint->name(), " (modification box)")),
      wrapped_endpoint_(*wrapped_endpoint),
      parameters_(parameters) {}

void ModificationBox::OnBeforeSimulationStart() {
  if (!parameters_.blackhole_duration.IsZero()) {
    float offset = 0.5f + RandFloat(*simulator()->GetRandomGenerator()) * 0.2f;
    blackhole_start_time_ =
        simulator()->GetClock()->Now() + offset * parameters_.duration;
  }
}

void ModificationBox::AcceptPacket(
    std::unique_ptr<quic::simulator::Packet> packet) {
  quic::QuicRandom* const rng = simulator()->GetRandomGenerator();
  const quic::QuicTime now = simulator()->GetClock()->Now();
  bool drop = false;
  if (parameters_.packet_loss_rate > 0) {
    if (RandFloat(*rng) < parameters_.packet_loss_rate) {
      drop = true;
    }
  }
  if (blackhole_start_time_.has_value()) {
    quic::QuicTime blackhole_end_time =
        *blackhole_start_time_ + parameters_.blackhole_duration;
    if (now >= blackhole_start_time_ && now < blackhole_end_time) {
      drop = true;
    }
  }
  if (!drop) {
    wrapped_endpoint_.GetRxPort()->AcceptPacket(std::move(packet));
  }
}

ObjectGenerator::ObjectGenerator(quic::simulator::Simulator* simulator,
                                 const std::string& actor_name,
                                 MoqtSession* session, FullTrackName track_name,
                                 int keyframe_interval, int fps,
                                 float i_to_p_ratio,
                                 quic::QuicBandwidth bitrate)
    : Actor(simulator, actor_name),
      queue_(std::make_shared<MoqtOutgoingQueue>(
          track_name, MoqtForwardingPreference::kSubgroup,
          simulator->GetClock())),
      keyframe_interval_(keyframe_interval),
      time_between_frames_(QuicTimeDelta::FromMicroseconds(1.0e6 / fps)),
      i_to_p_ratio_(i_to_p_ratio),
      bitrate_(bitrate),
      bitrate_history_({bitrate}) {}

void ObjectGenerator::Act() {
  ++frame_number_;
  bool i_frame = (frame_number_ % keyframe_interval_) == 0;
  size_t size = GetFrameSize(i_frame);

  QuicheBuffer buffer(quiche::SimpleBufferAllocator::Get(), size);
  memset(buffer.data(), 0, buffer.size());
  quiche::QuicheDataWriter writer(size, buffer.data());
  bool success = writer.WriteUInt64(clock_->Now().ToDebuggingValue());
  QUICHE_CHECK(success);

  queue_->AddObject(QuicheMemSlice(std::move(buffer)), i_frame);
  Schedule(clock_->Now() + time_between_frames_);
}

size_t ObjectGenerator::GetFrameSize(bool i_frame) const {
  int p_frame_count = keyframe_interval_ - 1;
  // Compute the frame sizes as a fraction of the total group size.
  float i_frame_fraction = i_to_p_ratio_ / (i_to_p_ratio_ + p_frame_count);
  float p_frame_fraction = 1.0 / (i_to_p_ratio_ + p_frame_count);
  float frame_fraction = i_frame ? i_frame_fraction : p_frame_fraction;

  QuicTimeDelta group_duration = time_between_frames_ * keyframe_interval_;
  QuicByteCount group_byte_count = group_duration * bitrate_;
  size_t frame_size = std::ceil(frame_fraction * group_byte_count);
  QUICHE_CHECK_GE(frame_size, 8u) << "Frame size is too small for a timestamp";
  return frame_size;
}

void ObjectGenerator::ConsiderAdjustingBitrate(quic::QuicBandwidth bandwidth,
                                               BitrateAdjustmentType type) {
  if (moqt::ShouldIgnoreBitrateAdjustment(bandwidth, type, bitrate_,
                                          /*min_change=*/0.01)) {
    return;
  }
  bitrate_ = bandwidth;
  bitrate_history_.push_back(bandwidth);
}

std::string ObjectGenerator::FormatBitrateHistory() const {
  std::vector<std::string> bits;
  bits.reserve(bitrate_history_.size());
  for (QuicBandwidth bandwidth : bitrate_history_) {
    bits.push_back(absl::StrCat(bandwidth));
  }
  return absl::StrJoin(bits, " -> ");
}

void ObjectReceiver::OnReply(
    const FullTrackName& full_track_name,
    std::variant<SubscribeOkData, MoqtRequestError> response) {
  QUICHE_CHECK(full_track_name == TrackName());
  if (std::holds_alternative<MoqtRequestError>(response)) {
    MoqtRequestError error = std::get<MoqtRequestError>(response);
    QUICHE_CHECK(!error.reason_phrase.empty()) << error.reason_phrase;
  }
}

void ObjectReceiver::OnObjectFragment(const FullTrackName& full_track_name,
                                      const PublishedObjectMetadata& metadata,
                                      absl::string_view object,
                                      bool end_of_message) {
  QUICHE_DCHECK(full_track_name == TrackName());
  if (metadata.status != MoqtObjectStatus::kNormal) {
    QUICHE_DCHECK(end_of_message);
    return;
  }
  if (!end_of_message) {
    QUICHE_LOG(DFATAL) << "Partial receiving of objects wasn't enabled";
    return;
  }
  OnFullObject(metadata.location, object);
}

void ObjectReceiver::OnFullObject(Location sequence,
                                  absl::string_view payload) {
  QUICHE_CHECK_GE(payload.size(), 8u);
  quiche::QuicheDataReader reader(payload);
  uint64_t time_us;
  reader.ReadUInt64(&time_us);
  QuicTime time = QuicTime::Zero() + QuicTimeDelta::FromMicroseconds(time_us);
  QuicTimeDelta delay = clock_->Now() - time;
  QUICHE_CHECK_GT(delay, QuicTimeDelta::Zero());
  QUICHE_DCHECK(absl::c_all_of(reader.ReadRemainingPayload(),
                               [](char c) { return c == 0; }));
  ++full_objects_received_;
  if (delay > deadline_) {
    ++full_objects_received_late_;
  } else {
    ++full_objects_received_on_time_;
    total_bytes_received_on_time_ += payload.size();
  }
  if (object_ack_function_) {
    object_ack_function_(sequence.group, sequence.object, deadline_ - delay);
  }
}

// Computes the size of the network queue on the switch.
constexpr QuicByteCount AdjustedQueueSize(
    const SimulationParameters& parameters) {
  if (parameters.network_queue_size > 0) {
    return parameters.network_queue_size;
  }
  QuicByteCount bdp = parameters.bandwidth * parameters.min_rtt;
  return 2 * bdp;
}

MoqtSimulator::MoqtSimulator(const SimulationParameters& parameters)
    : simulator_(quic::QuicRandom::GetInstance()),
      client_endpoint_(&simulator_, "Client", "Server", kMoqtVersion),
      server_endpoint_(&simulator_, "Server", "Client", kMoqtVersion),
      switch_(&simulator_, "Switch", 8, AdjustedQueueSize(parameters)),
      modification_box_(switch_.port(1), parameters),
      client_link_(&client_endpoint_, &modification_box_, kClientLinkBandwidth,
                   parameters.min_rtt * 0.25),
      server_link_(&server_endpoint_, switch_.port(2), parameters.bandwidth,
                   parameters.min_rtt * 0.25),
      generator_(&simulator_, "Client generator", client_endpoint_.session(),
                 TrackName(), parameters.keyframe_interval, parameters.fps,
                 parameters.i_to_p_ratio, parameters.bitrate),
      receiver_(simulator_.GetClock(), parameters.deadline),
      adjuster_(simulator_.GetClock(), client_endpoint_.session()->session(),
                &generator_),
      parameters_(parameters) {
  if (parameters.aggregation_threshold > 0) {
    QuicTimeDelta timeout = parameters.aggregation_timeout;
    if (timeout.IsZero()) {
      timeout = parameters.min_rtt * 0.25;
    }
    switch_.port_queue(2)->EnableAggregation(parameters.aggregation_threshold,
                                             timeout);
  }
  client_endpoint_.RecordTrace();
  QUICHE_DCHECK(client_endpoint_.trace_visitor() != nullptr);
  client_endpoint_.session()->trace_recorder().set_trace(
      client_endpoint_.trace_visitor()->trace());
}

std::string MoqtSimulator::GetClientSessionCongestionControl() {
  return quic::CongestionControlTypeToString(client_endpoint_.quic_session()
                                                 ->connection()
                                                 ->sent_packet_manager()
                                                 .GetSendAlgorithm()
                                                 ->GetCongestionControlType());
}

void MoqtSimulator::Run() {
  // Perform the QUIC and the MoQT handshake.
  client_session()->set_support_object_acks(true);
  server_session()->set_support_object_acks(true);
  RunHandshakeOrDie(simulator_, client_endpoint_, server_endpoint_);

  generator_.queue()->SetDeliveryOrder(parameters_.delivery_order);
  client_session()->set_publisher(&publisher_);
  if (parameters_.bitrate_adaptation) {
    client_session()->SetMonitoringInterfaceForTrack(TrackName(), &adjuster_);
  }
  if (parameters_.alternative_timeout) {
    client_session()->UseAlternateDeliveryTimeout();
  }
  publisher_.Add(generator_.queue());
  modification_box_.OnBeforeSimulationStart();

  // The simulation is started as follows.  At t=0:
  //   (1) The server issues a subscribe request.
  //   (2) The client starts immediately generating data.  At this point, the
  //       server does not yet have an active subscription, so the client has
  //       some catching up to do.
  generator_.Start();
  VersionSpecificParameters subscription_parameters;
  if (parameters_.bitrate_adaptation) {
    subscription_parameters.oack_window_size = parameters_.deadline;
  }
  if (!parameters_.delivery_timeout.IsInfinite()) {
    subscription_parameters.delivery_timeout = parameters_.delivery_timeout;
  }
  server_session()->RelativeJoiningFetch(TrackName(), &receiver_, 0,
                                         subscription_parameters);
  simulator_.RunFor(parameters_.duration);

  // At the end, we wait for eight RTTs until the connection settles down.
  generator_.Stop();
  wait_at_the_end_ =
      8 * client_endpoint_.quic_session()->GetSessionStats().smoothed_rtt;
  simulator_.RunFor(QuicTimeDelta(wait_at_the_end_));
}

void MoqtSimulator::HumanReadableOutput() {
  const QuicTimeDelta total_time =
      parameters_.duration + QuicTimeDelta(wait_at_the_end_);
  absl::PrintF("Ran simulation for %v + %.1fms\n", parameters_.duration,
               absl::ToDoubleMilliseconds(wait_at_the_end_));
  absl::PrintF("Congestion control used: %s\n",
               GetClientSessionCongestionControl());

  size_t total_sent = generator_.total_objects_sent();
  size_t missing_objects =
      generator_.total_objects_sent() - receiver_.full_objects_received();
  absl::PrintF("Objects received: %s\n",
               FormatPercentage(receiver_.full_objects_received(), total_sent));
  absl::PrintF(
      "  on time: %s\n",
      FormatPercentage(receiver_.full_objects_received_on_time(), total_sent));
  absl::PrintF(
      "     late: %s\n",
      FormatPercentage(receiver_.full_objects_received_late(), total_sent));
  absl::PrintF("    never: %s\n",
               FormatPercentage(missing_objects, total_sent));
  absl::PrintF("\n");
  absl::PrintF("Average on-time goodput: %v\n",
               QuicBandwidth::FromBytesAndTimeDelta(
                   receiver_.total_bytes_received_on_time(), total_time));
  absl::PrintF("Bitrates: %s\n", generator_.FormatBitrateHistory());
}

void MoqtSimulator::CustomOutput(absl::string_view format) {
  size_t total_sent = generator_.total_objects_sent();
  std::vector<OutputField> fields;
  fields.push_back(OutputFraction("{on_time_fraction}",
                                  receiver_.full_objects_received_on_time(),
                                  total_sent));
  fields.push_back(OutputFraction(
      "{late_fraction}", receiver_.full_objects_received_late(), total_sent));
  size_t missing_objects =
      generator_.total_objects_sent() - receiver_.full_objects_received();
  fields.push_back(
      OutputFraction("{missing_fraction}", missing_objects, total_sent));
  std::string output = absl::StrReplaceAll(format, fields);
  std::cout << output << std::endl;
}

float MoqtSimulator::received_on_time_fraction() const {
  QUICHE_DCHECK_GE(generator_.total_objects_sent(), 0);
  return static_cast<float>(receiver_.full_objects_received_on_time()) /
         generator_.total_objects_sent();
}

}  // namespace moqt::test
