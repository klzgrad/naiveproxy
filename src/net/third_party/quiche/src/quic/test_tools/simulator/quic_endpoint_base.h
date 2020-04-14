// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_SIMULATOR_QUIC_ENDPOINT_BASE_H_
#define QUICHE_QUIC_TEST_TOOLS_SIMULATOR_QUIC_ENDPOINT_BASE_H_

#include <memory>

#include "net/third_party/quiche/src/quic/core/crypto/null_decrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/null_encrypter.h"
#include "net/third_party/quiche/src/quic/core/quic_connection.h"
#include "net/third_party/quiche/src/quic/core/quic_default_packet_writer.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_stream_frame_data_producer.h"
#include "net/third_party/quiche/src/quic/core/quic_trace_visitor.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_containers.h"
#include "net/third_party/quiche/src/quic/test_tools/simple_session_notifier.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/link.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/queue.h"

namespace quic {
namespace simulator {

// Size of the TX queue used by the kernel/NIC.  1000 is the Linux
// kernel default.
const QuicByteCount kTxQueueSize = 1000;

// Generate a random local network host-port tuple based on the name of the
// endpoint.
QuicSocketAddress GetAddressFromName(std::string name);

// A QUIC connection endpoint.  If the specific data transmitted does not matter
// (e.g. for congestion control purposes), QuicEndpoint is the subclass that
// transmits dummy data.  If the actual semantics of the connection matter,
// subclassing QuicEndpointBase is required.
class QuicEndpointBase : public Endpoint,
                         public UnconstrainedPortInterface,
                         public Queue::ListenerInterface {
 public:
  // Does not create the connection; the subclass has to create connection by
  // itself.
  QuicEndpointBase(Simulator* simulator,
                   std::string name,
                   std::string peer_name);
  ~QuicEndpointBase() override;

  inline QuicConnection* connection() { return connection_.get(); }
  inline size_t write_blocked_count() { return write_blocked_count_; }

  // Drop the next packet upon receipt.
  void DropNextIncomingPacket();

  // UnconstrainedPortInterface method.  Called whenever the endpoint receives a
  // packet.
  void AcceptPacket(std::unique_ptr<Packet> packet) override;

  // Enables logging of the connection trace at the end of the unit test.
  void RecordTrace();

  // Begin Endpoint implementation.
  UnconstrainedPortInterface* GetRxPort() override;
  void SetTxPort(ConstrainedPortInterface* port) override;
  // End Endpoint implementation.

  // Actor method.
  void Act() override {}

  // Queue::ListenerInterface method.
  void OnPacketDequeued() override;

 protected:
  // A Writer object that writes into the |nic_tx_queue_|.
  class Writer : public QuicPacketWriter {
   public:
    explicit Writer(QuicEndpointBase* endpoint);
    ~Writer() override;

    WriteResult WritePacket(const char* buffer,
                            size_t buf_len,
                            const QuicIpAddress& self_address,
                            const QuicSocketAddress& peer_address,
                            PerPacketOptions* options) override;
    bool IsWriteBlocked() const override;
    void SetWritable() override;
    QuicByteCount GetMaxPacketSize(
        const QuicSocketAddress& peer_address) const override;
    bool SupportsReleaseTime() const override;
    bool IsBatchMode() const override;
    char* GetNextWriteLocation(const QuicIpAddress& self_address,
                               const QuicSocketAddress& peer_address) override;
    WriteResult Flush() override;

   private:
    QuicEndpointBase* endpoint_;

    bool is_blocked_;
  };

  // The producer outputs the repetition of the same byte.  That sequence is
  // verified by the receiver.
  class DataProducer : public QuicStreamFrameDataProducer {
   public:
    WriteStreamDataResult WriteStreamData(QuicStreamId id,
                                          QuicStreamOffset offset,
                                          QuicByteCount data_length,
                                          QuicDataWriter* writer) override;
    bool WriteCryptoData(EncryptionLevel level,
                         QuicStreamOffset offset,
                         QuicByteCount data_length,
                         QuicDataWriter* writer) override;
  };

  std::string peer_name_;

  Writer writer_;
  // The queue for the outgoing packets.  In reality, this might be either on
  // the network card, or in the kernel, but for concreteness we assume it's on
  // the network card.
  Queue nic_tx_queue_;
  // Created by the subclass.
  std::unique_ptr<QuicConnection> connection_;

  // Counts the number of times the writer became write-blocked.
  size_t write_blocked_count_;

  // If true, drop the next packet when receiving it.
  bool drop_next_packet_;

  std::unique_ptr<QuicTraceVisitor> trace_visitor_;
};

// Multiplexes multiple connections at the same host on the network.
class QuicEndpointMultiplexer : public Endpoint,
                                public UnconstrainedPortInterface {
 public:
  QuicEndpointMultiplexer(std::string name,
                          const std::vector<QuicEndpointBase*>& endpoints);
  ~QuicEndpointMultiplexer() override;

  // Receives a packet and passes it to the specified endpoint if that endpoint
  // is one of the endpoints being multiplexed, otherwise ignores the packet.
  void AcceptPacket(std::unique_ptr<Packet> packet) override;
  UnconstrainedPortInterface* GetRxPort() override;

  // Sets the egress port for all the endpoints being multiplexed.
  void SetTxPort(ConstrainedPortInterface* port) override;

  void Act() override {}

 private:
  QuicUnorderedMap<std::string, QuicEndpointBase*> mapping_;
};

}  // namespace simulator
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_SIMULATOR_QUIC_ENDPOINT_BASE_H_
