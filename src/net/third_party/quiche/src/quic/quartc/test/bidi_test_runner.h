// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QUARTC_TEST_BIDI_TEST_RUNNER_H_
#define QUICHE_QUIC_QUARTC_TEST_BIDI_TEST_RUNNER_H_

#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_endpoint.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_packet_writer.h"
#include "net/third_party/quiche/src/quic/quartc/test/quartc_data_source.h"
#include "net/third_party/quiche/src/quic/quartc/test/quartc_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/simulator.h"

namespace quic {
namespace test {

// Interface for a component that intercepts endpoint callbacks before
// forwarding them to another delegate.
class QuartcEndpointInterceptor : public QuartcEndpoint::Delegate {
 public:
  ~QuartcEndpointInterceptor() override = default;

  // Passes the test's endpoint delegate to this interceptor.  The interceptor
  // must forward all callbacks to this delegate as soon as it finishes handling
  // them.
  virtual void SetDelegate(QuartcEndpoint::Delegate* delegate) = 0;
};

// Runner for bidirectional media flow tests.
//
// BidiTestRunner allows an external fixture to set up transports, then executes
// a test.  During the test, it sets up two QuartcPeers, connects them through
// the transports, and sends data in both directions for a specified duration.
// It then stops sending, waits for any pending messages to finish transmission,
// and then computes and logs a few basic metrics.
//
// For now, the runner computes the maximum and average one-way delay, the total
// throughput (in bytes) and the average bandwidth (in bits per second).  It
// logs these to the test's text logs.
//
// By default, the BidiTestRunner emulates one video stream and one audio stream
// in each direction.  The audio stream runs with a 20 ms ptime, between 8 and
// 64 kbps.  The video stream runs at 30 fps, between 25 kbps and 5 mbps.
// Individual tests can overwrite the configs.
//
// BidiTestRunner provides a way for the test to register an "interceptor" on
// each endpoint.  This allows a test to reconfigure that endpoint's session
// prior to beginning the test.  For example, interceptors may be used to attach
// debug visitors or change the congestion controller.
class BidiTestRunner {
 public:
  // TODO(b/130540842): Make this compatible with non-simulator execution.
  BidiTestRunner(simulator::Simulator* simulator,
                 QuartcPacketTransport* client_transport,
                 QuartcPacketTransport* server_transport);

  virtual ~BidiTestRunner();

  void set_client_configs(std::vector<QuartcDataSource::Config> configs) {
    client_configs_ = std::move(configs);
  }

  void set_server_configs(std::vector<QuartcDataSource::Config> configs) {
    server_configs_ = std::move(configs);
  }

  void set_client_interceptor(QuartcEndpointInterceptor* interceptor) {
    client_interceptor_ = interceptor;
  }

  void set_server_interceptor(QuartcEndpointInterceptor* interceptor) {
    server_interceptor_ = interceptor;
  }

  virtual bool RunTest(QuicTime::Delta test_duration);

 private:
  // Returns true when no pending packets are believed to be in-flight.
  bool PacketsDrained();

  simulator::Simulator* simulator_;
  QuartcPacketTransport* client_transport_;
  QuartcPacketTransport* server_transport_;

  std::vector<QuartcDataSource::Config> client_configs_;
  std::vector<QuartcDataSource::Config> server_configs_;

  QuartcEndpointInterceptor* client_interceptor_ = nullptr;
  QuartcEndpointInterceptor* server_interceptor_ = nullptr;

  std::unique_ptr<QuartcServerEndpoint> server_endpoint_;
  std::unique_ptr<QuartcClientEndpoint> client_endpoint_;

  std::unique_ptr<QuartcPeer> client_peer_;
  std::unique_ptr<QuartcPeer> server_peer_;
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_QUARTC_TEST_BIDI_TEST_RUNNER_H_
