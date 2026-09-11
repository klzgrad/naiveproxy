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

#include "src/ipc/host_impl.h"

#include <algorithm>
#include <cinttypes>
#include <utility>

#include "perfetto/base/build_config.h"
#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/task_runner.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/android_utils.h"
#include "perfetto/ext/base/crash_keys.h"
#include "perfetto/ext/base/sys_types.h"
#include "perfetto/ext/base/unix_socket.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/ext/ipc/service.h"
#include "perfetto/ext/ipc/service_descriptor.h"

#include "protos/perfetto/ipc/wire_protocol.gen.h"

// TODO(primiano): put limits on #connections/uid and req. queue (b/69093705).

namespace perfetto {
namespace ipc {

namespace {

constexpr base::SockFamily kHostSockFamily =
    kUseTCPSocket ? base::SockFamily::kInet : base::SockFamily::kUnix;

base::CrashKey g_crash_key_uid("ipc_uid");

base::MachineID GenerateMachineID(base::UnixSocket* sock,
                                  const std::string& machine_id_hint) {
  // The special value of base::kDefaultMachineID is reserved for local
  // producers.
  if (!sock->is_connected() || sock->family() == base::SockFamily::kUnix)
    return base::kDefaultMachineID;

  base::FnvHasher hasher;
  // Use the hint from the client, or fallback to hostname if the client
  // doesn't provide a hint.
  if (!machine_id_hint.empty()) {
    hasher.Update(machine_id_hint);
  } else {
    // Use the socket address without the port number part as the hint.
    auto host_id = sock->GetSockAddr();
    auto pos = std::string::npos;
    switch (sock->family()) {
      case base::SockFamily::kInet:
        PERFETTO_FALLTHROUGH;
      case base::SockFamily::kInet6:
        PERFETTO_FALLTHROUGH;
      case base::SockFamily::kVsock:
        pos = host_id.rfind(":");
        if (pos != std::string::npos)
          host_id.resize(pos);
        break;
      case base::SockFamily::kUnspec:
        PERFETTO_FALLTHROUGH;
      case base::SockFamily::kUnix:
        PERFETTO_DFATAL("Should be unreachable.");
        return base::kDefaultMachineID;
    }
    hasher.Update(host_id);
  }

  // Take the lower 32-bit from the hash.
  uint32_t digest = static_cast<uint32_t>(hasher.digest());
  // Avoid the extremely unlikely case that the hasher digest happens to be 0.
  return digest == base::kDefaultMachineID ? 1 : digest;
}
}  // namespace

uid_t HostImpl::ClientConnection::GetPosixPeerUid() const {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) ||   \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_FREEBSD) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
  if (sock->family() == base::SockFamily::kUnix)
    return sock->peer_uid_posix();
#endif

  // For non-unix sockets, check if the UID is set in OnSetPeerIdentity().
  if (uid_override != base::kInvalidUid)
    return uid_override;
  // Must be != kInvalidUid or the PacketValidator will fail.
  return 0;
}

pid_t HostImpl::ClientConnection::GetLinuxPeerPid() const {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX_BUT_NOT_QNX) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  if (sock->family() == base::SockFamily::kUnix)
    return sock->peer_pid_linux();
#endif

  // For non-unix sockets, return the PID set in OnSetPeerIdentity().
  return pid_override;
}

// static
std::unique_ptr<Host> Host::CreateInstance(const char* socket_name,
                                           base::TaskRunner* task_runner) {
  std::unique_ptr<HostImpl> host(new HostImpl(socket_name, task_runner));
  if (!host->sock() || !host->sock()->is_listening())
    return nullptr;
  return std::unique_ptr<Host>(std::move(host));
}

// static
std::unique_ptr<Host> Host::CreateInstance(base::ScopedSocketHandle socket_fd,
                                           base::TaskRunner* task_runner) {
  std::unique_ptr<HostImpl> host(
      new HostImpl(std::move(socket_fd), task_runner));
  if (!host->sock() || !host->sock()->is_listening())
    return nullptr;
  return std::unique_ptr<Host>(std::move(host));
}

// static
std::unique_ptr<Host> Host::CreateInstance_Fuchsia(
    base::TaskRunner* task_runner) {
  return std::unique_ptr<HostImpl>(new HostImpl(task_runner));
}

HostImpl::HostImpl(base::ScopedSocketHandle socket_fd,
                   base::TaskRunner* task_runner)
    : task_runner_(task_runner),
      machine_name_(base::GetPerfettoMachineName()),
      weak_ptr_factory_(this) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  sock_ = base::UnixSocket::Listen(std::move(socket_fd), this, task_runner_,
                                   kHostSockFamily, base::SockType::kStream);
}

HostImpl::HostImpl(const char* socket_name, base::TaskRunner* task_runner)
    : task_runner_(task_runner),
      machine_name_(base::GetPerfettoMachineName()),
      weak_ptr_factory_(this) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  sock_ = base::UnixSocket::Listen(socket_name, this, task_runner_,
                                   base::GetSockFamily(socket_name),
                                   base::SockType::kStream);
  if (!sock_) {
    PERFETTO_PLOG("Failed to create %s", socket_name);
  }
}

HostImpl::HostImpl(base::TaskRunner* task_runner)
    : task_runner_(task_runner),
      machine_name_(base::GetPerfettoMachineName()),
      weak_ptr_factory_(this) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
}

HostImpl::~HostImpl() = default;

bool HostImpl::ExposeService(std::unique_ptr<Service> service) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  const std::string& service_name = service->GetDescriptor().service_name;
  if (GetServiceByName(service_name)) {
    PERFETTO_DLOG("Duplicate ExposeService(): %s", service_name.c_str());
    return false;
  }
  service->use_shmem_emulation_ =
      sock() && !base::SockShmemSupported(sock()->family());
  ServiceID sid = ++last_service_id_;
  ExposedService exposed_service(sid, service_name, std::move(service));
  services_.emplace(sid, std::move(exposed_service));
  return true;
}

void HostImpl::AdoptConnectedSocket_Fuchsia(
    base::ScopedSocketHandle connected_socket,
    std::function<bool(int)> send_fd_cb) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  PERFETTO_DCHECK(connected_socket);
  // Should not be used in conjunction with listen sockets.
  PERFETTO_DCHECK(!sock_);

  auto unix_socket = base::UnixSocket::AdoptConnected(
      std::move(connected_socket), this, task_runner_, kHostSockFamily,
      base::SockType::kStream);

  auto* unix_socket_ptr = unix_socket.get();
  OnNewIncomingConnection(nullptr, std::move(unix_socket));
  ClientConnection* client_connection = clients_by_socket_[unix_socket_ptr];
  client_connection->send_fd_cb_fuchsia = std::move(send_fd_cb);
  PERFETTO_DCHECK(client_connection->send_fd_cb_fuchsia);
}

void HostImpl::SetSocketSendTimeoutMs(uint32_t timeout_ms) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  // Should be less than the watchdog period (30s).
  socket_tx_timeout_ms_ = timeout_ms;
}

void HostImpl::OnNewIncomingConnection(
    base::UnixSocket*,
    std::unique_ptr<base::UnixSocket> new_conn) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  std::unique_ptr<ClientConnection> client(new ClientConnection());
  ClientID client_id = ++last_client_id_;
  clients_by_socket_[new_conn.get()] = client.get();
  client->id = client_id;
  client->sock = std::move(new_conn);
  client->sock->SetTxTimeout(socket_tx_timeout_ms_);
  if (client->sock->family() == base::SockFamily::kUnix) {
    client->machine_name = machine_name_;
  }
  clients_[client_id] = std::move(client);
}

void HostImpl::OnDataAvailable(base::UnixSocket* sock) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  auto it = clients_by_socket_.find(sock);
  if (it == clients_by_socket_.end())
    return;
  ClientConnection* client = it->second;
  BufferedFrameDeserializer& frame_deserializer = client->frame_deserializer;

  auto peer_uid = client->GetPosixPeerUid();
  auto scoped_key = g_crash_key_uid.SetScoped(static_cast<int64_t>(peer_uid));

  size_t rsize;
  do {
    auto buf = frame_deserializer.BeginReceive();
    base::ScopedFile fd;
    rsize = client->sock->Receive(buf.data, buf.size, &fd);
    if (fd) {
      PERFETTO_DCHECK(!client->received_fd);
      client->received_fd = std::move(fd);
    }
    if (!frame_deserializer.EndReceive(rsize))
      return OnDisconnect(client->sock.get());
  } while (rsize > 0);

  for (;;) {
    std::unique_ptr<Frame> frame = frame_deserializer.PopNextFrame();
    if (!frame)
      break;
    OnReceivedFrame(client, *frame);
  }
}

void HostImpl::OnReceivedFrame(ClientConnection* client,
                               const Frame& req_frame) {
  if (req_frame.has_msg_bind_service())
    return OnBindService(client, req_frame);
  if (req_frame.has_msg_invoke_method())
    return OnInvokeMethod(client, req_frame);
  if (req_frame.has_set_peer_identity())
    return OnSetPeerIdentity(client, req_frame);

  PERFETTO_DLOG("Received invalid RPC frame from client %" PRIu64, client->id);
  Frame reply_frame;
  reply_frame.set_request_id(req_frame.request_id());
  reply_frame.mutable_msg_request_error()->set_error("unknown request");
  SendFrame(client, reply_frame);
}

void HostImpl::OnBindService(ClientConnection* client, const Frame& req_frame) {
  // Binding a service doesn't do anything major. It just returns back the
  // service id and its method map.
  const Frame::BindService& req = req_frame.msg_bind_service();
  Frame reply_frame;
  reply_frame.set_request_id(req_frame.request_id());
  auto* reply = reply_frame.mutable_msg_bind_service_reply();
  const ExposedService* service = GetServiceByName(req.service_name());
  if (service) {
    reply->set_success(true);
    reply->set_service_id(service->id);
    uint32_t method_id = 1;  // method ids start at index 1.
    for (const auto& desc_method : service->instance->GetDescriptor().methods) {
      Frame::BindServiceReply::MethodInfo* method_info = reply->add_methods();
      method_info->set_name(desc_method.name);
      method_info->set_id(method_id++);
    }
  }
  SendFrame(client, reply_frame);
}

void HostImpl::OnInvokeMethod(ClientConnection* client,
                              const Frame& req_frame) {
  const Frame::InvokeMethod& req = req_frame.msg_invoke_method();
  Frame reply_frame;
  RequestID request_id = req_frame.request_id();
  reply_frame.set_request_id(request_id);
  reply_frame.mutable_msg_invoke_method_reply()->set_success(false);
  auto svc_it = services_.find(req.service_id());
  if (svc_it == services_.end())
    return SendFrame(client, reply_frame);  // |success| == false by default.

  Service* service = svc_it->second.instance.get();
  const ServiceDescriptor& svc = service->GetDescriptor();
  const auto& methods = svc.methods;
  const uint32_t method_id = req.method_id();
  if (method_id == 0 || method_id > methods.size())
    return SendFrame(client, reply_frame);

  const ServiceDescriptor::Method& method = methods[method_id - 1];
  std::unique_ptr<ProtoMessage> decoded_req_args(
      method.request_proto_decoder(req.args_proto()));
  if (!decoded_req_args)
    return SendFrame(client, reply_frame);

  Deferred<ProtoMessage> deferred_reply;
  base::WeakPtr<HostImpl> host_weak_ptr = weak_ptr_factory_.GetWeakPtr();
  ClientID client_id = client->id;

  if (!req.drop_reply()) {
    deferred_reply.Bind([host_weak_ptr, client_id,
                         request_id](AsyncResult<ProtoMessage> reply) {
      if (!host_weak_ptr)
        return;  // The reply came too late, the HostImpl has gone.
      host_weak_ptr->ReplyToMethodInvocation(client_id, request_id,
                                             std::move(reply));
    });
  }

  auto peer_uid = client->GetPosixPeerUid();
  auto scoped_key = g_crash_key_uid.SetScoped(static_cast<int64_t>(peer_uid));
  service->client_info_ =
      ClientInfo(client->id, peer_uid, client->GetLinuxPeerPid(),
                 client->GetMachineID(), client->GetMachineName());
  service->received_fd_ = &client->received_fd;
  method.invoker(service, *decoded_req_args, std::move(deferred_reply));
  service->received_fd_ = nullptr;
  service->client_info_ = ClientInfo();
}

void HostImpl::OnSetPeerIdentity(ClientConnection* client,
                                 const Frame& req_frame) {
  if (client->sock->family() == base::SockFamily::kUnix) {
    PERFETTO_DLOG("SetPeerIdentity is ignored for unix socket connections.");
    return;
  }

  // This is can only be set once by the relay service.
  if (client->pid_override != base::kInvalidPid ||
      client->uid_override != base::kInvalidUid) {
    PERFETTO_DLOG("Already received SetPeerIdentity.");
    return;
  }

  const auto& set_peer_identity = req_frame.set_peer_identity();
  client->pid_override = set_peer_identity.pid();
  client->uid_override = static_cast<uid_t>(set_peer_identity.uid());

  client->machine_id = GenerateMachineID(client->sock.get(),
                                         set_peer_identity.machine_id_hint());
  client->machine_name = set_peer_identity.machine_name();
}

void HostImpl::ReplyToMethodInvocation(ClientID client_id,
                                       RequestID request_id,
                                       AsyncResult<ProtoMessage> reply) {
  auto client_iter = clients_.find(client_id);
  if (client_iter == clients_.end())
    return;  // client has disconnected by the time we got the async reply.

  ClientConnection* client = client_iter->second.get();
  Frame reply_frame;
  reply_frame.set_request_id(request_id);

  // TODO(fmayer): add a test to guarantee that the reply is consumed within the
  // same call stack and not kept around. ConsumerIPCService::OnTraceData()
  // relies on this behavior.
  auto* reply_frame_data = reply_frame.mutable_msg_invoke_method_reply();
  reply_frame_data->set_has_more(reply.has_more());
  if (reply.success()) {
    std::string reply_proto = reply->SerializeAsString();
    reply_frame_data->set_reply_proto(reply_proto);
    reply_frame_data->set_success(true);
  }
  SendFrame(client, reply_frame, reply.fd());
}

// static
void HostImpl::SendFrame(ClientConnection* client, const Frame& frame, int fd) {
  auto peer_uid = client->GetPosixPeerUid();
  auto scoped_key = g_crash_key_uid.SetScoped(static_cast<int64_t>(peer_uid));

  std::string buf = BufferedFrameDeserializer::Serialize(frame);

  // On Fuchsia, |send_fd_cb_fuchsia_| is used to send the FD to the client
  // and therefore must be set.
  PERFETTO_DCHECK(!PERFETTO_BUILDFLAG(PERFETTO_OS_FUCHSIA) ||
                  client->send_fd_cb_fuchsia);
  if (client->send_fd_cb_fuchsia && fd != base::ScopedFile::kInvalid) {
    if (!client->send_fd_cb_fuchsia(fd)) {
      client->sock->Shutdown(true);
      return;
    }
    fd = base::ScopedFile::kInvalid;
  }

  // When a new Client connects in OnNewClientConnection we set a timeout on
  // Send (see call to SetTxTimeout).
  //
  // The old behaviour was to do a blocking I/O call, which caused crashes from
  // misbehaving producers (see b/169051440).
  bool res = client->sock->Send(buf.data(), buf.size(), fd);
  // If we timeout |res| will be false, but the UnixSocket will have called
  // UnixSocket::ShutDown() and thus |is_connected()| is false.
  PERFETTO_CHECK(res || !client->sock->is_connected());
}

void HostImpl::OnDisconnect(base::UnixSocket* sock) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  auto it = clients_by_socket_.find(sock);
  if (it == clients_by_socket_.end())
    return;
  auto* client = it->second;
  ClientID client_id = client->id;

  ClientInfo client_info(client_id, client->GetPosixPeerUid(),
                         client->GetLinuxPeerPid(), client->GetMachineID(),
                         client->GetMachineName());

  clients_by_socket_.erase(it);
  PERFETTO_DCHECK(clients_.count(client_id));
  clients_.erase(client_id);

  for (const auto& service_it : services_) {
    Service& service = *service_it.second.instance;
    service.client_info_ = client_info;
    service.OnClientDisconnected();
    service.client_info_ = ClientInfo();
  }
}

const HostImpl::ExposedService* HostImpl::GetServiceByName(
    const std::string& name) {
  // This could be optimized by using another map<name,ServiceID>. However this
  // is used only by Bind/ExposeService that are quite rare (once per client
  // connection and once per service instance), not worth it.
  for (const auto& it : services_) {
    if (it.second.name == name)
      return &it.second;
  }
  return nullptr;
}

HostImpl::ExposedService::ExposedService(ServiceID id_,
                                         const std::string& name_,
                                         std::unique_ptr<Service> instance_)
    : id(id_), name(name_), instance(std::move(instance_)) {}

HostImpl::ExposedService::ExposedService(ExposedService&&) noexcept = default;
HostImpl::ExposedService& HostImpl::ExposedService::operator=(
    HostImpl::ExposedService&&) = default;
HostImpl::ExposedService::~ExposedService() = default;

HostImpl::ClientConnection::~ClientConnection() = default;

}  // namespace ipc
}  // namespace perfetto
