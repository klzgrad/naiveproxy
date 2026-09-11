// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_BONNET_ASYNC_WRITE_PACKET_EXCHANGER_H_
#define QUICHE_QUIC_QBONE_BONNET_ASYNC_WRITE_PACKET_EXCHANGER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <queue>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "quiche/quic/qbone/bonnet/qbone_client_packet_exchanger.h"
#include "quiche/quic/qbone/bonnet/tun_device_packet_exchanger.h"
#include "quiche/quic/qbone/platform/kernel_interface.h"
#include "quiche/quic/qbone/platform/netlink_interface.h"
#include "quiche/common/quiche_callbacks.h"

namespace quic {

// Packet exchanger that handles write operations asynchronously on a separate
// worker thread.
class AsyncWritePacketExchanger : public QboneClientPacketExchanger {
 public:
  // Executor that runs given callbacks on the main silo thread (the thread that
  // owns and interacts with the exchanger).
  class SiloExecutor {
   public:
    virtual ~SiloExecutor() = default;

    // Stops the executor and either blocks until all pending callbacks are
    // completed or cancels them. No callbacks will be executed after this
    // returns. Must be called from the silo thread.
    virtual void Stop() = 0;

    // Return true iff called from the silo thread. Thread-safe.
    virtual bool IsOnSiloThread() const = 0;

    // Schedules the given callback to be executed on the silo thread. No
    // guarantees about when the callback will be executed or the order in
    // which it will be executed relative to other callbacks, but all callbacks
    // will eventually be executed if Stop() is not called. Thread-safe.
    virtual void ExecuteInSilo(quiche::SingleUseCallback<void()> callback) = 0;
  };

  AsyncWritePacketExchanger(
      size_t mtu,
      KernelInterface* absl_nonnull kernel ABSL_ATTRIBUTE_LIFETIME_BOUND,
      NetlinkInterface* absl_nonnull netlink ABSL_ATTRIBUTE_LIFETIME_BOUND,
      Visitor* absl_nonnull visitor ABSL_ATTRIBUTE_LIFETIME_BOUND, bool is_tap,
      absl::string_view ifname, int64_t max_buffer_size_bytes,
      int64_t max_result_buffer_size_bytes,
      absl_nonnull std::unique_ptr<SiloExecutor> silo_executor);
  ~AsyncWritePacketExchanger() override;

  // QboneClientPacketExchanger:
  void Start(int read_fd, int write_fd,
             QboneClientPacketExchanger* absl_nullable exchanger) override;
  void Stop() override;
  int OnReadFromNetworkReady(int max_packets_to_read) override;
  void WritePacketToNetwork(absl::Span<const std::byte> packet) override;

 private:
  class WriteThread;
  class IntermediateVisitor;

  struct InternalWriteResult {
    std::vector<std::byte> packet;
    absl::Duration latency;
  };

  void OnWriteResultsReadyForSiloThread();

  const int64_t max_buffer_size_bytes_;
  const int64_t max_result_buffer_size_bytes_;
  absl_nonnull std::unique_ptr<SiloExecutor> silo_executor_;
  Visitor& visitor_;

  std::unique_ptr<IntermediateVisitor> intermediate_visitor_;
  TunDevicePacketExchanger thread_local_exchanger_;

  absl::Mutex write_queue_mutex_;
  int64_t write_queue_size_ ABSL_GUARDED_BY(write_queue_mutex_) = 0;
  std::queue<std::vector<std::byte>> write_queue_
      ABSL_GUARDED_BY(write_queue_mutex_);

  absl::Mutex result_queue_mutex_;
  int64_t result_queue_size_ ABSL_GUARDED_BY(result_queue_mutex_) = 0;
  std::queue<absl::StatusOr<InternalWriteResult>> result_queue_
      ABSL_GUARDED_BY(result_queue_mutex_);

  std::unique_ptr<WriteThread> write_thread_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_BONNET_ASYNC_WRITE_PACKET_EXCHANGER_H_
