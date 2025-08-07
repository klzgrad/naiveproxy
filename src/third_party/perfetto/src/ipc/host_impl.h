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

#ifndef SRC_IPC_HOST_IMPL_H_
#define SRC_IPC_HOST_IMPL_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include "perfetto/base/task_runner.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/sys_types.h"
#include "perfetto/ext/base/thread_checker.h"
#include "perfetto/ext/base/unix_socket.h"
#include "perfetto/ext/ipc/deferred.h"
#include "perfetto/ext/ipc/host.h"
#include "src/ipc/buffered_frame_deserializer.h"

namespace perfetto {
namespace ipc {

constexpr uint32_t kDefaultIpcTxTimeoutMs = 10000;

class HostImpl : public Host, public base::UnixSocket::EventListener {
 public:
  HostImpl(const char* socket_name, base::TaskRunner*);
  HostImpl(base::ScopedSocketHandle, base::TaskRunner*);
  HostImpl(base::TaskRunner* task_runner);
  ~HostImpl() override;

  // Host implementation.
  bool ExposeService(std::unique_ptr<Service>) override;
  void AdoptConnectedSocket_Fuchsia(
      base::ScopedSocketHandle,
      std::function<bool(int)> send_fd_cb) override;
  void SetSocketSendTimeoutMs(uint32_t timeout_ms) override;

  // base::UnixSocket::EventListener implementation.
  void OnNewIncomingConnection(base::UnixSocket*,
                               std::unique_ptr<base::UnixSocket>) override;
  void OnDisconnect(base::UnixSocket*) override;
  void OnDataAvailable(base::UnixSocket*) override;

  const base::UnixSocket* sock() const { return sock_.get(); }

 private:
  // Owns the per-client receive buffer (BufferedFrameDeserializer).
  struct ClientConnection {
    ~ClientConnection();
    ClientID id;
    std::unique_ptr<base::UnixSocket> sock;
    BufferedFrameDeserializer frame_deserializer;
    base::ScopedFile received_fd;
    std::function<bool(int)> send_fd_cb_fuchsia;
    // Peer identity set using IPCFrame sent by the client. These 3 fields
    // should be used only for non-AF_UNIX connections AF_UNIX connections
    // should only rely on the peer identity obtained from the socket.
    uid_t uid_override = base::kInvalidUid;
    pid_t pid_override = base::kInvalidPid;

    // |machine_id| is mapped from machine_id_hint (or socket hostname if
    // |the client doesn't support machine_id_hint).
    base::MachineID machine_id = base::kDefaultMachineID;

    pid_t GetLinuxPeerPid() const;
    uid_t GetPosixPeerUid() const;
    base::MachineID GetMachineID() const { return machine_id; }
  };
  struct ExposedService {
    ExposedService(ServiceID, const std::string&, std::unique_ptr<Service>);
    ~ExposedService();
    ExposedService(ExposedService&&) noexcept;
    ExposedService& operator=(ExposedService&&);

    ServiceID id;
    std::string name;
    std::unique_ptr<Service> instance;
  };

  HostImpl(const HostImpl&) = delete;
  HostImpl& operator=(const HostImpl&) = delete;

  bool Initialize(const char* socket_name);
  void OnReceivedFrame(ClientConnection*, const Frame&);
  void OnBindService(ClientConnection*, const Frame&);
  void OnInvokeMethod(ClientConnection*, const Frame&);
  void OnSetPeerIdentity(ClientConnection*, const Frame&);

  void ReplyToMethodInvocation(ClientID, RequestID, AsyncResult<ProtoMessage>);
  const ExposedService* GetServiceByName(const std::string&);

  static void SendFrame(ClientConnection*, const Frame&, int fd = -1);

  base::TaskRunner* const task_runner_;
  std::map<ServiceID, ExposedService> services_;
  std::unique_ptr<base::UnixSocket> sock_;  // The listening socket.
  std::map<ClientID, std::unique_ptr<ClientConnection>> clients_;
  std::map<base::UnixSocket*, ClientConnection*> clients_by_socket_;
  ServiceID last_service_id_ = 0;
  ClientID last_client_id_ = 0;
  uint32_t socket_tx_timeout_ms_ = kDefaultIpcTxTimeoutMs;
  PERFETTO_THREAD_CHECKER(thread_checker_)
  base::WeakPtrFactory<HostImpl> weak_ptr_factory_;  // Keep last.
};

}  // namespace ipc
}  // namespace perfetto

#endif  // SRC_IPC_HOST_IMPL_H_
