/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/profiling/memory/unwinding.h"

#include <sys/types.h>
#include <unistd.h>

#include <condition_variable>
#include <mutex>

#include <unwindstack/MachineArm.h>
#include <unwindstack/MachineArm64.h>
#include <unwindstack/MachineRiscv64.h>
#include <unwindstack/MachineX86.h>
#include <unwindstack/MachineX86_64.h>
#include <unwindstack/Maps.h>
#include <unwindstack/Memory.h>
#include <unwindstack/Regs.h>
#include <unwindstack/RegsArm.h>
#include <unwindstack/RegsArm64.h>
#include <unwindstack/RegsRiscv64.h>
#include <unwindstack/RegsX86.h>
#include <unwindstack/RegsX86_64.h>
#include <unwindstack/Unwinder.h>
#include <unwindstack/UserArm.h>
#include <unwindstack/UserArm64.h>
#include <unwindstack/UserRiscv64.h>
#include <unwindstack/UserX86.h>
#include <unwindstack/UserX86_64.h>

#include <procinfo/process_map.h>

#include "perfetto/base/logging.h"
#include "perfetto/base/task_runner.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/thread_task_runner.h"

#include "src/profiling/memory/unwound_messages.h"
#include "src/profiling/memory/wire_protocol.h"

namespace perfetto {
namespace profiling {
namespace {

constexpr base::TimeMillis kMapsReparseInterval{500};
constexpr uint32_t kRetryDelayMs = 100;

constexpr size_t kMaxFrames = 500;

// We assume average ~300us per unwind. If we handle up to 1000 unwinds, this
// makes sure other tasks get to be run at least every 300ms if the unwinding
// saturates this thread.
constexpr size_t kUnwindBatchSize = 1000;
constexpr size_t kRecordBatchSize = 1024;
constexpr size_t kMaxAllocRecordArenaSize = 2 * kRecordBatchSize;

#pragma GCC diagnostic push
// We do not care about deterministic destructor order.
#pragma GCC diagnostic ignored "-Wglobal-constructors"
#pragma GCC diagnostic ignored "-Wexit-time-destructors"
static std::vector<std::string> kSkipMaps{"heapprofd_client.so",
                                          "heapprofd_client_api.so"};
#pragma GCC diagnostic pop

size_t GetRegsSize(unwindstack::Regs* regs) {
  if (regs->Is32Bit())
    return sizeof(uint32_t) * regs->total_regs();
  return sizeof(uint64_t) * regs->total_regs();
}

void ReadFromRawData(unwindstack::Regs* regs, void* raw_data) {
  memcpy(regs->RawData(), raw_data, GetRegsSize(regs));
}

}  // namespace

std::unique_ptr<unwindstack::Regs> CreateRegsFromRawData(
    unwindstack::ArchEnum arch,
    void* raw_data) {
  std::unique_ptr<unwindstack::Regs> ret;
  switch (arch) {
    case unwindstack::ARCH_X86:
      ret.reset(new unwindstack::RegsX86());
      break;
    case unwindstack::ARCH_X86_64:
      ret.reset(new unwindstack::RegsX86_64());
      break;
    case unwindstack::ARCH_ARM:
      ret.reset(new unwindstack::RegsArm());
      break;
    case unwindstack::ARCH_ARM64:
      ret.reset(new unwindstack::RegsArm64());
      break;
    case unwindstack::ARCH_RISCV64:
      ret.reset(new unwindstack::RegsRiscv64());
      break;
    case unwindstack::ARCH_UNKNOWN:
      break;
  }
  if (ret)
    ReadFromRawData(ret.get(), raw_data);
  return ret;
}

bool DoUnwind(WireMessage* msg, UnwindingMetadata* metadata, AllocRecord* out) {
  AllocMetadata* alloc_metadata = msg->alloc_header;
  std::unique_ptr<unwindstack::Regs> regs(CreateRegsFromRawData(
      alloc_metadata->arch, alloc_metadata->register_data));
  if (regs == nullptr) {
    PERFETTO_DLOG("Unable to construct unwindstack::Regs");
    unwindstack::FrameData frame_data{};
    frame_data.function_name = "ERROR READING REGISTERS";

    out->frames.clear();
    out->build_ids.clear();
    out->frames.emplace_back(std::move(frame_data));
    out->build_ids.emplace_back("");
    out->error = true;
    return false;
  }
  uint8_t* stack = reinterpret_cast<uint8_t*>(msg->payload);
  std::shared_ptr<unwindstack::Memory> mems =
      std::make_shared<StackOverlayMemory>(metadata->fd_mem,
                                           alloc_metadata->stack_pointer, stack,
                                           msg->payload_size);

  unwindstack::Unwinder unwinder(kMaxFrames, &metadata->fd_maps, regs.get(),
                                 mems);
#if PERFETTO_BUILDFLAG(PERFETTO_ANDROID_BUILD)
  unwinder.SetJitDebug(metadata->GetJitDebug(regs->Arch()));
  unwinder.SetDexFiles(metadata->GetDexFiles(regs->Arch()));
#endif
  // Suppress incorrect "variable may be uninitialized" error for if condition
  // after this loop. error_code = LastErrorCode gets run at least once.
  unwindstack::ErrorCode error_code = unwindstack::ERROR_NONE;
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (attempt > 0) {
      if (metadata->last_maps_reparse_time + kMapsReparseInterval >
          base::GetWallTimeMs()) {
        PERFETTO_DLOG("Skipping reparse due to rate limit.");
        break;
      }
      PERFETTO_DLOG("Reparsing maps");
      metadata->ReparseMaps();
      metadata->last_maps_reparse_time = base::GetWallTimeMs();
      // Regs got invalidated by libuwindstack's speculative jump.
      // Reset.
      ReadFromRawData(regs.get(), alloc_metadata->register_data);
      out->reparsed_map = true;
#if PERFETTO_BUILDFLAG(PERFETTO_ANDROID_BUILD)
      unwinder.SetJitDebug(metadata->GetJitDebug(regs->Arch()));
      unwinder.SetDexFiles(metadata->GetDexFiles(regs->Arch()));
#endif
    }
    out->frames.swap(unwinder.frames());  // Provide the unwinder buffer to use.
    unwinder.Unwind(&kSkipMaps, /*map_suffixes_to_ignore=*/nullptr);
    out->frames.swap(unwinder.frames());  // Take the buffer back.
    error_code = unwinder.LastErrorCode();
    if (error_code != unwindstack::ERROR_INVALID_MAP &&
        (unwinder.warnings() & unwindstack::WARNING_DEX_PC_NOT_IN_MAP) == 0) {
      break;
    }
  }
  out->build_ids.resize(out->frames.size());
  for (size_t i = 0; i < out->frames.size(); ++i) {
    out->build_ids[i] = metadata->GetBuildId(out->frames[i]);
  }

  if (error_code != unwindstack::ERROR_NONE) {
    PERFETTO_DLOG("Unwinding error %" PRIu8, error_code);
    unwindstack::FrameData frame_data{};
    frame_data.function_name =
        "ERROR " + StringifyLibUnwindstackError(error_code);

    out->frames.emplace_back(std::move(frame_data));
    out->build_ids.emplace_back("");
    out->error = true;
  }
  return true;
}

UnwindingWorker::~UnwindingWorker() {
  if (thread_task_runner_.get() == nullptr) {
    return;
  }
  std::mutex mutex;
  std::condition_variable cv;

  std::unique_lock<std::mutex> lock(mutex);
  bool done = false;
  thread_task_runner_.PostTask([&mutex, &cv, &done, this] {
    for (auto& it : client_data_) {
      auto& client_data = it.second;
      client_data.sock->Shutdown(false);
    }
    client_data_.clear();

    std::lock_guard<std::mutex> inner_lock(mutex);
    done = true;
    cv.notify_one();
  });
  cv.wait(lock, [&done] { return done; });
}

void UnwindingWorker::OnDisconnect(base::UnixSocket* self) {
  pid_t peer_pid = self->peer_pid_linux();
  auto it = client_data_.find(peer_pid);
  if (it == client_data_.end()) {
    PERFETTO_DFATAL_OR_ELOG("Disconnected unexpected socket.");
    return;
  }

  ClientData& client_data = it->second;
  SharedRingBuffer& shmem = client_data.shmem;
  client_data.drain_bytes = shmem.read_avail();

  if (client_data.drain_bytes != 0) {
    DrainJob(peer_pid);
  } else {
    FinishDisconnect(it);
  }
}

void UnwindingWorker::RemoveClientData(
    std::map<pid_t, ClientData>::iterator client_data_iterator) {
  client_data_.erase(client_data_iterator);
  if (client_data_.empty()) {
    // We got rid of the last client. Flush and destruct AllocRecords in
    // arena. Disable the arena (will not accept returning borrowed records)
    // in case there are pending AllocRecords on the main thread.
    alloc_record_arena_.Disable();
  }
}

void UnwindingWorker::FinishDisconnect(
    std::map<pid_t, ClientData>::iterator client_data_iterator) {
  pid_t peer_pid = client_data_iterator->first;
  ClientData& client_data = client_data_iterator->second;
  SharedRingBuffer& shmem = client_data.shmem;

  if (!client_data.free_records.empty()) {
    delegate_->PostFreeRecord(this, std::move(client_data.free_records));
  }

  SharedRingBuffer::Stats stats = {};
  {
    auto lock = shmem.AcquireLock(ScopedSpinlock::Mode::Try);
    if (lock.locked())
      stats = shmem.GetStats(lock);
    else
      PERFETTO_ELOG("Failed to log shmem to get stats.");
  }
  DataSourceInstanceID ds_id = client_data.data_source_instance_id;

  RemoveClientData(client_data_iterator);
  delegate_->PostSocketDisconnected(this, ds_id, peer_pid, stats);
}

void UnwindingWorker::OnDataAvailable(base::UnixSocket* self) {
  // Drain buffer to clear the notification.
  char recv_buf[kUnwindBatchSize];
  self->Receive(recv_buf, sizeof(recv_buf));
  BatchUnwindJob(self->peer_pid_linux());
}

UnwindingWorker::ReadAndUnwindBatchResult UnwindingWorker::ReadAndUnwindBatch(
    ClientData* client_data) {
  SharedRingBuffer& shmem = client_data->shmem;
  SharedRingBuffer::Buffer buf;
  ReadAndUnwindBatchResult res;

  size_t i;
  for (i = 0; i < kUnwindBatchSize; ++i) {
    uint64_t reparses_before = client_data->metadata.reparses;
    buf = shmem.BeginRead();
    if (!buf)
      break;
    HandleBuffer(this, &alloc_record_arena_, buf, client_data,
                 client_data->sock->peer_pid_linux(), delegate_);
    res.bytes_read += shmem.EndRead(std::move(buf));
    // Reparsing takes time, so process the rest in a new batch to avoid timing
    // out.
    if (reparses_before < client_data->metadata.reparses) {
      res.status = ReadAndUnwindBatchResult::Status::kHasMore;
      return res;
    }
  }

  if (i == kUnwindBatchSize) {
    res.status = ReadAndUnwindBatchResult::Status::kHasMore;
  } else if (i > 0) {
    res.status = ReadAndUnwindBatchResult::Status::kReadSome;
  } else {
    res.status = ReadAndUnwindBatchResult::Status::kReadNone;
  }
  return res;
}

void UnwindingWorker::BatchUnwindJob(pid_t peer_pid) {
  auto it = client_data_.find(peer_pid);
  if (it == client_data_.end()) {
    // This can happen if the client disconnected before the buffer was fully
    // handled.
    PERFETTO_DLOG("Unexpected data.");
    return;
  }
  ClientData& client_data = it->second;
  if (client_data.drain_bytes != 0) {
    // This process disconnected and we're reading out the remainder of its
    // buffered data in a dedicated recurring task (DrainJob), so this task has
    // nothing to do.
    return;
  }

  bool job_reposted = false;
  bool reader_paused = false;
  switch (ReadAndUnwindBatch(&client_data).status) {
    case ReadAndUnwindBatchResult::Status::kHasMore:
      thread_task_runner_.get()->PostTask(
          [this, peer_pid] { BatchUnwindJob(peer_pid); });
      job_reposted = true;
      break;
    case ReadAndUnwindBatchResult::Status::kReadSome:
      thread_task_runner_.get()->PostDelayedTask(
          [this, peer_pid] { BatchUnwindJob(peer_pid); }, kRetryDelayMs);
      job_reposted = true;
      break;
    case ReadAndUnwindBatchResult::Status::kReadNone:
      client_data.shmem.SetReaderPaused();
      reader_paused = true;
      break;
  }

  // We need to either repost the job, or set the reader paused bit. By
  // setting that bit, we inform the client that we want to be notified when
  // new data is written to the shared memory buffer.
  // If we do neither of these things, we will not read from the shared memory
  // buffer again.
  PERFETTO_CHECK(job_reposted || reader_paused);
}

void UnwindingWorker::DrainJob(pid_t peer_pid) {
  auto it = client_data_.find(peer_pid);
  if (it == client_data_.end()) {
    return;
  }
  ClientData& client_data = it->second;
  auto res = ReadAndUnwindBatch(&client_data);
  switch (res.status) {
    case ReadAndUnwindBatchResult::Status::kHasMore:
      if (res.bytes_read < client_data.drain_bytes) {
        client_data.drain_bytes -= res.bytes_read;
        thread_task_runner_.get()->PostTask(
            [this, peer_pid] { DrainJob(peer_pid); });
        return;
      }
      // ReadAndUnwindBatch read more than client_data.drain_bytes.
      break;
    case ReadAndUnwindBatchResult::Status::kReadSome:
      // ReadAndUnwindBatch read all the available data (for now) in the shared
      // memory buffer.
    case ReadAndUnwindBatchResult::Status::kReadNone:
      // There was no data in the shared memory buffer.
      break;
  }
  // No further drain task has been scheduled. Drain is finished. Finish the
  // disconnect operation as well.

  FinishDisconnect(it);
}

// static
void UnwindingWorker::HandleBuffer(UnwindingWorker* self,
                                   AllocRecordArena* alloc_record_arena,
                                   const SharedRingBuffer::Buffer& buf,
                                   ClientData* client_data,
                                   pid_t peer_pid,
                                   Delegate* delegate) {
  UnwindingMetadata* unwinding_metadata = &client_data->metadata;
  DataSourceInstanceID data_source_instance_id =
      client_data->data_source_instance_id;
  WireMessage msg;
  // TODO(fmayer): standardise on char* or uint8_t*.
  // char* has stronger guarantees regarding aliasing.
  // see https://timsong-cpp.github.io/cppwp/n3337/basic.lval#10.8
  if (!ReceiveWireMessage(reinterpret_cast<char*>(buf.data), buf.size, &msg)) {
    PERFETTO_DFATAL_OR_ELOG("Failed to receive wire message.");
    return;
  }

  if (msg.record_type == RecordType::Malloc) {
    std::unique_ptr<AllocRecord> rec = alloc_record_arena->BorrowAllocRecord();
    rec->alloc_metadata = *msg.alloc_header;
    rec->pid = peer_pid;
    rec->data_source_instance_id = data_source_instance_id;
    auto start_time_us = base::GetWallTimeNs() / 1000;
    if (!client_data->stream_allocations)
      DoUnwind(&msg, unwinding_metadata, rec.get());
    rec->unwinding_time_us = static_cast<uint64_t>(
        ((base::GetWallTimeNs() / 1000) - start_time_us).count());
    delegate->PostAllocRecord(self, std::move(rec));
  } else if (msg.record_type == RecordType::Free) {
    FreeRecord rec;
    rec.pid = peer_pid;
    rec.data_source_instance_id = data_source_instance_id;
    // We need to copy this, so we can return the memory to the shmem buffer.
    memcpy(&rec.entry, msg.free_header, sizeof(*msg.free_header));
    client_data->free_records.emplace_back(std::move(rec));
    if (client_data->free_records.size() == kRecordBatchSize) {
      delegate->PostFreeRecord(self, std::move(client_data->free_records));
      client_data->free_records.clear();
      client_data->free_records.reserve(kRecordBatchSize);
    }
  } else if (msg.record_type == RecordType::HeapName) {
    HeapNameRecord rec;
    rec.pid = peer_pid;
    rec.data_source_instance_id = data_source_instance_id;
    memcpy(&rec.entry, msg.heap_name_header, sizeof(*msg.heap_name_header));
    rec.entry.heap_name[sizeof(rec.entry.heap_name) - 1] = '\0';
    delegate->PostHeapNameRecord(self, std::move(rec));
  } else {
    PERFETTO_DFATAL_OR_ELOG("Invalid record type.");
  }
}

void UnwindingWorker::PostHandoffSocket(HandoffData handoff_data) {
  // Even with C++14, this cannot be moved, as std::function has to be
  // copyable, which HandoffData is not.
  HandoffData* raw_data = new HandoffData(std::move(handoff_data));
  // We do not need to use a WeakPtr here because the task runner will not
  // outlive its UnwindingWorker.
  thread_task_runner_.get()->PostTask([this, raw_data] {
    HandoffData data = std::move(*raw_data);
    delete raw_data;
    HandleHandoffSocket(std::move(data));
  });
}

void UnwindingWorker::HandleHandoffSocket(HandoffData handoff_data) {
  auto sock = base::UnixSocket::AdoptConnected(
      handoff_data.sock.ReleaseFd(), this, this->thread_task_runner_.get(),
      base::SockFamily::kUnix, base::SockType::kStream);
  pid_t peer_pid = sock->peer_pid_linux();

  UnwindingMetadata metadata(std::move(handoff_data.maps_fd),
                             std::move(handoff_data.mem_fd));
  ClientData client_data{
      handoff_data.data_source_instance_id,
      std::move(sock),
      std::move(metadata),
      std::move(handoff_data.shmem),
      std::move(handoff_data.client_config),
      handoff_data.stream_allocations,
      /*drain_bytes=*/0,
      /*free_records=*/{},
  };
  client_data.free_records.reserve(kRecordBatchSize);
  client_data.shmem.SetReaderPaused();
  client_data_.emplace(peer_pid, std::move(client_data));
  alloc_record_arena_.Enable();
}

void UnwindingWorker::HandleDrainFree(DataSourceInstanceID ds_id, pid_t pid) {
  auto it = client_data_.find(pid);
  if (it != client_data_.end()) {
    ClientData& client_data = it->second;

    if (!client_data.free_records.empty()) {
      delegate_->PostFreeRecord(this, std::move(client_data.free_records));
      client_data.free_records.clear();
      client_data.free_records.reserve(kRecordBatchSize);
    }
  }
  delegate_->PostDrainDone(this, ds_id);
}

void UnwindingWorker::PostDisconnectSocket(pid_t pid) {
  // We do not need to use a WeakPtr here because the task runner will not
  // outlive its UnwindingWorker.
  thread_task_runner_.get()->PostTask(
      [this, pid] { HandleDisconnectSocket(pid); });
}

void UnwindingWorker::PostPurgeProcess(pid_t pid) {
  // We do not need to use a WeakPtr here because the task runner will not
  // outlive its UnwindingWorker.
  thread_task_runner_.get()->PostTask([this, pid] {
    auto it = client_data_.find(pid);
    if (it == client_data_.end()) {
      return;
    }
    RemoveClientData(it);
  });
}

void UnwindingWorker::PostDrainFree(DataSourceInstanceID ds_id, pid_t pid) {
  // We do not need to use a WeakPtr here because the task runner will not
  // outlive its UnwindingWorker.
  thread_task_runner_.get()->PostTask(
      [this, ds_id, pid] { HandleDrainFree(ds_id, pid); });
}

void UnwindingWorker::HandleDisconnectSocket(pid_t pid) {
  auto it = client_data_.find(pid);
  if (it == client_data_.end()) {
    // This is expected if the client voluntarily disconnects before the
    // profiling session ended. In that case, there is a race between the main
    // thread learning about the disconnect and it calling back here.
    return;
  }
  ClientData& client_data = it->second;
  // Shutdown and call OnDisconnect handler.
  client_data.shmem.SetShuttingDown();
  client_data.sock->Shutdown(/* notify= */ true);
}

std::unique_ptr<AllocRecord> AllocRecordArena::BorrowAllocRecord() {
  std::lock_guard<std::mutex> l(*alloc_records_mutex_);
  if (!alloc_records_.empty()) {
    std::unique_ptr<AllocRecord> result = std::move(alloc_records_.back());
    alloc_records_.pop_back();
    return result;
  }
  return std::unique_ptr<AllocRecord>(new AllocRecord());
}

void AllocRecordArena::ReturnAllocRecord(std::unique_ptr<AllocRecord> record) {
  std::lock_guard<std::mutex> l(*alloc_records_mutex_);
  if (enabled_ && record && alloc_records_.size() < kMaxAllocRecordArenaSize)
    alloc_records_.emplace_back(std::move(record));
}

void AllocRecordArena::Disable() {
  std::lock_guard<std::mutex> l(*alloc_records_mutex_);
  alloc_records_.clear();
  enabled_ = false;
}

void AllocRecordArena::Enable() {
  std::lock_guard<std::mutex> l(*alloc_records_mutex_);
  enabled_ = true;
}

UnwindingWorker::Delegate::~Delegate() = default;

}  // namespace profiling
}  // namespace perfetto
