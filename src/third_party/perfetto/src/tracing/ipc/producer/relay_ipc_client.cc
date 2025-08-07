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

#include "src/tracing/ipc/producer/relay_ipc_client.h"

#include "perfetto/base/task_runner.h"
#include "perfetto/tracing/core/forward_decls.h"
#include "protos/perfetto/ipc/relay_port.ipc.h"

namespace perfetto {

RelayIPCClient::EventListener::~EventListener() = default;

RelayIPCClient::RelayIPCClient(ipc::Client::ConnArgs conn_args,
                               base::WeakPtr<EventListener> listener,
                               base::TaskRunner* task_runner)
    : listener_(std::move(listener)),
      task_runner_(task_runner),
      ipc_channel_(
          ipc::Client::CreateInstance(std::move(conn_args), task_runner)),
      relay_proxy_(new protos::gen::RelayPortProxy(this /* event_listener */)) {
  ipc_channel_->BindService(relay_proxy_->GetWeakPtr());
  PERFETTO_DCHECK_THREAD(thread_checker_);
}

RelayIPCClient::~RelayIPCClient() = default;

void RelayIPCClient::OnConnect() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  connected_ = true;

  if (listener_)
    listener_->OnServiceConnected();
}

void RelayIPCClient::OnDisconnect() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  connected_ = false;

  if (listener_)
    listener_->OnServiceDisconnected();
}

void RelayIPCClient::InitRelay(const InitRelayRequest& init_relay_request) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  PERFETTO_CHECK(connected_);

  ipc::Deferred<protos::gen::InitRelayResponse> async_resp;
  async_resp.Bind(
      [listener = listener_](ipc::AsyncResult<InitRelayResponse> resp) {
        if (!listener)
          return;
        if (!resp)
          return listener->OnServiceDisconnected();
        // We do nothing once response is received.
      });
  relay_proxy_->InitRelay(init_relay_request, std::move(async_resp), -1);
}

void RelayIPCClient::SyncClock(const SyncClockRequest& sync_clock_request) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!connected_) {
    return task_runner_->PostTask([listener = listener_]() {
      if (listener)
        listener->OnServiceDisconnected();
    });
  }

  SyncClockResponse resp;
  ipc::Deferred<protos::gen::SyncClockResponse> async_resp;
  async_resp.Bind(
      [listener = listener_](ipc::AsyncResult<SyncClockResponse> resp) {
        if (!listener)
          return;
        if (!resp)
          return listener->OnServiceDisconnected();
        listener->OnSyncClockResponse(*resp);
      });
  relay_proxy_->SyncClock(sync_clock_request, std::move(async_resp), -1);
}

}  // namespace perfetto
