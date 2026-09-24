// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/qbone/bonnet/async_write_packet_exchanger.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <queue>
#include <thread>  // NOLINT (for open-sourceable thread ID)
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/types/span.h"
#include "quiche/quic/platform/api/quic_thread.h"
#include "quiche/quic/qbone/bonnet/qbone_client_packet_exchanger.h"
#include "quiche/quic/qbone/platform/kernel_interface.h"
#include "quiche/quic/qbone/platform/netlink_interface.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quic {

class AsyncWritePacketExchanger::WriteThread : public QuicThread {
 public:
  WriteThread(QboneClientPacketExchanger* absl_nonnull thread_local_exchanger,
              AsyncWritePacketExchanger* absl_nonnull async_exchanger)
      : QuicThread("AsyncWritePacketExchanger::WriteThread"),
        thread_local_exchanger_(*thread_local_exchanger),
        async_exchanger_(*async_exchanger) {}

  void CompleteWritesAndStop() {
    QUICHE_DCHECK(async_exchanger_.silo_executor_->IsOnSiloThread());
    {
      absl::MutexLock lock(async_exchanger_.write_queue_mutex_);
      stop_requested_ = true;
    }
    Join();
  }

  bool IsRunningOnThread() const {
    return thread_started_.HasBeenNotified() &&
           std::this_thread::get_id() == thread_id_;
  }

  void ProcessWriteResults(absl::StatusOr<std::vector<WriteResult>> results) {
    QUICHE_DCHECK(IsRunningOnThread());

    if (results.ok()) {
      for (const WriteResult& result : *results) {
        auto packet_node = in_flight_writes_.extract(result.packet.data());
        if (packet_node.empty()) {
          // Unexpected. Assuming `thread_local_exchanger_` should always pass
          // results to the visitor during the WritePacketToNetwork() call and
          // that the result packet should point to the original packet buffer
          // stored in `in_flight_writes_`.
          QUICHE_BUG(qbone_async_write_packet_exchanger_write_result_not_found);
          continue;
        }

        ready_results_.push(
            InternalWriteResult{.packet = std::move(packet_node.mapped()),
                                .latency = result.latency});
      }
    } else {
      in_flight_error_count_++;
      ready_results_.push(results.status());
    }
  }

 protected:
  void Run() override {
    QUICHE_DCHECK_EQ(thread_id_, std::thread::id());

    thread_id_ = std::this_thread::get_id();
    thread_started_.Notify();

    while (true) {
      std::queue<std::vector<std::byte>> packets_to_write =
          GetPacketsToWriteCriticalSection();

      if (packets_to_write.empty()) {
        break;
      }

      while (!packets_to_write.empty()) {
        // Store packet buffers in `in_flight_writes_` during the write. Should
        // be unnecessary, assuming `thread_local_exchanger_` always completes
        // the write and calls the visitor synchronously, but this allows for
        // validating that the received write results are for the expected
        // packets.
        absl::Span<const std::byte> packet_span = packets_to_write.front();
        QUICHE_DCHECK(packet_span.data() != nullptr);
        in_flight_writes_[packet_span.data()] =
            std::move(packets_to_write.front());
        QUICHE_DCHECK_EQ(in_flight_writes_[packet_span.data()].data(),
                         packet_span.data());
        thread_local_exchanger_.WritePacketToNetwork(packet_span);
        packets_to_write.pop();
      }

      // Abandon any packets that failed to write.
      QUICHE_BUG_IF(qbone_async_write_packet_exchanger_missing_write_results,
                    in_flight_writes_.size() != in_flight_error_count_);
      in_flight_writes_.clear();
      in_flight_error_count_ = 0;

      if (!ready_results_.empty()) {
        PassResultsCriticalSection();

        // Trigger the silo thread to process the results and pass out to the
        // visitor. This may trigger unnecessary extra calls if this happens
        // multiple times before the silo thread executes the task, but it
        // should be safe and have negligible impact on performance, so we won't
        // worry about tracking when the call is or isn't necessary.
        //
        // Use a direct reference to the exchanger because `this` may be stopped
        // and destroyed before the lambda is executed. But we assume the
        // exchanger itself is safe because it owns the executor and stops its
        // execution on shutdown.
        AsyncWritePacketExchanger* exchanger = &async_exchanger_;
        async_exchanger_.silo_executor_->ExecuteInSilo(
            [exchanger]() { exchanger->OnWriteResultsReadyForSiloThread(); });
      }
    }

    {
      absl::MutexLock lock(async_exchanger_.write_queue_mutex_);
      QUICHE_DCHECK(stop_requested_);
    }
  }

 private:
  bool IsQueueNonEmptyOrStopRequested() const
      ABSL_SHARED_LOCKS_REQUIRED(async_exchanger_.write_queue_mutex_) {
    return !async_exchanger_.write_queue_.empty() || stop_requested_;
  }

  // May return an empty queue if stop is requested. Otherwise, blocks until the
  // queue is non-empty.
  std::queue<std::vector<std::byte>> GetPacketsToWriteCriticalSection() {
    std::queue<std::vector<std::byte>> packets_to_write;

    absl::MutexLock lock(
        async_exchanger_.write_queue_mutex_,
        absl::Condition(this, &WriteThread::IsQueueNonEmptyOrStopRequested));

    if (async_exchanger_.write_queue_.empty()) {
      return {};
    } else {
      packets_to_write.swap(async_exchanger_.write_queue_);
      async_exchanger_.write_queue_size_ = 0;
      return packets_to_write;
    }
  }

  void PassResultsCriticalSection() {
    absl::MutexLock lock(async_exchanger_.result_queue_mutex_);

    while (!ready_results_.empty()) {
      // For simplicity, only track packet size in the result queue, rather than
      // complete size of the result struct. Expect that to be the only part
      // with significant memory usage.
      int64_t packet_size = ready_results_.front().ok()
                                ? ready_results_.front()->packet.size()
                                : 0;
      int64_t new_queue_size =
          async_exchanger_.result_queue_size_ + packet_size;
      if (new_queue_size <= async_exchanger_.max_result_buffer_size_bytes_) {
        async_exchanger_.result_queue_size_ = new_queue_size;
        async_exchanger_.result_queue_.push(std::move(ready_results_.front()));
      } else {
        // Result passing is even more best-effort than packet writing. If the
        // results queue is full, drop the result.
        QUICHE_LOG_EVERY_N_SEC(WARNING, 5)
            << "Result queue is full, dropping result.";
      }

      ready_results_.pop();
    }
  }

  QboneClientPacketExchanger& thread_local_exchanger_;
  AsyncWritePacketExchanger& async_exchanger_;

  absl::flat_hash_map<const std::byte*, std::vector<std::byte>>
      in_flight_writes_;
  int in_flight_error_count_ = 0;
  std::queue<absl::StatusOr<AsyncWritePacketExchanger::InternalWriteResult>>
      ready_results_;

  absl::Notification thread_started_;
  bool stop_requested_ ABSL_GUARDED_BY(async_exchanger_.write_queue_mutex_) =
      false;

  // Thread-safe to read after notification of `thread_started_`.
  std::thread::id thread_id_;
};

class AsyncWritePacketExchanger::IntermediateVisitor : public Visitor {
 public:
  IntermediateVisitor(AsyncWritePacketExchanger* absl_nonnull async_exchanger)
      : async_exchanger_(*async_exchanger) {}

  void OnRead(absl::StatusOr<std::vector<ReadResult>> results) override {
    // Expect read to occur on the silo thread, so results can be passed through
    // directly.
    QUICHE_BUG_IF(qbone_async_write_packet_exchanger_read_result_wrong_thread,
                  !async_exchanger_.silo_executor_->IsOnSiloThread());
    async_exchanger_.visitor_.OnRead(std::move(results));
  }

  void OnWrite(absl::StatusOr<std::vector<WriteResult>> results) override {
    // Expect write to occur on the separate write thread.
    QUICHE_BUG_IF(qbone_async_write_packet_exchanger_write_result_wrong_thread,
                  !async_exchanger_.write_thread_ ||
                      !async_exchanger_.write_thread_->IsRunningOnThread());
    async_exchanger_.write_thread_->ProcessWriteResults(std::move(results));
  }

 private:
  AsyncWritePacketExchanger& async_exchanger_;
};

AsyncWritePacketExchanger::AsyncWritePacketExchanger(
    size_t mtu, KernelInterface* absl_nonnull kernel,
    NetlinkInterface* absl_nonnull netlink, Visitor* absl_nonnull visitor,
    bool is_tap, absl::string_view ifname, int64_t max_buffer_size_bytes,
    int64_t max_result_buffer_size_bytes,
    absl_nonnull std::unique_ptr<SiloExecutor> silo_executor)
    : max_buffer_size_bytes_(max_buffer_size_bytes),
      max_result_buffer_size_bytes_(max_result_buffer_size_bytes),
      silo_executor_(std::move(silo_executor)),
      visitor_(*visitor),
      intermediate_visitor_(std::make_unique<IntermediateVisitor>(this)),
      thread_local_exchanger_(mtu, kernel, netlink, intermediate_visitor_.get(),
                              is_tap, ifname) {}

AsyncWritePacketExchanger::~AsyncWritePacketExchanger() {
  QUICHE_DCHECK(silo_executor_->IsOnSiloThread());
  QUICHE_BUG_IF(qbone_async_write_packet_exchanger_not_stopped,
                write_thread_ != nullptr);

  Stop();
  silo_executor_->Stop();
}

void AsyncWritePacketExchanger::Start(
    int read_fd, int write_fd,
    QboneClientPacketExchanger* absl_nullable exchanger) {
  QUICHE_DCHECK(silo_executor_->IsOnSiloThread());

  if (exchanger == nullptr) {
    exchanger = this;
  }

  thread_local_exchanger_.Start(read_fd, write_fd, exchanger);

  // Allow idempotent Start() calls (assuming `thread_local_exchanger_`
  // validates same inputs).
  if (!write_thread_) {
    write_thread_ =
        std::make_unique<WriteThread>(&thread_local_exchanger_, this);
    write_thread_->Start();
  }
}

void AsyncWritePacketExchanger::Stop() {
  QUICHE_DCHECK(silo_executor_->IsOnSiloThread());

  if (write_thread_) {
    write_thread_->CompleteWritesAndStop();
  }
  thread_local_exchanger_.Stop();

  // Flush any pending write results to visitor. Along with stopping the write
  // thread to ensure no more results are added, this will ensure that if
  // `silo_executor_` has any pending calls to run
  // OnWriteResultsReadyForSiloThread(), it will safely find an empty results
  // queue and not call `visitor_` callbacks.
  OnWriteResultsReadyForSiloThread();

  write_thread_.reset();
}

int AsyncWritePacketExchanger::OnReadFromNetworkReady(int max_packets_to_read) {
  QUICHE_DCHECK(silo_executor_->IsOnSiloThread());
  return thread_local_exchanger_.OnReadFromNetworkReady(max_packets_to_read);
}

void AsyncWritePacketExchanger::WritePacketToNetwork(
    absl::Span<const std::byte> packet) {
  QUICHE_DCHECK(silo_executor_->IsOnSiloThread());

  if (packet.empty()) {
    QUICHE_BUG(qbone_async_write_packet_exchanger_write_empty_packet);
    return;
  }

  if (!write_thread_) {
    QUICHE_BUG(qbone_async_write_packet_exchanger_write_not_started);
    return;
  }

  // This will waste time copying if the write queue is full, but that should be
  // an exceptional case. Better to not hold the lock while copying.
  std::vector<std::byte> packet_copy(packet.begin(), packet.end());

  bool buffer_full = false;
  {
    absl::MutexLock lock(write_queue_mutex_);
    if (write_queue_size_ + packet_copy.size() <= max_buffer_size_bytes_) {
      write_queue_size_ += packet_copy.size();
      write_queue_.push(std::move(packet_copy));
    } else {
      buffer_full = true;
    }
  }

  // QBONE packet delivery is best-effort, so if the buffer is full, drop the
  // packet. No attempt to wait/retry.
  if (buffer_full) {
    visitor_.OnWrite(absl::ResourceExhaustedError(
        "AsyncWritePacketExchanger::WritePacketToNetwork: buffer full"));
  }
}

void AsyncWritePacketExchanger::OnWriteResultsReadyForSiloThread() {
  QUICHE_DCHECK(silo_executor_->IsOnSiloThread());

  std::vector<absl::StatusOr<InternalWriteResult>> results;
  {
    absl::MutexLock lock(result_queue_mutex_);
    results.reserve(result_queue_.size());
    while (!result_queue_.empty()) {
      results.push_back(std::move(result_queue_.front()));
      result_queue_.pop();
    }
    result_queue_size_ = 0;
  }

  std::vector<WriteResult> write_results;
  std::vector<absl::Status> errors;
  for (const auto& result : results) {
    if (result.ok()) {
      write_results.push_back(
          WriteResult{.packet = result->packet, .latency = result->latency});
    } else {
      errors.push_back(result.status());
    }
  }

  // Report all successful writes in one batch for efficiency. Errors are less
  // common, and we don't optimize for them.
  if (!write_results.empty()) {
    QUICHE_DCHECK(write_thread_);  // Expect no results after stop.
    visitor_.OnWrite(std::move(write_results));
  }
  for (const auto& error : errors) {
    QUICHE_DCHECK(write_thread_);  // Expect no errors after stop.
    visitor_.OnWrite(error);
  }
}

}  // namespace quic
