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
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/casts.h"
#include "absl/container/flat_hash_map.h"
#include "absl/strings/ascii.h"
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
#include "quiche/quic/moqt/moqt_outgoing_queue.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/quic/moqt/moqt_track.h"
#include "quiche/quic/moqt/test_tools/moqt_simulator_harness.h"
#include "quiche/quic/test_tools/simulator/actor.h"
#include "quiche/quic/test_tools/simulator/link.h"
#include "quiche/quic/test_tools/simulator/port.h"
#include "quiche/quic/test_tools/simulator/simulator.h"
#include "quiche/quic/test_tools/simulator/switch.h"
#include "quiche/common/platform/api/quiche_command_line_flags.h"
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
using ::quic::QuicClock;
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
  // Packet aggregation timeout.  If zero, this will be set to the quarter of
  // min RTT.
  QuicTimeDelta aggregation_timeout = QuicTimeDelta::Zero();
  // Packet aggregation threshold.  If zero, packet aggregation is disabled.
  QuicByteCount aggregation_threshold = 0;

  // Count frames as useful only if they were received `deadline` after which
  // they were generated.
  QuicTimeDelta deadline = QuicTimeDelta::FromSeconds(2);
  // Delivery order used by the publisher.
  MoqtDeliveryOrder delivery_order = MoqtDeliveryOrder::kDescending;
  // Delivery timeout for the subscription.  This is mechanically independent
  // from `deadline`, which is an accounting-only parameter (in practice, those
  // should probably be close).
  QuicTimeDelta delivery_timeout = QuicTimeDelta::Infinite();
  // Whether MoqtBitrateAdjuster is enabled.
  bool bitrate_adaptation = true;
  // Use alternative delivery timeout design.
  bool alternative_timeout = false;

  // Number of frames in an individual group.
  int keyframe_interval = 30 * 2;
  // Number of frames generated per second.
  int fps = 30;
  // The ratio by which an I-frame is bigger than a P-frame.
  float i_to_p_ratio = 2 / 1;
  // The target bitrate of the data being exchanged.
  QuicBandwidth bitrate = QuicBandwidth::FromBitsPerSecond(1.0e6);

  // Adds random packet loss rate, as a fraction.
  float packet_loss_rate = 0.0f;

  // If non-zero, makes the traffic disappear in the middle of the connection
  // for the specified duration.
  quic::QuicTimeDelta blackhole_duration = QuicTimeDelta::Zero();
};

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

// Box that enacts MoQT simulator specific modifications to the traffic.
class ModificationBox : public Endpoint,
                        public quic::simulator::UnconstrainedPortInterface {
 public:
  ModificationBox(Endpoint* wrapped_endpoint,
                  const SimulationParameters& parameters)
      : Endpoint(
            wrapped_endpoint->simulator(),
            absl::StrCat(wrapped_endpoint->name(), " (moedification box)")),
        wrapped_endpoint_(*wrapped_endpoint),
        parameters_(parameters) {}

  void OnBeforeSimulationStart() {
    if (!parameters_.blackhole_duration.IsZero()) {
      float offset =
          0.5f + RandFloat(*simulator()->GetRandomGenerator()) * 0.2f;
      blackhole_start_time_ =
          simulator()->GetClock()->Now() + offset * parameters_.duration;
    }
  }

  // Endpoint implementation.
  void Act() override {}
  quic::simulator::UnconstrainedPortInterface* GetRxPort() override {
    return this;
  }
  void SetTxPort(quic::simulator::ConstrainedPortInterface* port) override {
    return wrapped_endpoint_.SetTxPort(port);
  }

  // UnconstrainedPortInterface implementation.
  void AcceptPacket(std::unique_ptr<quic::simulator::Packet> packet) {
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

 private:
  Endpoint& wrapped_endpoint_;
  SimulationParameters parameters_;
  std::optional<QuicTime> blackhole_start_time_;
};

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
            track_name, MoqtForwardingPreference::kSubgroup,
            simulator->GetClock())),
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

class ObjectReceiver : public SubscribeRemoteTrack::Visitor {
 public:
  explicit ObjectReceiver(const QuicClock* clock, QuicTimeDelta deadline)
      : clock_(clock), deadline_(deadline) {}

  void OnReply(const FullTrackName& full_track_name,
               std::optional<Location> /*largest_id*/,
               std::optional<absl::string_view> error_reason_phrase) override {
    QUICHE_CHECK(full_track_name == TrackName());
    QUICHE_CHECK(!error_reason_phrase.has_value()) << *error_reason_phrase;
  }

  void OnCanAckObjects(MoqtObjectAckFunction ack_function) override {
    object_ack_function_ = std::move(ack_function);
  }

  void OnObjectFragment(const FullTrackName& full_track_name, Location sequence,
                        MoqtPriority /*publisher_priority*/,
                        MoqtObjectStatus status, absl::string_view object,
                        bool end_of_message) override {
    QUICHE_DCHECK(full_track_name == TrackName());
    if (status != MoqtObjectStatus::kNormal) {
      QUICHE_DCHECK(end_of_message);
      return;
    }
    if (!end_of_message) {
      QUICHE_LOG(DFATAL) << "Partial receiving of objects wasn't enabled";
      return;
    }
    OnFullObject(sequence, object);
  }

  void OnSubscribeDone(FullTrackName /*full_track_name*/) override {}

  void OnFullObject(Location sequence, absl::string_view payload) {
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
  absl::flat_hash_map<Location, std::string> partial_objects_;
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
        modification_box_(switch_.port(1), parameters),
        client_link_(&client_endpoint_, &modification_box_,
                     kClientLinkBandwidth, parameters.min_rtt * 0.25),
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
    if (!parameters_.delivery_timeout.IsInfinite()) {
      subscription_parameters.delivery_timeout = parameters_.delivery_timeout;
    }
    server_session()->JoiningFetch(TrackName(), &receiver_, 0,
                                   subscription_parameters);
    simulator_.RunFor(parameters_.duration);

    // At the end, we wait for eight RTTs until the connection settles down.
    generator_.Stop();
    wait_at_the_end_ =
        8 * client_endpoint_.quic_session()->GetSessionStats().smoothed_rtt;
    simulator_.RunFor(QuicTimeDelta(wait_at_the_end_));
  }

  void HumanReadableOutput() {
    const QuicTimeDelta total_time =
        parameters_.duration + QuicTimeDelta(wait_at_the_end_);
    absl::PrintF("Ran simulation for %v + %.1fms\n", parameters_.duration,
                 absl::ToDoubleMilliseconds(wait_at_the_end_));
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

  void CustomOutput(absl::string_view format) {
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

 private:
  Simulator simulator_;
  MoqtClientEndpoint client_endpoint_;
  MoqtServerEndpoint server_endpoint_;
  quic::simulator::Switch switch_;
  ModificationBox modification_box_;
  quic::simulator::SymmetricLink client_link_;
  quic::simulator::SymmetricLink server_link_;
  MoqtKnownTrackPublisher publisher_;
  ObjectGenerator generator_;
  ObjectReceiver receiver_;
  MoqtBitrateAdjuster adjuster_;
  SimulationParameters parameters_;

  absl::Duration wait_at_the_end_;
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

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    bool, bitrate_adaptation, true,
    "Whether track payload's bitrate can be adjusted.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(absl::Duration, delivery_timeout,
                                absl::InfiniteDuration(),
                                "Delivery timeout for the subscription.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(bool, alternative_timeout, false,
                                "Use alternative delivery timeout design.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    float, packet_loss_rate,
    moqt::test::SimulationParameters().packet_loss_rate,
    "Adds additional packet loss at the publisher-to-subscriber direction, "
    "specified as a fraction.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    absl::Duration, blackhole_duration,
    moqt::test::SimulationParameters().blackhole_duration.ToAbsl(),
    "If non-zero, makes the traffic disappear in the middle of the connection "
    "for the specified duration.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    quic::QuicByteCount, aggregation_threshold,
    moqt::test::SimulationParameters().aggregation_threshold,
    "If non-zero, enables packet aggregation with the specified threshold (the "
    "packets sent by publisher will be delayed until the specified number is "
    "present).");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    absl::Duration, aggregation_timeout,
    moqt::test::SimulationParameters().aggregation_timeout.ToAbsl(),
    "Sets the timeout for packet aggregation; if zero, this will be set to the "
    "quarter of min RTT.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    absl::Duration, group_duration, absl::ZeroDuration(),
    "If non-zero, sets the group size to match the requested duration");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, output_format, "",
    R"(If non-empty, instead of the usual human-readable format,
the tool will output the raw numbers from the simulation, formatted as
descrbied by the parameter.

Supported format keys:
* {on_time_fraction} -- fraction of objects that arrived on time
* {late_fraction} -- fraction of objects that arrived late
* {missing_fraction} -- fraction of objects that never arrived)");

int main(int argc, char** argv) {
  moqt::test::SimulationParameters parameters;
  quiche::QuicheParseCommandLineFlags("moqt_simulator", argc, argv);
  parameters.bandwidth = quic::QuicBandwidth::FromKBitsPerSecond(
      quiche::GetQuicheCommandLineFlag(FLAGS_bandwidth));
  parameters.deadline =
      quic::QuicTimeDelta(quiche::GetQuicheCommandLineFlag(FLAGS_deadline));
  parameters.duration =
      quic::QuicTimeDelta(quiche::GetQuicheCommandLineFlag(FLAGS_duration));
  parameters.bitrate_adaptation =
      quiche::GetQuicheCommandLineFlag(FLAGS_bitrate_adaptation);
  parameters.delivery_timeout = quic::QuicTimeDelta(
      quiche::GetQuicheCommandLineFlag(FLAGS_delivery_timeout));
  parameters.packet_loss_rate =
      quiche::GetQuicheCommandLineFlag(FLAGS_packet_loss_rate);
  parameters.alternative_timeout =
      quiche::GetQuicheCommandLineFlag(FLAGS_alternative_timeout);
  parameters.blackhole_duration = quic::QuicTimeDelta(
      quiche::GetQuicheCommandLineFlag(FLAGS_blackhole_duration));
  parameters.aggregation_threshold =
      quiche::GetQuicheCommandLineFlag(FLAGS_aggregation_threshold);
  parameters.aggregation_timeout = quic::QuicTimeDelta(
      quiche::GetQuicheCommandLineFlag(FLAGS_aggregation_timeout));

  absl::Duration group_duration =
      quiche::GetQuicheCommandLineFlag(FLAGS_group_duration);
  if (group_duration > absl::ZeroDuration()) {
    parameters.keyframe_interval =
        absl::ToDoubleSeconds(group_duration) * parameters.fps;
  }

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

  std::string output_format =
      quiche::GetQuicheCommandLineFlag(FLAGS_output_format);
  if (output_format.empty()) {
    simulator.HumanReadableOutput();
  } else {
    simulator.CustomOutput(output_format);
  }
  return 0;
}
