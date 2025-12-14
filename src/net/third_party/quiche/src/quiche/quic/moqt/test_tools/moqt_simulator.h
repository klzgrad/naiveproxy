// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_SIMULATOR_H_
#define QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_SIMULATOR_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "quiche/quic/core/quic_bandwidth.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/moqt/moqt_bitrate_adjuster.h"
#include "quiche/quic/moqt/moqt_known_track_publisher.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_object.h"
#include "quiche/quic/moqt/moqt_outgoing_queue.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/quic/moqt/moqt_session_interface.h"
#include "quiche/quic/moqt/test_tools/moqt_simulator_harness.h"
#include "quiche/quic/test_tools/simulator/actor.h"
#include "quiche/quic/test_tools/simulator/link.h"
#include "quiche/quic/test_tools/simulator/port.h"
#include "quiche/quic/test_tools/simulator/switch.h"

namespace moqt::test {

// Parameters describing the scenario being simulated.
struct SimulationParameters {
  // Bottleneck bandwidth of the simulated scenario.
  quic::QuicBandwidth bandwidth = quic::QuicBandwidth::FromBitsPerSecond(2.0e6);
  // Intended RTT (as computed from propagation delay alone) between the client
  // and the server.
  quic::QuicTimeDelta min_rtt = quic::QuicTimeDelta::FromMilliseconds(20);
  // The size of the network queue; if zero, assumed to be twice the BDP.
  quic::QuicByteCount network_queue_size = 0;
  // Duration for which the simulation is run.
  quic::QuicTimeDelta duration = quic::QuicTimeDelta::FromSeconds(60);
  // Packet aggregation timeout.  If zero, this will be set to the quarter of
  // min RTT.
  quic::QuicTimeDelta aggregation_timeout = quic::QuicTimeDelta::Zero();
  // Packet aggregation threshold.  If zero, packet aggregation is disabled.
  quic::QuicByteCount aggregation_threshold = 0;

  // Count frames as useful only if they were received `deadline` after which
  // they were generated.
  quic::QuicTimeDelta deadline = quic::QuicTimeDelta::FromSeconds(2);
  // Delivery order used by the publisher.
  MoqtDeliveryOrder delivery_order = MoqtDeliveryOrder::kDescending;
  // Delivery timeout for the subscription.  This is mechanically independent
  // from `deadline`, which is an accounting-only parameter (in practice, those
  // should probably be close).
  quic::QuicTimeDelta delivery_timeout = quic::QuicTimeDelta::Infinite();
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
  quic::QuicBandwidth bitrate = quic::QuicBandwidth::FromBitsPerSecond(1.0e6);

  // Adds random packet loss rate, as a fraction.
  float packet_loss_rate = 0.0f;

  // If non-zero, makes the traffic disappear in the middle of the connection
  // for the specified duration.
  quic::QuicTimeDelta blackhole_duration = quic::QuicTimeDelta::Zero();
};

// Box that enacts MoQT simulator specific modifications to the traffic.
class ModificationBox : public quic::simulator::Endpoint,
                        public quic::simulator::UnconstrainedPortInterface {
 public:
  ModificationBox(Endpoint* wrapped_endpoint,
                  const SimulationParameters& parameters);

  void OnBeforeSimulationStart();

  // Endpoint implementation.
  void Act() override {}
  quic::simulator::UnconstrainedPortInterface* GetRxPort() override {
    return this;
  }
  void SetTxPort(quic::simulator::ConstrainedPortInterface* port) override {
    return wrapped_endpoint_.SetTxPort(port);
  }

  // UnconstrainedPortInterface implementation.
  void AcceptPacket(std::unique_ptr<quic::simulator::Packet> packet) override;

 private:
  Endpoint& wrapped_endpoint_;
  SimulationParameters parameters_;
  std::optional<quic::QuicTime> blackhole_start_time_;
};

// Generates test objects at a constant rate.  The first eight bytes of every
// object generated is a timestamp, the rest is all zeroes.  The first object in
// the group can be made bigger than the rest, to simulate the profile of real
// video bitstreams.
class ObjectGenerator : public quic::simulator::Actor,
                        public moqt::BitrateAdjustable {
 public:
  ObjectGenerator(quic::simulator::Simulator* simulator,
                  const std::string& actor_name, MoqtSession* session,
                  FullTrackName track_name, int keyframe_interval, int fps,
                  float i_to_p_ratio, quic::QuicBandwidth bitrate);

  void Act() override;

  void Start() { Schedule(clock_->Now()); }
  void Stop() { Unschedule(); }

  std::shared_ptr<MoqtOutgoingQueue> queue() { return queue_; }
  size_t total_objects_sent() const { return frame_number_ + 1; }

  size_t GetFrameSize(bool i_frame) const;

  quic::QuicBandwidth GetCurrentBitrate() const override { return bitrate_; }
  bool CouldUseExtraBandwidth() override { return true; }
  void ConsiderAdjustingBitrate(quic::QuicBandwidth bandwidth,
                                BitrateAdjustmentType type) override;
  std::string FormatBitrateHistory() const;

 private:
  std::shared_ptr<MoqtOutgoingQueue> queue_;
  int keyframe_interval_;
  quic::QuicTimeDelta time_between_frames_;
  float i_to_p_ratio_;
  quic::QuicBandwidth bitrate_;
  int frame_number_ = -1;
  std::vector<quic::QuicBandwidth> bitrate_history_;
};

class ObjectReceiver : public SubscribeVisitor {
 public:
  explicit ObjectReceiver(const quic::QuicClock* clock,
                          quic::QuicTimeDelta deadline)
      : clock_(clock), deadline_(deadline) {}

  void OnReply(
      const FullTrackName& full_track_name,
      std::variant<SubscribeOkData, MoqtRequestError> response) override;

  void OnCanAckObjects(MoqtObjectAckFunction ack_function) override {
    object_ack_function_ = std::move(ack_function);
  }

  void OnObjectFragment(const FullTrackName& full_track_name,
                        const PublishedObjectMetadata& metadata,
                        absl::string_view object, bool end_of_message) override;

  void OnPublishDone(FullTrackName /*full_track_name*/) override {}
  void OnMalformedTrack(const FullTrackName& /*full_track_name*/) override {}
  void OnStreamFin(const FullTrackName&, DataStreamIndex) override {}
  void OnStreamReset(const FullTrackName&, DataStreamIndex) override {}

  void OnFullObject(Location sequence, absl::string_view payload);

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
  const quic::QuicClock* clock_ = nullptr;
  // TODO: figure out when partial objects should be discarded.
  absl::flat_hash_map<Location, std::string> partial_objects_;
  MoqtObjectAckFunction object_ack_function_ = nullptr;

  size_t full_objects_received_ = 0;

  quic::QuicTimeDelta deadline_;
  size_t full_objects_received_on_time_ = 0;
  size_t full_objects_received_late_ = 0;
  size_t total_bytes_received_on_time_ = 0;
};

// Simulates the performance of MoQT transfer under the specified network
// conditions.
class MoqtSimulator {
 public:
  explicit MoqtSimulator(const SimulationParameters& parameters);

  MoqtSession* client_session() { return client_endpoint_.session(); }
  MoqtSession* server_session() { return server_endpoint_.session(); }

  std::string GetClientSessionCongestionControl();

  // Runs the simulation and outputs the results to stdout.
  void Run();

  // The fraction of objects received on time.
  float received_on_time_fraction() const;

  // Outputs the results of the simulation to stdout.
  void HumanReadableOutput();
  void CustomOutput(absl::string_view format);

 private:
  quic::simulator::Simulator simulator_;
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

}  // namespace moqt::test

#endif  // QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_SIMULATOR_H_
