// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// moqt_simulator simulates the behavior of MoQ Transport under various network
// conditions and application settings.

#include <cstdint>
#include <iostream>
#include <string>

#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "quiche/quic/core/quic_bandwidth.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/test_tools/moqt_simulator.h"
#include "quiche/common/platform/api/quiche_command_line_flags.h"

namespace {

using ::quic::QuicBandwidth;
using ::quic::QuicByteCount;
using ::quic::QuicTimeDelta;

}  // namespace

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
