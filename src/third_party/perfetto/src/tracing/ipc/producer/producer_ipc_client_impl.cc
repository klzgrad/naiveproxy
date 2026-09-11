/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "src/tracing/ipc/producer/producer_ipc_client_impl.h"

#include <cinttypes>

#include <string.h>

#include "perfetto/base/logging.h"
#include "perfetto/base/task_runner.h"
#include "perfetto/ext/base/unix_socket.h"
#include "perfetto/ext/base/version.h"
#include "perfetto/ext/ipc/client.h"
#include "perfetto/ext/tracing/core/commit_data_request.h"
#include "perfetto/ext/tracing/core/producer.h"
#include "perfetto/ext/tracing/core/shared_memory_abi.h"
#include "perfetto/ext/tracing/core/shared_memory_arbiter.h"
#include "perfetto/ext/tracing/core/trace_writer.h"
#include "perfetto/tracing/core/data_source_config.h"
#include "perfetto/tracing/core/data_source_descriptor.h"
#include "perfetto/tracing/core/trace_config.h"
#include "src/tracing/core/in_process_shared_memory.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include "src/tracing/ipc/shared_memory_windows.h"
#else
#include "src/tracing/ipc/posix_shared_memory.h"
#endif

// TODO(fmayer): think to what happens when ProducerIPCClientImpl gets destroyed
// w.r.t. the Producer pointer. Also think to lifetime of the Producer* during
// the callbacks.

namespace perfetto {

// static. (Declared in include/tracing/ipc/producer_ipc_client.h).
std::unique_ptr<TracingService::ProducerEndpoint> ProducerIPCClient::Connect(
    const char* service_sock_name,
    Producer* producer,
    const std::string& producer_name,
    base::TaskRunner* task_runner,
    TracingService::ProducerSMBScrapingMode smb_scraping_mode,
    size_t shared_memory_size_hint_bytes,
    size_t shared_memory_page_size_hint_bytes,
    std::unique_ptr<SharedMemory> shm,
    std::unique_ptr<SharedMemoryArbiter> shm_arbiter,
    ConnectionFlags conn_flags) {
  return std::unique_ptr<TracingService::ProducerEndpoint>(
      new ProducerIPCClientImpl(
          {service_sock_name,
           conn_flags ==
               ProducerIPCClient::ConnectionFlags::kRetryIfUnreachable},
          producer, producer_name, task_runner, smb_scraping_mode,
          shared_memory_size_hint_bytes, shared_memory_page_size_hint_bytes,
          std::move(shm), std::move(shm_arbiter), nullptr));
}

// static. (Declared in include/tracing/ipc/producer_ipc_client.h).
std::unique_ptr<TracingService::ProducerEndpoint> ProducerIPCClient::Connect(
    ipc::Client::ConnArgs conn_args,
    Producer* producer,
    const std::string& producer_name,
    base::TaskRunner* task_runner,
    TracingService::ProducerSMBScrapingMode smb_scraping_mode,
    size_t shared_memory_size_hint_bytes,
    size_t shared_memory_page_size_hint_bytes,
    std::unique_ptr<SharedMemory> shm,
    std::unique_ptr<SharedMemoryArbiter> shm_arbiter,
    CreateSocketAsync create_socket_async) {
  return std::unique_ptr<TracingService::ProducerEndpoint>(
      new ProducerIPCClientImpl(
          std::move(conn_args), producer, producer_name, task_runner,
          smb_scraping_mode, shared_memory_size_hint_bytes,
          shared_memory_page_size_hint_bytes, std::move(shm),
          std::move(shm_arbiter), create_socket_async));
}

ProducerIPCClientImpl::ProducerIPCClientImpl(
    ipc::Client::ConnArgs conn_args,
    Producer* producer,
    const std::string& producer_name,
    base::TaskRunner* task_runner,
    TracingService::ProducerSMBScrapingMode smb_scraping_mode,
    size_t shared_memory_size_hint_bytes,
    size_t shared_memory_page_size_hint_bytes,
    std::unique_ptr<SharedMemory> shm,
    std::unique_ptr<SharedMemoryArbiter> shm_arbiter,
    CreateSocketAsync create_socket_async)
    : producer_(producer),
      task_runner_(task_runner),
      receive_shmem_fd_cb_fuchsia_(
          std::move(conn_args.receive_shmem_fd_cb_fuchsia)),
      producer_port_(
          new protos::gen::ProducerPortProxy(this /* event_listener */)),
      shared_memory_(std::move(shm)),
      shared_memory_arbiter_(std::move(shm_arbiter)),
      name_(producer_name),
      shared_memory_page_size_hint_bytes_(shared_memory_page_size_hint_bytes),
      shared_memory_size_hint_bytes_(shared_memory_size_hint_bytes),
      smb_scraping_mode_(smb_scraping_mode) {
  // Check for producer-provided SMB (used by Chrome for startup tracing).
  if (shared_memory_) {
    // We also expect a valid (unbound) arbiter. Bind it to this endpoint now.
    PERFETTO_CHECK(shared_memory_arbiter_);
    shared_memory_arbiter_->BindToProducerEndpoint(this, task_runner_);

    // If the service accepts our SMB, then it must match our requested page
    // layout. The protocol doesn't allow the service to change the size and
    // layout when the SMB is provided by the producer.
    shared_buffer_page_size_kb_ = shared_memory_page_size_hint_bytes_ / 1024;
  }

  if (create_socket_async) {
    PERFETTO_DCHECK(conn_args.socket_name);
    auto weak_this = weak_factory_.GetWeakPtr();
    create_socket_async(
        [weak_this, task_runner = task_runner_](base::SocketHandle fd) {
          task_runner->PostTask([weak_this, fd] {
            base::ScopedSocketHandle handle(fd);
            if (!weak_this) {
              return;
            }
            ipc::Client::ConnArgs args(std::move(handle));
            weak_this->ipc_channel_ = ipc::Client::CreateInstance(
                std::move(args), weak_this->task_runner_);
            weak_this->ipc_channel_->BindService(
                weak_this->producer_port_->GetWeakPtr());
          });
        });
  } else {
    ipc_channel_ =
        ipc::Client::CreateInstance(std::move(conn_args), task_runner);
    ipc_channel_->BindService(producer_port_->GetWeakPtr());
  }
  PERFETTO_DCHECK_THREAD(thread_checker_);
}

ProducerIPCClientImpl::~ProducerIPCClientImpl() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
}

void ProducerIPCClientImpl::Disconnect() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!producer_port_)
    return;
  // Reset the producer port so that no further IPCs are received and IPC
  // callbacks are no longer executed. Also reset the IPC channel so that the
  // service is notified of the disconnection.
  producer_port_.reset();
  ipc_channel_.reset();
  // Perform disconnect synchronously.
  OnDisconnect();
}

// Called by the IPC layer if the BindService() succeeds.
void ProducerIPCClientImpl::OnConnect() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  connected_ = true;

  // The IPC layer guarantees that any outstanding callback will be dropped on
  // the floor if producer_port_ is destroyed between the request and the reply.
  // Binding |this| is hence safe.
  ipc::Deferred<protos::gen::InitializeConnectionResponse> on_init;
  on_init.Bind(
      [this](ipc::AsyncResult<protos::gen::InitializeConnectionResponse> resp) {
        OnConnectionInitialized(
            resp.success(),
            resp.success() ? resp->using_shmem_provided_by_producer() : false,
            resp.success() ? resp->direct_smb_patching_supported() : false,
            resp.success() ? resp->use_shmem_emulation() : false);
      });
  protos::gen::InitializeConnectionRequest req;
  req.set_producer_name(name_);
  req.set_shared_memory_size_hint_bytes(
      static_cast<uint32_t>(shared_memory_size_hint_bytes_));
  req.set_shared_memory_page_size_hint_bytes(
      static_cast<uint32_t>(shared_memory_page_size_hint_bytes_));
  switch (smb_scraping_mode_) {
    case TracingService::ProducerSMBScrapingMode::kDefault:
      // No need to set the mode, it defaults to use the service default if
      // unspecified.
      break;
    case TracingService::ProducerSMBScrapingMode::kEnabled:
      req.set_smb_scraping_mode(
          protos::gen::InitializeConnectionRequest::SMB_SCRAPING_ENABLED);
      break;
    case TracingService::ProducerSMBScrapingMode::kDisabled:
      req.set_smb_scraping_mode(
          protos::gen::InitializeConnectionRequest::SMB_SCRAPING_DISABLED);
      break;
  }

  int shm_fd = -1;
  if (shared_memory_) {
    req.set_producer_provided_shmem(true);
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
    auto key = static_cast<SharedMemoryWindows*>(shared_memory_.get())->key();
    req.set_shm_key_windows(key);
#else
    shm_fd = static_cast<PosixSharedMemory*>(shared_memory_.get())->fd();
#endif
  }

  req.set_sdk_version(base::GetVersionString());
  producer_port_->InitializeConnection(req, std::move(on_init), shm_fd);

  // Create the back channel to receive commands from the Service.
  ipc::Deferred<protos::gen::GetAsyncCommandResponse> on_cmd;
  on_cmd.Bind(
      [this](ipc::AsyncResult<protos::gen::GetAsyncCommandResponse> resp) {
        if (!resp)
          return;  // The IPC channel was closed and |resp| was auto-rejected.
        OnServiceRequest(*resp);
      });
  producer_port_->GetAsyncCommand(protos::gen::GetAsyncCommandRequest(),
                                  std::move(on_cmd));

  // If there are pending Sync() requests, send them now.
  for (auto& pending_sync : pending_sync_reqs_)
    Sync(std::move(pending_sync));
  pending_sync_reqs_.clear();
}

void ProducerIPCClientImpl::OnDisconnect() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  PERFETTO_DLOG("Tracing service connection failure");
  connected_ = false;
  data_sources_setup_.clear();
  producer_->OnDisconnect();  // Note: may delete |this|.
}

void ProducerIPCClientImpl::ScheduleDisconnect() {
  // |ipc_channel| doesn't allow disconnection in the middle of handling
  // an IPC call, so the connection drop must take place over two phases.

  // First, synchronously drop the |producer_port_| so that no more IPC
  // messages are handled.
  producer_port_.reset();

  // Then schedule an async task for performing the remainder of the
  // disconnection operations outside the context of the IPC method handler.
  auto weak_this = weak_factory_.GetWeakPtr();
  task_runner_->PostTask([weak_this]() {
    if (weak_this) {
      weak_this->Disconnect();
    }
  });
}

void ProducerIPCClientImpl::OnConnectionInitialized(
    bool connection_succeeded,
    bool using_shmem_provided_by_producer,
    bool direct_smb_patching_supported,
    bool use_shmem_emulation) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  // If connection_succeeded == false, the OnDisconnect() call will follow next
  // and there we'll notify the |producer_|. TODO: add a test for this.
  if (!connection_succeeded)
    return;
  is_shmem_provided_by_producer_ = using_shmem_provided_by_producer;
  direct_smb_patching_supported_ = direct_smb_patching_supported;
  // The tracing service may reject using shared memory and tell the client to
  // commit data over the socket. This can happen when the client connects to
  // the service via a relay service:
  // client <-Unix socket-> relay service <- vsock -> tracing service.
  use_shmem_emulation_ = use_shmem_emulation;
  producer_->OnConnect();

  // Bail out if the service failed to adopt our producer-allocated SMB.
  // TODO(eseckler): Handle adoption failure more gracefully.
  if (shared_memory_ && !is_shmem_provided_by_producer_) {
    PERFETTO_DLOG("Service failed adopt producer-provided SMB, disconnecting.");
    Disconnect();
    return;
  }
}

void ProducerIPCClientImpl::OnServiceRequest(
    const protos::gen::GetAsyncCommandResponse& cmd) {
  PERFETTO_DCHECK_THREAD(thread_checker_);

  // This message is sent only when connecting to a service running Android Q+.
  // See comment below in kStartDataSource.
  if (cmd.has_setup_data_source()) {
    const auto& req = cmd.setup_data_source();
    const DataSourceInstanceID dsid = req.new_instance_id();
    data_sources_setup_.insert(dsid);
    producer_->SetupDataSource(dsid, req.config());
    return;
  }

  if (cmd.has_start_data_source()) {
    const auto& req = cmd.start_data_source();
    const DataSourceInstanceID dsid = req.new_instance_id();
    const DataSourceConfig& cfg = req.config();
    if (!data_sources_setup_.count(dsid)) {
      // When connecting with an older (Android P) service, the service will not
      // send a SetupDataSource message. We synthesize it here in that case.
      producer_->SetupDataSource(dsid, cfg);
    }
    producer_->StartDataSource(dsid, cfg);
    return;
  }

  if (cmd.has_stop_data_source()) {
    const DataSourceInstanceID dsid = cmd.stop_data_source().instance_id();
    producer_->StopDataSource(dsid);
    data_sources_setup_.erase(dsid);
    return;
  }

  if (cmd.has_setup_tracing()) {
    std::unique_ptr<SharedMemory> ipc_shared_memory;
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
    const std::string& shm_key = cmd.setup_tracing().shm_key_windows();
    if (!shm_key.empty())
      ipc_shared_memory = SharedMemoryWindows::Attach(shm_key);
#elif PERFETTO_BUILDFLAG(PERFETTO_OS_FUCHSIA)
    // On Fuchsia, the embedder is responsible for routing the shared memory
    // FD, which is provided to this code via a blocking callback.
    PERFETTO_CHECK(receive_shmem_fd_cb_fuchsia_);

    base::ScopedFile shmem_fd(receive_shmem_fd_cb_fuchsia_());
    if (!shmem_fd) {
      // Failure to get a shared memory buffer is a protocol violation and
      // therefore we should drop the Protocol connection.
      PERFETTO_ELOG("Could not get shared memory FD from embedder.");
      ScheduleDisconnect();
      return;
    }

    ipc_shared_memory =
        PosixSharedMemory::AttachToFd(std::move(shmem_fd),
                                      /*require_seals_if_supported=*/false);
#else
    base::ScopedFile shmem_fd = ipc_channel_->TakeReceivedFD();
    if (shmem_fd) {
      // TODO(primiano): handle mmap failure in case of OOM.
      ipc_shared_memory =
          PosixSharedMemory::AttachToFd(std::move(shmem_fd),
                                        /*require_seals_if_supported=*/false);
    }
#endif
    if (use_shmem_emulation_) {
      PERFETTO_CHECK(!ipc_shared_memory);
      // Need to create an emulated shmem buffer when the transport doesn't
      // support it.
      ipc_shared_memory = InProcessSharedMemory::Create(
          /*size=*/InProcessSharedMemory::kShmemEmulationSize);
    }
    if (ipc_shared_memory) {
      auto shmem_mode = use_shmem_emulation_
                            ? SharedMemoryABI::ShmemMode::kShmemEmulation
                            : SharedMemoryABI::ShmemMode::kDefault;
      // This is the nominal case used in most configurations, where the service
      // provides the SMB.
      PERFETTO_CHECK(!is_shmem_provided_by_producer_ && !shared_memory_);
      shared_memory_ = std::move(ipc_shared_memory);
      shared_buffer_page_size_kb_ =
          cmd.setup_tracing().shared_buffer_page_size_kb();
      shared_memory_arbiter_ = SharedMemoryArbiter::CreateInstance(
          shared_memory_.get(), shared_buffer_page_size_kb_ * 1024, shmem_mode,
          this, task_runner_);
      if (direct_smb_patching_supported_)
        shared_memory_arbiter_->SetDirectSMBPatchingSupportedByService();
    } else {
      // Producer-provided SMB (used by Chrome for startup tracing).
      PERFETTO_CHECK(is_shmem_provided_by_producer_ && shared_memory_ &&
                     shared_memory_arbiter_);
    }
    producer_->OnTracingSetup();
    return;
  }

  if (cmd.has_flush()) {
    // This cast boilerplate is required only because protobuf uses its own
    // uint64 and not stdint's uint64_t. On some 64 bit archs they differ on the
    // type (long vs long long) even though they have the same size.
    const auto* data_source_ids = cmd.flush().data_source_ids().data();
    static_assert(sizeof(data_source_ids[0]) == sizeof(DataSourceInstanceID),
                  "data_source_ids should be 64-bit");

    FlushFlags flags(cmd.flush().flags());
    producer_->Flush(
        cmd.flush().request_id(),
        reinterpret_cast<const DataSourceInstanceID*>(data_source_ids),
        static_cast<size_t>(cmd.flush().data_source_ids().size()), flags);
    return;
  }

  if (cmd.has_clear_incremental_state()) {
    const auto* data_source_ids =
        cmd.clear_incremental_state().data_source_ids().data();
    static_assert(sizeof(data_source_ids[0]) == sizeof(DataSourceInstanceID),
                  "data_source_ids should be 64-bit");
    producer_->ClearIncrementalState(
        reinterpret_cast<const DataSourceInstanceID*>(data_source_ids),
        static_cast<size_t>(
            cmd.clear_incremental_state().data_source_ids().size()));
    return;
  }

  PERFETTO_DFATAL("Unknown async request received from tracing service");
}

void ProducerIPCClientImpl::RegisterDataSource(
    const DataSourceDescriptor& descriptor) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!connected_) {
    PERFETTO_DLOG(
        "Cannot RegisterDataSource(), not connected to tracing service");
  }
  protos::gen::RegisterDataSourceRequest req;
  *req.mutable_data_source_descriptor() = descriptor;
  ipc::Deferred<protos::gen::RegisterDataSourceResponse> async_response;
  async_response.Bind(
      [](ipc::AsyncResult<protos::gen::RegisterDataSourceResponse> response) {
        if (!response)
          PERFETTO_DLOG("RegisterDataSource() failed: connection reset");
      });
  producer_port_->RegisterDataSource(req, std::move(async_response));
}

void ProducerIPCClientImpl::UpdateDataSource(
    const DataSourceDescriptor& descriptor) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!connected_) {
    PERFETTO_DLOG(
        "Cannot UpdateDataSource(), not connected to tracing service");
  }
  protos::gen::UpdateDataSourceRequest req;
  *req.mutable_data_source_descriptor() = descriptor;
  ipc::Deferred<protos::gen::UpdateDataSourceResponse> async_response;
  async_response.Bind(
      [](ipc::AsyncResult<protos::gen::UpdateDataSourceResponse> response) {
        if (!response)
          PERFETTO_DLOG("UpdateDataSource() failed: connection reset");
      });
  producer_port_->UpdateDataSource(req, std::move(async_response));
}

void ProducerIPCClientImpl::UnregisterDataSource(const std::string& name) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!connected_) {
    PERFETTO_DLOG(
        "Cannot UnregisterDataSource(), not connected to tracing service");
    return;
  }
  protos::gen::UnregisterDataSourceRequest req;
  req.set_data_source_name(name);
  producer_port_->UnregisterDataSource(
      req, ipc::Deferred<protos::gen::UnregisterDataSourceResponse>());
}

void ProducerIPCClientImpl::RegisterTraceWriter(uint32_t writer_id,
                                                uint32_t target_buffer) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!connected_) {
    PERFETTO_DLOG(
        "Cannot RegisterTraceWriter(), not connected to tracing service");
    return;
  }
  if (use_shmem_emulation_ &&
      smb_scraping_mode_ == TracingService::ProducerSMBScrapingMode::kEnabled) {
    writers_for_scraping_[static_cast<WriterID>(writer_id)] =
        static_cast<BufferID>(target_buffer);
  }
  protos::gen::RegisterTraceWriterRequest req;
  req.set_trace_writer_id(writer_id);
  req.set_target_buffer(target_buffer);
  producer_port_->RegisterTraceWriter(
      req, ipc::Deferred<protos::gen::RegisterTraceWriterResponse>());
}

void ProducerIPCClientImpl::UnregisterTraceWriter(uint32_t writer_id) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!connected_) {
    PERFETTO_DLOG(
        "Cannot UnregisterTraceWriter(), not connected to tracing service");
    return;
  }
  if (use_shmem_emulation_ &&
      smb_scraping_mode_ == TracingService::ProducerSMBScrapingMode::kEnabled) {
    writers_for_scraping_.erase(static_cast<WriterID>(writer_id));
  }
  protos::gen::UnregisterTraceWriterRequest req;
  req.set_trace_writer_id(writer_id);
  producer_port_->UnregisterTraceWriter(
      req, ipc::Deferred<protos::gen::UnregisterTraceWriterResponse>());
}

void ProducerIPCClientImpl::CommitData(const CommitDataRequest& req,
                                       CommitDataCallback callback) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!connected_) {
    PERFETTO_DLOG("Cannot CommitData(), not connected to tracing service");
    return;
  }
  ipc::Deferred<protos::gen::CommitDataResponse> async_response;
  // TODO(primiano): add a test that destroys ProducerIPCClientImpl soon after
  // this call and checks that the callback is dropped.
  if (callback) {
    async_response.Bind(
        [callback](ipc::AsyncResult<protos::gen::CommitDataResponse> response) {
          if (!response) {
            PERFETTO_DLOG("CommitData() failed: connection reset");
            return;
          }
          callback();
        });
  }
  producer_port_->CommitData(req, std::move(async_response));
}

void ProducerIPCClientImpl::NotifyDataSourceStarted(DataSourceInstanceID id) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!connected_) {
    PERFETTO_DLOG(
        "Cannot NotifyDataSourceStarted(), not connected to tracing service");
    return;
  }
  protos::gen::NotifyDataSourceStartedRequest req;
  req.set_data_source_id(id);
  producer_port_->NotifyDataSourceStarted(
      req, ipc::Deferred<protos::gen::NotifyDataSourceStartedResponse>());
}

void ProducerIPCClientImpl::NotifyDataSourceStopped(DataSourceInstanceID id) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!connected_) {
    PERFETTO_DLOG(
        "Cannot NotifyDataSourceStopped(), not connected to tracing service");
    return;
  }
  protos::gen::NotifyDataSourceStoppedRequest req;
  req.set_data_source_id(id);
  producer_port_->NotifyDataSourceStopped(
      req, ipc::Deferred<protos::gen::NotifyDataSourceStoppedResponse>());
}

void ProducerIPCClientImpl::ActivateTriggers(
    const std::vector<std::string>& triggers) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!connected_) {
    PERFETTO_DLOG(
        "Cannot ActivateTriggers(), not connected to tracing service");
    return;
  }
  protos::gen::ActivateTriggersRequest proto_req;
  for (const auto& name : triggers) {
    *proto_req.add_trigger_names() = name;
  }
  producer_port_->ActivateTriggers(
      proto_req, ipc::Deferred<protos::gen::ActivateTriggersResponse>());
}

void ProducerIPCClientImpl::Sync(std::function<void()> callback) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!connected_) {
    pending_sync_reqs_.emplace_back(std::move(callback));
    return;
  }
  ipc::Deferred<protos::gen::SyncResponse> resp;
  resp.Bind([callback](ipc::AsyncResult<protos::gen::SyncResponse>) {
    // Here we ACK the callback even if the service replies with a failure
    // (i.e. the service is too old and doesn't understand Sync()). In that
    // case the service has still seen the request, the IPC roundtrip is
    // still a (weaker) linearization fence.
    callback();
  });
  producer_port_->Sync(protos::gen::SyncRequest(), std::move(resp));
}

std::unique_ptr<TraceWriter> ProducerIPCClientImpl::CreateTraceWriter(
    BufferID target_buffer,
    BufferExhaustedPolicy buffer_exhausted_policy) {
  // This method can be called by different threads. |shared_memory_arbiter_| is
  // thread-safe but be aware of accessing any other state in this function.
  return shared_memory_arbiter_->CreateTraceWriter(target_buffer,
                                                   buffer_exhausted_policy);
}

SharedMemoryArbiter* ProducerIPCClientImpl::MaybeSharedMemoryArbiter() {
  return shared_memory_arbiter_.get();
}

bool ProducerIPCClientImpl::IsShmemProvidedByProducer() const {
  return is_shmem_provided_by_producer_;
}

void ProducerIPCClientImpl::NotifyFlushComplete(FlushRequestID req_id) {
  shared_memory_arbiter_->NotifyFlushComplete(req_id);

  // NB: For producers using SMB emulation, the actual value of
  // ProducerSMBScrapingMode::kDefault in the service-side is unknown on the
  // producer-side. Therefore we only do producer-side SMB scraping if the
  // scraping mode is explicitly enabled.
  if (use_shmem_emulation_ &&
      smb_scraping_mode_ == TracingService::ProducerSMBScrapingMode::kEnabled) {
    // Producer-side SMB scraping should occur after the flush is complete.
    // Most often the NotifyFlushComplete method is called at the end of a
    // producer's Flush method which also very commonly posts a flush pending
    // commit data request to the producer IPC client's task runner. Posting
    // the SMB scraping task into the task runner should push the SMB scraping
    // after the pending flush data request.
    shared_memory_arbiter_->ScrapeEmulatedSharedMemoryBuffer(
        writers_for_scraping_);
  }
}

SharedMemory* ProducerIPCClientImpl::shared_memory() const {
  return shared_memory_.get();
}

size_t ProducerIPCClientImpl::shared_buffer_page_size_kb() const {
  return shared_buffer_page_size_kb_;
}

}  // namespace perfetto
