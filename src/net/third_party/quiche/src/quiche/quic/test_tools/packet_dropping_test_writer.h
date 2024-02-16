// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_PACKET_DROPPING_TEST_WRITER_H_
#define QUICHE_QUIC_TEST_TOOLS_PACKET_DROPPING_TEST_WRITER_H_

#include <cstdint>
#include <list>
#include <memory>

#include "absl/base/attributes.h"
#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_packet_writer_wrapper.h"
#include "quiche/quic/platform/api/quic_mutex.h"
#include "quiche/quic/test_tools/quic_test_utils.h"

namespace quic {
namespace test {

// Simulates a connection that drops packets a configured percentage of the time
// and has a blocked socket a configured percentage of the time.  Also provides
// the options to delay packets and reorder packets if delay is enabled.
class PacketDroppingTestWriter : public QuicPacketWriterWrapper {
 public:
  class Delegate {
   public:
    virtual ~Delegate() {}
    virtual void OnCanWrite() = 0;
  };

  PacketDroppingTestWriter();
  PacketDroppingTestWriter(const PacketDroppingTestWriter&) = delete;
  PacketDroppingTestWriter& operator=(const PacketDroppingTestWriter&) = delete;

  ~PacketDroppingTestWriter() override;

  // Must be called before blocking, reordering or delaying (loss is OK). May be
  // called after connecting if the helper is not available before.
  // |on_can_write| will be triggered when fake-unblocking.
  void Initialize(QuicConnectionHelperInterface* helper,
                  QuicAlarmFactory* alarm_factory,
                  std::unique_ptr<Delegate> on_can_write);

  // QuicPacketWriter methods:
  WriteResult WritePacket(const char* buffer, size_t buf_len,
                          const QuicIpAddress& self_address,
                          const QuicSocketAddress& peer_address,
                          PerPacketOptions* options,
                          const QuicPacketWriterParams& params) override;

  bool IsWriteBlocked() const override;

  void SetWritable() override;

  QuicPacketBuffer GetNextWriteLocation(
      const QuicIpAddress& /*self_address*/,
      const QuicSocketAddress& /*peer_address*/) override {
    // If the wrapped writer supports zero-copy, disable it, because it is not
    // compatible with delayed writes in this class.
    return {nullptr, nullptr};
  }

  // Writes out any packet which should have been sent by now
  // to the contained writer and returns the time
  // for the next delayed packet to be written.
  QuicTime ReleaseOldPackets();

  // Sets |delay_alarm_| to fire at |new_deadline|.
  void SetDelayAlarm(QuicTime new_deadline);

  void OnCanWrite();

  // The percent of time a packet is simulated as being lost.
  // If |fake_packet_loss_percentage| is 100, then all packages are lost.
  // Otherwise actual percentage will be lower than
  // |fake_packet_loss_percentage|, because every dropped package is followed by
  // a minimum number of successfully written packets.
  void set_fake_packet_loss_percentage(int32_t fake_packet_loss_percentage) {
    QuicWriterMutexLock lock(&config_mutex_);
    fake_packet_loss_percentage_ = fake_packet_loss_percentage;
  }

  // Once called, the next |passthrough_for_next_n_packets_| WritePacket() calls
  // will always send the packets immediately, without being affected by the
  // simulated error conditions.
  void set_passthrough_for_next_n_packets(
      uint32_t passthrough_for_next_n_packets) {
    QuicWriterMutexLock lock(&config_mutex_);
    passthrough_for_next_n_packets_ = passthrough_for_next_n_packets;
  }

  // Simulate dropping the first n packets unconditionally.
  // Subsequent packets will be lost at fake_packet_loss_percentage_ if set.
  void set_fake_drop_first_n_packets(int32_t fake_drop_first_n_packets) {
    QuicWriterMutexLock lock(&config_mutex_);
    fake_drop_first_n_packets_ = fake_drop_first_n_packets;
  }

  // The percent of time WritePacket will block and set WriteResult's status
  // to WRITE_STATUS_BLOCKED.
  void set_fake_blocked_socket_percentage(
      int32_t fake_blocked_socket_percentage) {
    QUICHE_DCHECK(clock_);
    QuicWriterMutexLock lock(&config_mutex_);
    fake_blocked_socket_percentage_ = fake_blocked_socket_percentage;
  }

  // The percent of time a packet is simulated as being reordered.
  void set_fake_reorder_percentage(int32_t fake_packet_reorder_percentage) {
    QUICHE_DCHECK(clock_);
    QuicWriterMutexLock lock(&config_mutex_);
    QUICHE_DCHECK(!fake_packet_delay_.IsZero());
    fake_packet_reorder_percentage_ = fake_packet_reorder_percentage;
  }

  // The delay before writing this packet.
  void set_fake_packet_delay(QuicTime::Delta fake_packet_delay) {
    QUICHE_DCHECK(clock_);
    QuicWriterMutexLock lock(&config_mutex_);
    fake_packet_delay_ = fake_packet_delay;
  }

  // The maximum bandwidth and buffer size of the connection.  When these are
  // set, packets will be delayed until a connection with that bandwidth would
  // transmit it.  Once the |buffer_size| is reached, all new packets are
  // dropped.
  void set_max_bandwidth_and_buffer_size(QuicBandwidth fake_bandwidth,
                                         QuicByteCount buffer_size) {
    QUICHE_DCHECK(clock_);
    QuicWriterMutexLock lock(&config_mutex_);
    fake_bandwidth_ = fake_bandwidth;
    buffer_size_ = buffer_size;
  }

  // Useful for reproducing very flaky issues.
  ABSL_ATTRIBUTE_UNUSED void set_seed(uint64_t seed) {
    simple_random_.set_seed(seed);
  }

 private:
  // Writes out the next packet to the contained writer and returns the time
  // for the next delayed packet to be written.
  QuicTime ReleaseNextPacket();

  // A single packet which will be sent at the supplied send_time.
  struct DelayedWrite {
   public:
    DelayedWrite(const char* buffer, size_t buf_len,
                 const QuicIpAddress& self_address,
                 const QuicSocketAddress& peer_address,
                 std::unique_ptr<PerPacketOptions> options,
                 const QuicPacketWriterParams& params, QuicTime send_time);
    DelayedWrite(const DelayedWrite&) = delete;
    DelayedWrite(DelayedWrite&&) = default;
    DelayedWrite& operator=(const DelayedWrite&) = delete;
    DelayedWrite& operator=(DelayedWrite&&) = default;
    ~DelayedWrite();

    std::string buffer;
    QuicIpAddress self_address;
    QuicSocketAddress peer_address;
    std::unique_ptr<PerPacketOptions> options;
    QuicPacketWriterParams params;
    QuicTime send_time;
  };

  using DelayedPacketList = std::list<DelayedWrite>;

  const QuicClock* clock_;
  std::unique_ptr<QuicAlarm> write_unblocked_alarm_;
  std::unique_ptr<QuicAlarm> delay_alarm_;
  std::unique_ptr<Delegate> on_can_write_;
  SimpleRandom simple_random_;
  // Stored packets delayed by fake packet delay or bandwidth restrictions.
  DelayedPacketList delayed_packets_;
  QuicByteCount cur_buffer_size_;
  uint64_t num_calls_to_write_;
  uint32_t passthrough_for_next_n_packets_ QUIC_GUARDED_BY(config_mutex_);
  int32_t num_consecutive_succesful_writes_;

  QuicMutex config_mutex_;
  int32_t fake_packet_loss_percentage_ QUIC_GUARDED_BY(config_mutex_);
  int32_t fake_drop_first_n_packets_ QUIC_GUARDED_BY(config_mutex_);
  int32_t fake_blocked_socket_percentage_ QUIC_GUARDED_BY(config_mutex_);
  int32_t fake_packet_reorder_percentage_ QUIC_GUARDED_BY(config_mutex_);
  QuicTime::Delta fake_packet_delay_ QUIC_GUARDED_BY(config_mutex_);
  QuicBandwidth fake_bandwidth_ QUIC_GUARDED_BY(config_mutex_);
  QuicByteCount buffer_size_ QUIC_GUARDED_BY(config_mutex_);
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_PACKET_DROPPING_TEST_WRITER_H_
