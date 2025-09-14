/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "src/tracing/test/proxy_producer_endpoint.h"

#include "perfetto/ext/tracing/core/trace_writer.h"

namespace perfetto {

ProxyProducerEndpoint::~ProxyProducerEndpoint() = default;

void ProxyProducerEndpoint::Disconnect() {
  if (!backend_) {
    return;
  }
  backend_->Disconnect();
}
void ProxyProducerEndpoint::RegisterDataSource(
    const DataSourceDescriptor& dsd) {
  if (!backend_) {
    return;
  }
  backend_->RegisterDataSource(dsd);
}
void ProxyProducerEndpoint::UpdateDataSource(const DataSourceDescriptor& dsd) {
  if (!backend_) {
    return;
  }
  backend_->UpdateDataSource(dsd);
}
void ProxyProducerEndpoint::UnregisterDataSource(const std::string& name) {
  if (!backend_) {
    return;
  }
  backend_->UnregisterDataSource(name);
}
void ProxyProducerEndpoint::RegisterTraceWriter(uint32_t writer_id,
                                                uint32_t target_buffer) {
  if (!backend_) {
    return;
  }
  backend_->RegisterTraceWriter(writer_id, target_buffer);
}
void ProxyProducerEndpoint::UnregisterTraceWriter(uint32_t writer_id) {
  if (!backend_) {
    return;
  }
  backend_->UnregisterTraceWriter(writer_id);
}
void ProxyProducerEndpoint::CommitData(const CommitDataRequest& req,
                                       CommitDataCallback callback) {
  if (!backend_) {
    return;
  }
  backend_->CommitData(req, callback);
}
SharedMemory* ProxyProducerEndpoint::shared_memory() const {
  if (!backend_) {
    return nullptr;
  }
  return backend_->shared_memory();
}
size_t ProxyProducerEndpoint::shared_buffer_page_size_kb() const {
  if (!backend_) {
    return 0;
  }
  return backend_->shared_buffer_page_size_kb();
}
std::unique_ptr<TraceWriter> ProxyProducerEndpoint::CreateTraceWriter(
    BufferID target_buffer,
    BufferExhaustedPolicy buffer_exhausted_policy) {
  if (!backend_) {
    return nullptr;
  }
  return backend_->CreateTraceWriter(target_buffer, buffer_exhausted_policy);
}
SharedMemoryArbiter* ProxyProducerEndpoint::MaybeSharedMemoryArbiter() {
  if (!backend_) {
    return nullptr;
  }
  return backend_->MaybeSharedMemoryArbiter();
}
bool ProxyProducerEndpoint::IsShmemProvidedByProducer() const {
  if (!backend_) {
    return false;
  }
  return backend_->IsShmemProvidedByProducer();
}
void ProxyProducerEndpoint::NotifyFlushComplete(FlushRequestID id) {
  if (!backend_) {
    return;
  }
  backend_->NotifyFlushComplete(id);
}
void ProxyProducerEndpoint::NotifyDataSourceStarted(DataSourceInstanceID id) {
  if (!backend_) {
    return;
  }
  backend_->NotifyDataSourceStarted(id);
}
void ProxyProducerEndpoint::NotifyDataSourceStopped(DataSourceInstanceID id) {
  if (!backend_) {
    return;
  }
  backend_->NotifyDataSourceStopped(id);
}
void ProxyProducerEndpoint::ActivateTriggers(
    const std::vector<std::string>& triggers) {
  if (!backend_) {
    return;
  }
  backend_->ActivateTriggers(triggers);
}
void ProxyProducerEndpoint::Sync(std::function<void()> callback) {
  if (!backend_) {
    return;
  }
  backend_->Sync(callback);
}

}  // namespace perfetto
