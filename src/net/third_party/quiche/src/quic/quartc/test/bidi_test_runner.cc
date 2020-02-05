// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/quartc/test/bidi_test_runner.h"

#include <utility>

#include "net/third_party/quiche/src/quic/quartc/test/quartc_peer.h"

namespace quic {
namespace test {

namespace {

void LogResults(const std::vector<ReceivedMessage>& messages,
                IdToSequenceNumberMap sent_sequence_numbers) {
  QuicTime::Delta max_delay = QuicTime::Delta::Zero();
  QuicTime::Delta total_delay = QuicTime::Delta::Zero();
  QuicByteCount total_throughput = 0;
  int64_t messages_received = 0;
  for (const auto& message : messages) {
    QuicTime::Delta one_way_delay =
        message.receive_time - message.frame.send_time;
    QUIC_VLOG(1) << "Frame details: source_id=" << message.frame.source_id
                 << ", sequence_number=" << message.frame.sequence_number
                 << ", one_way_delay (ms)=" << one_way_delay.ToMilliseconds();
    max_delay = std::max(max_delay, one_way_delay);
    total_delay = total_delay + one_way_delay;
    total_throughput += message.frame.size;
    ++messages_received;
  }

  int64_t messages_expected = 0;
  for (const auto& it : sent_sequence_numbers) {
    // Sequence numbers start at zero, so add one to the last sequence number
    // to get the expected number of messages.
    messages_expected += it.second + 1;
  }

  QuicBandwidth total_bandwidth = QuicBandwidth::FromBytesAndTimeDelta(
      total_throughput,
      messages.back().receive_time - messages.front().receive_time);
  double fraction_lost =
      1.0 - static_cast<double>(messages_received) / messages_expected;
  QUIC_LOG(INFO) << "Summary:\n  max_delay (ms)=" << max_delay.ToMilliseconds()
                 << "\n  average_delay (ms)="
                 << total_delay.ToMilliseconds() / messages.size()
                 << "\n  total_throughput (bytes)=" << total_throughput
                 << "\n  total_bandwidth (bps)="
                 << total_bandwidth.ToBitsPerSecond()
                 << "\n  fraction_lost=" << fraction_lost;
}

}  // namespace

BidiTestRunner::BidiTestRunner(simulator::Simulator* simulator,
                               QuartcPacketTransport* client_transport,
                               QuartcPacketTransport* server_transport)
    : simulator_(simulator),
      client_transport_(client_transport),
      server_transport_(server_transport) {
  // Set up default data source configs.
  // Emulates an audio source with a 20 ms ptime.
  QuartcDataSource::Config audio;
  audio.id = 1;
  audio.frame_interval = QuicTime::Delta::FromMilliseconds(20);
  audio.min_bandwidth = QuicBandwidth::FromKBitsPerSecond(8);
  audio.max_bandwidth = QuicBandwidth::FromKBitsPerSecond(64);

  // Emulates a video source at 30 fps.
  QuartcDataSource::Config video;
  video.id = 2;
  video.frame_interval = QuicTime::Delta::FromMicroseconds(33333);
  video.min_bandwidth = QuicBandwidth::FromKBitsPerSecond(25);
  video.max_bandwidth = QuicBandwidth::FromKBitsPerSecond(5000);

  // Note: by placing audio first, it takes priority in bandwidth allocations.
  client_configs_.push_back(audio);
  client_configs_.push_back(video);
  server_configs_.push_back(audio);
  server_configs_.push_back(video);
}

BidiTestRunner::~BidiTestRunner() {
  // Note that peers must be deleted before endpoints.  Peers close the
  // connection when deleted.
  client_peer_.reset();
  server_peer_.reset();
}

bool BidiTestRunner::RunTest(QuicTime::Delta test_duration) {
  client_peer_ = std::make_unique<QuartcPeer>(
      simulator_->GetClock(), simulator_->GetAlarmFactory(),
      simulator_->GetRandomGenerator(),
      simulator_->GetStreamSendBufferAllocator(), client_configs_);
  server_peer_ = std::make_unique<QuartcPeer>(
      simulator_->GetClock(), simulator_->GetAlarmFactory(),
      simulator_->GetRandomGenerator(),
      simulator_->GetStreamSendBufferAllocator(), server_configs_);

  QuartcEndpoint::Delegate* server_delegate = server_peer_.get();
  if (server_interceptor_) {
    server_interceptor_->SetDelegate(server_delegate);
    server_delegate = server_interceptor_;
  }
  server_endpoint_ = std::make_unique<QuartcServerEndpoint>(
      simulator_->GetAlarmFactory(), simulator_->GetClock(),
      simulator_->GetRandomGenerator(), server_delegate, QuartcSessionConfig());

  QuartcEndpoint::Delegate* client_delegate = client_peer_.get();
  if (client_interceptor_) {
    client_interceptor_->SetDelegate(client_delegate);
    client_delegate = client_interceptor_;
  }
  client_endpoint_ = std::make_unique<QuartcClientEndpoint>(
      simulator_->GetAlarmFactory(), simulator_->GetClock(),
      simulator_->GetRandomGenerator(), client_delegate, QuartcSessionConfig(),
      server_endpoint_->server_crypto_config());

  QuicTime start_time = simulator_->GetClock()->Now();
  server_endpoint_->Connect(server_transport_);
  client_endpoint_->Connect(client_transport_);

  // Measure connect latency.
  if (!simulator_->RunUntil([this] { return client_peer_->Enabled(); })) {
    return false;
  }
  QuicTime client_connected = simulator_->GetClock()->Now();
  QuicTime::Delta client_connect_latency = client_connected - start_time;

  if (!simulator_->RunUntil([this] { return server_peer_->Enabled(); })) {
    return false;
  }
  QuicTime server_connected = simulator_->GetClock()->Now();
  QuicTime::Delta server_connect_latency = server_connected - start_time;

  QUIC_LOG(INFO) << "Connect latencies (ms): client=" << client_connect_latency
                 << ", server=" << server_connect_latency;

  // Run the test.
  simulator_->RunFor(test_duration);

  // Disable sending and drain.
  // Note that draining by waiting for the last sequence number sent may be
  // flaky if packet loss is enabled.  However, simulator-based tests don't
  // currently have any loss.
  server_peer_->SetEnabled(false);
  client_peer_->SetEnabled(false);

  if (!simulator_->RunUntil([this] { return PacketsDrained(); })) {
    return false;
  }

  // Compute results.
  QUIC_LOG(INFO) << "Printing client->server results:";
  LogResults(server_peer_->received_messages(),
             client_peer_->GetLastSequenceNumbers());

  QUIC_LOG(INFO) << "Printing server->client results:";
  LogResults(client_peer_->received_messages(),
             server_peer_->GetLastSequenceNumbers());
  return true;
}

bool BidiTestRunner::PacketsDrained() {
  const ReceivedMessage& last_server_message =
      server_peer_->received_messages().back();
  const ReceivedMessage& last_client_message =
      client_peer_->received_messages().back();

  // Last observed propagation delay on the client -> server path.
  QuicTime::Delta last_client_server_delay =
      last_server_message.receive_time - last_server_message.frame.send_time;

  // Last observed propagation delay on the server -> client path.
  QuicTime::Delta last_server_client_delay =
      last_client_message.receive_time - last_client_message.frame.send_time;

  // Last observed RTT based on the propagation delays above.
  QuicTime::Delta last_rtt =
      last_client_server_delay + last_server_client_delay;

  // If nothing interesting has happened for at least one RTT, then it's
  // unlikely anything is still in flight.
  QuicTime now = simulator_->GetClock()->Now();
  return now - last_server_message.receive_time > last_rtt &&
         now - last_client_message.receive_time > last_rtt;
}

}  // namespace test
}  // namespace quic
