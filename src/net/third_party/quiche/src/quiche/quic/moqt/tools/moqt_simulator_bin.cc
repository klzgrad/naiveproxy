// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// moqt_simulator simulates the behavior of MoQ Transport under various network
// conditions and application settings.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/quic_random.h"
#include "quiche/quic/core/quic_bandwidth.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
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
constexpr MoqtVersion kMoqtVersion = MoqtVersion::kDraft05;

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

  // Number of frames in an individual group.
  int keyframe_interval = 30 * 2;
  // Number of frames generated per second.
  int fps = 30;
  // The ratio by which an I-frame is bigger than a P-frame.
  float i_to_p_ratio = 2 / 1;
  // The target bitrate of the data being exchanged.
  QuicBandwidth bitrate = QuicBandwidth::FromBitsPerSecond(1.0e6);
};

// Generates test objects at a constant rate.  The first eight bytes of every
// object generated is a timestamp, the rest is all zeroes.  The first object in
// the group can be made bigger than the rest, to simulate the profile of real
// video bitstreams.
class ObjectGenerator : public quic::simulator::Actor {
 public:
  ObjectGenerator(Simulator* simulator, const std::string& actor_name,
                  MoqtSession* session, FullTrackName track_name,
                  int keyframe_interval, int fps, float i_to_p_ratio,
                  QuicBandwidth bitrate)
      : Actor(simulator, actor_name),
        queue_(std::make_shared<MoqtOutgoingQueue>(
            track_name, MoqtForwardingPreference::kGroup)),
        keyframe_interval_(keyframe_interval),
        time_between_frames_(QuicTimeDelta::FromMicroseconds(1.0e6 / fps)) {
    int p_frame_count = keyframe_interval - 1;
    // Compute the frame sizes as a fraction of the total group size.
    float i_frame_fraction = i_to_p_ratio / (i_to_p_ratio + p_frame_count);
    float p_frame_fraction = 1.0 / (i_to_p_ratio + p_frame_count);

    QuicTimeDelta group_duration =
        QuicTimeDelta::FromMicroseconds(1.0e6 * keyframe_interval / fps);
    QuicByteCount group_byte_count = group_duration * bitrate;
    i_frame_size_ = i_frame_fraction * group_byte_count;
    p_frame_size_ = p_frame_fraction * group_byte_count;
    QUICHE_CHECK_GE(i_frame_size_, 8u) << "Not enough space for a timestamp";
    QUICHE_CHECK_GE(p_frame_size_, 8u) << "Not enough space for a timestamp";
  }

  void Act() override {
    ++frame_number_;
    bool i_frame = (frame_number_ % keyframe_interval_) == 0;
    size_t size = i_frame ? i_frame_size_ : p_frame_size_;

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

 private:
  std::shared_ptr<MoqtOutgoingQueue> queue_;
  int keyframe_interval_;
  QuicTimeDelta time_between_frames_;
  QuicByteCount i_frame_size_;
  QuicByteCount p_frame_size_;
  int frame_number_ = -1;
};

class ObjectReceiver : public RemoteTrack::Visitor {
 public:
  explicit ObjectReceiver(const QuicClock* clock) : clock_(clock) {}

  void OnReply(const FullTrackName& full_track_name,
               std::optional<absl::string_view> error_reason_phrase) override {
    QUICHE_CHECK(full_track_name == TrackName());
    QUICHE_CHECK(!error_reason_phrase.has_value()) << *error_reason_phrase;
  }

  void OnObjectFragment(const FullTrackName& full_track_name,
                        uint64_t group_sequence, uint64_t object_sequence,
                        MoqtPriority /*publisher_priority*/,
                        MoqtObjectStatus /*status*/,
                        MoqtForwardingPreference /*forwarding_preference*/,
                        absl::string_view object,
                        bool end_of_message) override {
    QUICHE_DCHECK(full_track_name == TrackName());

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
  }

  size_t full_objects_received() const { return full_objects_received_; }

 private:
  const QuicClock* clock_ = nullptr;
  // TODO: figure out when partial objects should be discarded.
  absl::flat_hash_map<FullSequence, std::string> partial_objects_;

  size_t full_objects_received_ = 0;
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
        receiver_(simulator_.GetClock()),
        parameters_(parameters) {}

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
    client_session()->callbacks().session_established_callback = [this] {
      client_established_ = true;
    };
    server_session()->callbacks().session_established_callback = [this] {
      server_established_ = true;
    };
    client_endpoint_.quic_session()->CryptoConnect();
    simulator_.RunUntilOrTimeout(
        [&]() { return client_established_ && server_established_; },
        kConnectionTimeout);
    QUICHE_CHECK(client_established_) << "Client failed to establish session";
    QUICHE_CHECK(server_established_) << "Server failed to establish session";

    // The simulation is started as follows.  At t=0:
    //   (1) The server issues a subscribe request.
    //   (2) The client starts immediately generating data.  At this point, the
    //       server does not yet have an active subscription, so the client has
    //       some catching up to do.
    client_session()->set_publisher(&publisher_);
    publisher_.Add(generator_.queue());
    generator_.Start();
    server_session()->SubscribeCurrentGroup(TrackName().track_namespace,
                                            TrackName().track_name, &receiver_);
    simulator_.RunFor(parameters_.duration);

    // At the end, we wait for eight RTTs until the connection settles down.
    generator_.Stop();
    simulator_.RunFor(QuicTimeDelta(
        8 * client_endpoint_.quic_session()->GetSessionStats().smoothed_rtt));

    std::cout << "Ran simulation for " << parameters_.duration << std::endl;
    std::cout << "Congestion control used : "
              << GetClientSessionCongestionControl() << std::endl;
    std::cout << "Objects sent: " << generator_.total_objects_sent()
              << std::endl;
    std::cout << "Objects received: " << receiver_.full_objects_received()
              << std::endl;
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

int main(int argc, char** argv) {
  moqt::test::SimulationParameters parameters;
  quiche::QuicheParseCommandLineFlags("moqt_simulator", argc, argv);
  parameters.bandwidth = quic::QuicBandwidth::FromKBitsPerSecond(
      quiche::GetQuicheCommandLineFlag(FLAGS_bandwidth));

  moqt::test::MoqtSimulator simulator(parameters);
  simulator.Run();
  return 0;
}
