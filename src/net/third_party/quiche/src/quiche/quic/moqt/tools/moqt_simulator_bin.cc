// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// moqt_simulator simulates the behavior of MoQ Transport under various network
// conditions and application settings.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
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
#include "quiche/quic/moqt/moqt_outgoing_queue.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/quic/moqt/moqt_track.h"
#include "quiche/quic/moqt/test_tools/moqt_simulator_harness.h"
#include "quiche/quic/test_tools/simulator/actor.h"
#include "quiche/quic/test_tools/simulator/link.h"
#include "quiche/quic/test_tools/simulator/simulator.h"
#include "quiche/quic/test_tools/simulator/switch.h"
#include "quiche/common/platform/api/quiche_command_line_flags.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_data_reader.h"
#include "quiche/common/quiche_data_writer.h"
#include "quiche/common/simple_buffer_allocator.h"

namespace moqt::test {
namespace {

using ::quiche::QuicheBuffer;
using ::quiche::QuicheMemSlice;

using ::quic::QuicBandwidth;
using ::quic::QuicByteCount;
using ::quic::QuicClock;
using ::quic::QuicTime;
using ::quic::QuicTimeDelta;

using ::quic::simulator::Simulator;

// In the simulation, the server link is supposed to be the bottleneck, so this
// value just has to be sufficiently larger than the server link bandwidth.
constexpr QuicBandwidth kClientLinkBandwidth =
    QuicBandwidth::FromBitsPerSecond(10.0e6);
constexpr MoqtVersion kMoqtVersion = kDefaultMoqtVersion;

// Track name used by the simulator.
FullTrackName TrackName() { return FullTrackName("test", "track"); }

// Parameters describing the scenario being simulated.
struct SimulationParameters {
  // Bottleneck bandwidth of the simulated scenario.
  QuicBandwidth bandwidth = QuicBandwidth::FromBitsPerSecond(2.0e6);
  // Intended RTT (as computed from propagation delay alone) between the client
  // and the server.
  QuicTimeDelta min_rtt = QuicTimeDelta::FromMilliseconds(20);
  // The size of the network queue; if zero, assumed to be twice the BDP.
  QuicByteCount network_queue_size = 0;
  // Duration for which the simulation is run.
  QuicTimeDelta duration = QuicTimeDelta::FromSeconds(60);

  // Count frames as useful only if they were received `deadline` after which
  // they were generated.
  QuicTimeDelta deadline = QuicTimeDelta::FromSeconds(2);
  // Delivery order used by the publisher.
  MoqtDeliveryOrder delivery_order = MoqtDeliveryOrder::kDescending;

  // Number of frames in an individual group.
  int keyframe_interval = 30 * 2;
  // Number of frames generated per second.
  int fps = 30;
  // The ratio by which an I-frame is bigger than a P-frame.
  float i_to_p_ratio = 2 / 1;
  // The target bitrate of the data being exchanged.
  QuicBandwidth bitrate = QuicBandwidth::FromBitsPerSecond(1.0e6);
};

std::string FormatPercentage(size_t n, size_t total) {
  float percentage = 100.0f * n / total;
  return absl::StrFormat("%d / %d (%.2f%%)", n, total, percentage);
}

// Generates test objects at a constant rate.  The first eight bytes of every
// object generated is a timestamp, the rest is all zeroes.  The first object in
// the group can be made bigger than the rest, to simulate the profile of real
// video bitstreams.
class ObjectGenerator : public quic::simulator::Actor,
                        public moqt::BitrateAdjustable {
 public:
  ObjectGenerator(Simulator* simulator, const std::string& actor_name,
                  MoqtSession* session, FullTrackName track_name,
                  int keyframe_interval, int fps, float i_to_p_ratio,
                  QuicBandwidth bitrate)
      : Actor(simulator, actor_name),
        queue_(std::make_shared<MoqtOutgoingQueue>(
            track_name, MoqtForwardingPreference::kSubgroup)),
        keyframe_interval_(keyframe_interval),
        time_between_frames_(QuicTimeDelta::FromMicroseconds(1.0e6 / fps)),
        i_to_p_ratio_(i_to_p_ratio),
        bitrate_(bitrate),
        bitrate_history_({bitrate}) {}

  void Act() override {
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

  void Start() { Schedule(clock_->Now()); }
  void Stop() { Unschedule(); }

  std::shared_ptr<MoqtOutgoingQueue> queue() { return queue_; }
  size_t total_objects_sent() const { return frame_number_ + 1; }

  size_t GetFrameSize(bool i_frame) const {
    int p_frame_count = keyframe_interval_ - 1;
    // Compute the frame sizes as a fraction of the total group size.
    float i_frame_fraction = i_to_p_ratio_ / (i_to_p_ratio_ + p_frame_count);
    float p_frame_fraction = 1.0 / (i_to_p_ratio_ + p_frame_count);
    float frame_fraction = i_frame ? i_frame_fraction : p_frame_fraction;

    QuicTimeDelta group_duration = time_between_frames_ * keyframe_interval_;
    QuicByteCount group_byte_count = group_duration * bitrate_;
    size_t frame_size = std::ceil(frame_fraction * group_byte_count);
    QUICHE_CHECK_GE(frame_size, 8u)
        << "Frame size is too small for a timestamp";
    return frame_size;
  }

  quic::QuicBandwidth GetCurrentBitrate() const override { return bitrate_; }
  bool AdjustBitrate(quic::QuicBandwidth bandwidth) override {
    bitrate_ = bandwidth;
    bitrate_history_.push_back(bandwidth);
    return true;
  }
  std::string FormatBitrateHistory() const {
    std::vector<std::string> bits;
    bits.reserve(bitrate_history_.size());
    for (QuicBandwidth bandwidth : bitrate_history_) {
      bits.push_back(absl::StrCat(bandwidth));
    }
    return absl::StrJoin(bits, " -> ");
  }

 private:
  std::shared_ptr<MoqtOutgoingQueue> queue_;
  int keyframe_interval_;
  QuicTimeDelta time_between_frames_;
  float i_to_p_ratio_;
  QuicBandwidth bitrate_;
  int frame_number_ = -1;
  std::vector<QuicBandwidth> bitrate_history_;
};

class ObjectReceiver : public RemoteTrack::Visitor {
 public:
  explicit ObjectReceiver(const QuicClock* clock, QuicTimeDelta deadline)
      : clock_(clock), deadline_(deadline) {}

  void OnReply(const FullTrackName& full_track_name,
               std::optional<absl::string_view> error_reason_phrase) override {
    QUICHE_CHECK(full_track_name == TrackName());
    QUICHE_CHECK(!error_reason_phrase.has_value()) << *error_reason_phrase;
  }

  void OnCanAckObjects(MoqtObjectAckFunction ack_function) override {
    object_ack_function_ = std::move(ack_function);
  }

  void OnObjectFragment(const FullTrackName& full_track_name,
                        uint64_t group_sequence, uint64_t object_sequence,
                        MoqtPriority /*publisher_priority*/,
                        MoqtObjectStatus status,
                        MoqtForwardingPreference /*forwarding_preference*/,
                        absl::string_view object,
                        bool end_of_message) override {
    QUICHE_DCHECK(full_track_name == TrackName());
    if (status != MoqtObjectStatus::kNormal) {
      QUICHE_DCHECK(end_of_message);
      return;
    }

    // Buffer and assemble partially available objects.
    // TODO: this logic should be factored out. Also, this should take advantage
    // of the fact that in the current MoQT, the object size is known in
    // advance.
    FullSequence sequence{group_sequence, object_sequence};
    if (!end_of_message) {
      auto [it, unused] = partial_objects_.try_emplace(sequence);
      it->second.append(object);
      return;
    }
    auto it = partial_objects_.find(sequence);
    if (it == partial_objects_.end()) {
      OnFullObject(sequence, object);
      return;
    }
    std::string full_object = std::move(it->second);
    full_object.append(object);
    partial_objects_.erase(it);
    OnFullObject(sequence, full_object);
  }

  void OnFullObject(FullSequence sequence, absl::string_view payload) {
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

  size_t full_objects_received() const { return full_objects_received_; }
  size_t full_objects_received_on_time() const {
    return full_objects_received_on_time_;
  }
  size_t full_objects_received_late() const {
    return full_objects_received_late_;
  }
  size_t total_bytes_received_on_time() const {
    return total_bytes_received_on_time_;
  }

 private:
  const QuicClock* clock_ = nullptr;
  // TODO: figure out when partial objects should be discarded.
  absl::flat_hash_map<FullSequence, std::string> partial_objects_;
  MoqtObjectAckFunction object_ack_function_ = nullptr;

  size_t full_objects_received_ = 0;

  QuicTimeDelta deadline_;
  size_t full_objects_received_on_time_ = 0;
  size_t full_objects_received_late_ = 0;
  size_t total_bytes_received_on_time_ = 0;
};

// Computes the size of the network queue on the switch.
constexpr QuicByteCount AdjustedQueueSize(
    const SimulationParameters& parameters) {
  if (parameters.network_queue_size > 0) {
    return parameters.network_queue_size;
  }
  QuicByteCount bdp = parameters.bandwidth * parameters.min_rtt;
  return 2 * bdp;
}

// Simulates the performance of MoQT transfer under the specified network
// conditions.
class MoqtSimulator {
 public:
  explicit MoqtSimulator(const SimulationParameters& parameters)
      : simulator_(quic::QuicRandom::GetInstance()),
        client_endpoint_(&simulator_, "Client", "Server", kMoqtVersion),
        server_endpoint_(&simulator_, "Server", "Client", kMoqtVersion),
        switch_(&simulator_, "Switch", 8, AdjustedQueueSize(parameters)),
        client_link_(&client_endpoint_, switch_.port(1), kClientLinkBandwidth,
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
    client_endpoint_.RecordTrace();
  }

  MoqtSession* client_session() { return client_endpoint_.session(); }
  MoqtSession* server_session() { return server_endpoint_.session(); }

  std::string GetClientSessionCongestionControl() {
    return quic::CongestionControlTypeToString(
        client_endpoint_.quic_session()
            ->connection()
            ->sent_packet_manager()
            .GetSendAlgorithm()
            ->GetCongestionControlType());
  }

  // Runs the simulation and outputs the results to stdout.
  void Run() {
    // Timeout for establishing the connection.
    constexpr QuicTimeDelta kConnectionTimeout = QuicTimeDelta::FromSeconds(1);

    // Perform the QUIC and the MoQT handshake.
    client_session()->set_support_object_acks(true);
    client_session()->callbacks().session_established_callback = [this] {
      client_established_ = true;
    };
    server_session()->set_support_object_acks(true);
    server_session()->callbacks().session_established_callback = [this] {
      server_established_ = true;
    };
    client_endpoint_.quic_session()->CryptoConnect();
    simulator_.RunUntilOrTimeout(
        [&]() { return client_established_ && server_established_; },
        kConnectionTimeout);
    QUICHE_CHECK(client_established_) << "Client failed to establish session";
    QUICHE_CHECK(server_established_) << "Server failed to establish session";

    generator_.queue()->SetDeliveryOrder(parameters_.delivery_order);
    client_session()->set_publisher(&publisher_);
    client_session()->SetMonitoringInterfaceForTrack(TrackName(), &adjuster_);
    publisher_.Add(generator_.queue());

    // The simulation is started as follows.  At t=0:
    //   (1) The server issues a subscribe request.
    //   (2) The client starts immediately generating data.  At this point, the
    //       server does not yet have an active subscription, so the client has
    //       some catching up to do.
    generator_.Start();
    server_session()->SubscribeCurrentGroup(TrackName(), &receiver_);
    simulator_.RunFor(parameters_.duration);

    // At the end, we wait for eight RTTs until the connection settles down.
    generator_.Stop();
    absl::Duration wait_at_the_end =
        8 * client_endpoint_.quic_session()->GetSessionStats().smoothed_rtt;
    simulator_.RunFor(QuicTimeDelta(wait_at_the_end));
    const QuicTimeDelta total_time =
        parameters_.duration + QuicTimeDelta(wait_at_the_end);

    absl::PrintF("Ran simulation for %v + %.1fms\n", parameters_.duration,
                 absl::ToDoubleMilliseconds(wait_at_the_end));
    absl::PrintF("Congestion control used: %s\n",
                 GetClientSessionCongestionControl());

    size_t total_sent = generator_.total_objects_sent();
    size_t missing_objects =
        generator_.total_objects_sent() - receiver_.full_objects_received();
    absl::PrintF(
        "Objects received: %s\n",
        FormatPercentage(receiver_.full_objects_received(), total_sent));
    absl::PrintF("  on time: %s\n",
                 FormatPercentage(receiver_.full_objects_received_on_time(),
                                  total_sent));
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

 private:
  Simulator simulator_;
  MoqtClientEndpoint client_endpoint_;
  MoqtServerEndpoint server_endpoint_;
  quic::simulator::Switch switch_;
  quic::simulator::SymmetricLink client_link_;
  quic::simulator::SymmetricLink server_link_;
  MoqtKnownTrackPublisher publisher_;
  ObjectGenerator generator_;
  ObjectReceiver receiver_;
  MoqtBitrateAdjuster adjuster_;
  SimulationParameters parameters_;

  bool client_established_ = false;
  bool server_established_ = false;
};

}  // namespace
}  // namespace moqt::test

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    uint64_t, bandwidth,
    moqt::test::SimulationParameters().bandwidth.ToKBitsPerSecond(),
    "Bandwidth of the simulated link, in kilobits per second.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    absl::Duration, deadline,
    moqt::test::SimulationParameters().deadline.ToAbsl(),
    "Frame delivery deadline (used for measurement only).");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    absl::Duration, duration,
    moqt::test::SimulationParameters().duration.ToAbsl(),
    "Duration of the simulation");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, delivery_order, "desc",
    "Delivery order used for the MoQT track simulated ('asc' or 'desc').");

int main(int argc, char** argv) {
  moqt::test::SimulationParameters parameters;
  quiche::QuicheParseCommandLineFlags("moqt_simulator", argc, argv);
  parameters.bandwidth = quic::QuicBandwidth::FromKBitsPerSecond(
      quiche::GetQuicheCommandLineFlag(FLAGS_bandwidth));
  parameters.deadline =
      quic::QuicTimeDelta(quiche::GetQuicheCommandLineFlag(FLAGS_deadline));
  parameters.duration =
      quic::QuicTimeDelta(quiche::GetQuicheCommandLineFlag(FLAGS_duration));

  std::string raw_delivery_order = absl::AsciiStrToLower(
      quiche::GetQuicheCommandLineFlag(FLAGS_delivery_order));
  if (raw_delivery_order == "asc") {
    parameters.delivery_order = moqt::MoqtDeliveryOrder::kAscending;
  } else if (raw_delivery_order == "desc") {
    parameters.delivery_order = moqt::MoqtDeliveryOrder::kDescending;
  } else {
    std::cerr << "--delivery_order must be 'asc' or 'desc'." << std::endl;
    return 1;
  }

  moqt::test::MoqtSimulator simulator(parameters);
  simulator.Run();
  return 0;
}
