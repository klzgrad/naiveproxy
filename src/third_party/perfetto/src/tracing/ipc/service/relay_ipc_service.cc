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

#include "src/tracing/ipc/service/relay_ipc_service.h"

#include <cinttypes>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/clock_snapshots.h"
#include "perfetto/ext/ipc/service.h"
#include "perfetto/ext/tracing/core/tracing_service.h"
#include "perfetto/tracing/core/forward_decls.h"

namespace perfetto {

RelayIPCService::RelayIPCService(TracingService* core_service)
    : core_service_(core_service), weak_ptr_factory_(this) {}

TracingService::RelayEndpoint* RelayIPCService::GetRelayEndpoint(
    ipc::ClientID client_id) {
  auto* endpoint = relay_endpoints_.Find(client_id);
  if (!endpoint)
    return nullptr;
  return endpoint->get();
}

void RelayIPCService::OnClientDisconnected() {
  auto client_id = ipc::Service::client_info().client_id();
  PERFETTO_DLOG("Relay endpoint %" PRIu64 " disconnected ", client_id);

  auto* endpoint = GetRelayEndpoint(client_id);
  if (!endpoint)
    return;

  endpoint->Disconnect();
  relay_endpoints_.Erase(client_id);
}

void RelayIPCService::InitRelay(const protos::gen::InitRelayRequest& req,
                                DeferredInitRelayResponse resp) {
  // Send the response to client to reduce RTT.
  auto async_resp = ipc::AsyncResult<protos::gen::InitRelayResponse>::Create();
  resp.Resolve(std::move(async_resp));

  // Handle the request in the core service.
  auto machine_id = ipc::Service::client_info().machine_id();
  auto client_id = ipc::Service::client_info().client_id();
  auto* endpoint = GetRelayEndpoint(client_id);
  if (!endpoint) {
    auto ep = core_service_->ConnectRelayClient(
        std::make_pair(machine_id, client_id));
    endpoint = ep.get();
    relay_endpoints_.Insert(client_id, std::move(ep));
  }

  endpoint->CacheSystemInfo(req.system_info().SerializeAsArray());
}

void RelayIPCService::SyncClock(const protos::gen::SyncClockRequest& req,
                                DeferredSyncClockResponse resp) {
  auto host_clock_snapshots = base::CaptureClockSnapshots();

  // Send the response to client to reduce RTT.
  auto async_resp = ipc::AsyncResult<protos::gen::SyncClockResponse>::Create();
  resp.Resolve(std::move(async_resp));

  base::ClockSnapshotVector client_clock_snapshots;
  for (size_t i = 0; i < req.clocks().size(); i++) {
    auto& client_clock = req.clocks()[i];
    client_clock_snapshots.emplace_back(client_clock.clock_id(),
                                        client_clock.timestamp());
  }

  // Handle the request in the core service.
  auto machine_id = ipc::Service::client_info().machine_id();
  auto client_id = ipc::Service::client_info().client_id();
  auto* endpoint = GetRelayEndpoint(client_id);
  if (!endpoint) {
    auto ep = core_service_->ConnectRelayClient(
        std::make_pair(machine_id, client_id));
    endpoint = ep.get();
    relay_endpoints_.Insert(client_id, std::move(ep));
  }

  RelayEndpoint::SyncMode mode = req.phase() == SyncClockRequest::PING
                                     ? RelayEndpoint::SyncMode::PING
                                     : RelayEndpoint::SyncMode::UPDATE;
  endpoint->SyncClocks(mode, std::move(client_clock_snapshots),
                       std::move(host_clock_snapshots));
}
}  // namespace perfetto
