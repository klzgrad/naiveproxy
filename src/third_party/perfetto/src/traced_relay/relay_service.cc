/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "src/traced_relay/relay_service.h"

#include <functional>
#include <memory>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/task_runner.h"
#include "perfetto/ext/base/android_utils.h"
#include "perfetto/ext/base/clock_snapshots.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/hash.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/unix_socket.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/ext/base/version.h"
#include "perfetto/ext/ipc/client.h"
#include "perfetto/tracing/core/forward_decls.h"
#include "protos/perfetto/ipc/wire_protocol.gen.h"
#include "src/ipc/buffered_frame_deserializer.h"
#include "src/traced_relay/socket_relay_handler.h"
#include "src/tracing/ipc/producer/relay_ipc_client.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) ||   \
    PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
#endif

// Non-QNX include statements
#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) ||           \
    PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX_BUT_NOT_QNX) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
#include <sys/syscall.h>
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_OS_QNX)
#include <sys/neutrino.h>
#include <sys/syspage.h>
#endif

using ::perfetto::protos::gen::IPCFrame;

namespace perfetto {
namespace {

std::string GenerateSetPeerIdentityRequest(int32_t pid,
                                           int32_t uid,
                                           const std::string& machine_id_hint) {
  IPCFrame ipc_frame;
  ipc_frame.set_request_id(0);

  auto* set_peer_identity = ipc_frame.mutable_set_peer_identity();
#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  set_peer_identity->set_pid(pid);
#else
  base::ignore_result(pid);
#endif
  set_peer_identity->set_uid(uid);
  set_peer_identity->set_machine_id_hint(machine_id_hint);

  return ipc::BufferedFrameDeserializer::Serialize(ipc_frame);
}

void SetSystemInfo(protos::gen::InitRelayRequest* request) {
  base::SystemInfo sys_info = base::GetSystemInfo();

  auto* info = request->mutable_system_info();
  info->set_tracing_service_version(base::GetVersionString());

  if (sys_info.timezone_off_mins.has_value())
    info->set_timezone_off_mins(*sys_info.timezone_off_mins);

  if (sys_info.utsname_info.has_value()) {
    auto* utsname_info = info->mutable_utsname();
    utsname_info->set_sysname(sys_info.utsname_info->sysname);
    utsname_info->set_version(sys_info.utsname_info->version);
    utsname_info->set_machine(sys_info.utsname_info->machine);
    utsname_info->set_release(sys_info.utsname_info->release);
  }

  if (sys_info.page_size.has_value())
    info->set_page_size(*sys_info.page_size);
  if (sys_info.num_cpus.has_value())
    info->set_num_cpus(*sys_info.num_cpus);

  if (!sys_info.android_build_fingerprint.empty())
    info->set_android_build_fingerprint(sys_info.android_build_fingerprint);
  if (!sys_info.android_device_manufacturer.empty())
    info->set_android_device_manufacturer(sys_info.android_device_manufacturer);
  if (sys_info.android_sdk_version.has_value())
    info->set_android_sdk_version(*sys_info.android_sdk_version);
  if (!sys_info.android_soc_model.empty())
    info->set_android_soc_model(sys_info.android_soc_model);
  if (!sys_info.android_guest_soc_model.empty())
    info->set_android_guest_soc_model(sys_info.android_guest_soc_model);
  if (!sys_info.android_hardware_revision.empty())
    info->set_android_hardware_revision(sys_info.android_hardware_revision);
  if (!sys_info.android_storage_model.empty())
    info->set_android_storage_model(sys_info.android_storage_model);
  if (!sys_info.android_ram_model.empty())
    info->set_android_ram_model(sys_info.android_ram_model);
}

}  // Anonymous namespace.

RelayClient::~RelayClient() = default;
RelayClient::RelayClient(const std::string& client_sock_name,
                         const std::string& machine_id_hint,
                         base::TaskRunner* task_runner,
                         OnErrorCallback on_error_callback)
    : task_runner_(task_runner),
      on_error_callback_(on_error_callback),
      client_sock_name_(client_sock_name),
      machine_id_hint_(machine_id_hint) {
  Connect();
}

void RelayClient::Connect() {
  auto sock_family = base::GetSockFamily(client_sock_name_.c_str());
  client_sock_ =
      base::UnixSocket::Connect(client_sock_name_, this, task_runner_,
                                sock_family, base::SockType::kStream);
}

void RelayClient::NotifyError() {
  if (!on_error_callback_)
    return;

  // Can only notify once.
  on_error_callback_();
  on_error_callback_ = nullptr;
}

void RelayClient::OnConnect(base::UnixSocket* self, bool connected) {
  if (!connected) {
    return NotifyError();
  }

  // The RelayClient needs to send its peer identity to the host.
  auto req = GenerateSetPeerIdentityRequest(
      getpid(), static_cast<int32_t>(geteuid()), machine_id_hint_);
  if (!self->SendStr(req)) {
    return NotifyError();
  }

  // Create the IPC client with a connected socket.
  PERFETTO_DCHECK(self == client_sock_.get());
  auto sock_fd = client_sock_->ReleaseSocket().ReleaseFd();
  client_sock_ = nullptr;
  relay_ipc_client_ = std::make_unique<RelayIPCClient>(
      ipc::Client::ConnArgs(std::move(sock_fd)),
      weak_factory_for_ipc_client.GetWeakPtr(), task_runner_);
}

void RelayClient::OnServiceConnected() {
  InitRelayRequest();
  phase_ = Phase::PING;
  SendSyncClockRequest();
}

void RelayClient::OnServiceDisconnected() {
  NotifyError();
}

void RelayClient::InitRelayRequest() {
  protos::gen::InitRelayRequest request;

  SetSystemInfo(&request);

  relay_ipc_client_->InitRelay(request);
}

void RelayClient::SendSyncClockRequest() {
  protos::gen::SyncClockRequest request;
  switch (phase_) {
    case Phase::CONNECTING:
      PERFETTO_DFATAL("Should be unreachable.");
      return;
    case Phase::PING:
      request.set_phase(SyncClockRequest::PING);
      break;
    case Phase::UPDATE:
      request.set_phase(SyncClockRequest::UPDATE);
      break;
  }

  base::ClockSnapshotVector snapshot_data = base::CaptureClockSnapshots();
  for (auto& clock : snapshot_data) {
    auto* clock_proto = request.add_clocks();
    clock_proto->set_clock_id(clock.clock_id);
    clock_proto->set_timestamp(clock.timestamp);
  }

  relay_ipc_client_->SyncClock(request);
}

void RelayClient::OnSyncClockResponse(const protos::gen::SyncClockResponse&) {
  static constexpr uint32_t kSyncClockIntervalMs = 30000;  // 30 Sec.
  switch (phase_) {
    case Phase::CONNECTING:
      PERFETTO_DFATAL("Should be unreachable.");
      break;
    case Phase::PING:
      phase_ = Phase::UPDATE;
      SendSyncClockRequest();
      break;
    case Phase::UPDATE:
      // The client finished one run of clock sync. Schedule for next sync after
      // 30 sec.
      clock_synced_with_service_for_testing_ = true;
      task_runner_->PostDelayedTask(
          [self = weak_factory_.GetWeakPtr()]() {
            if (!self)
              return;

            self->phase_ = Phase::PING;
            self->SendSyncClockRequest();
          },
          kSyncClockIntervalMs);
      break;
  }
}

RelayService::RelayService(base::TaskRunner* task_runner)
    : task_runner_(task_runner), machine_id_hint_(GetMachineIdHint()) {}

void RelayService::Start(const char* listening_socket_name,
                         std::string client_socket_name) {
  auto sock_family = base::GetSockFamily(listening_socket_name);
  listening_socket_ =
      base::UnixSocket::Listen(listening_socket_name, this, task_runner_,
                               sock_family, base::SockType::kStream);
  bool producer_socket_listening =
      listening_socket_ && listening_socket_->is_listening();
  if (!producer_socket_listening) {
    PERFETTO_FATAL("Failed to listen to socket %s", listening_socket_name);
  }

  // Save |client_socket_name| for opening new client connection to remote
  // service when a local producer connects.
  client_socket_name_ = client_socket_name;

  ConnectRelayClient();
}

void RelayService::Start(base::ScopedSocketHandle server_socket_handle,
                         std::string client_socket_name) {
  // Called when the service is started by Android init, where
  // |server_socket_handle| is a unix socket.
  listening_socket_ = base::UnixSocket::Listen(
      std::move(server_socket_handle), this, task_runner_,
      base::SockFamily::kUnix, base::SockType::kStream);
  bool producer_socket_listening =
      listening_socket_ && listening_socket_->is_listening();
  if (!producer_socket_listening) {
    PERFETTO_FATAL("Failed to listen to the server socket");
  }

  // Save |client_socket_name| for opening new client connection to remote
  // service when a local producer connects.
  client_socket_name_ = client_socket_name;

  ConnectRelayClient();
}

void RelayService::OnNewIncomingConnection(
    base::UnixSocket* listen_socket,
    std::unique_ptr<base::UnixSocket> server_conn) {
  PERFETTO_DCHECK(listen_socket == listening_socket_.get());

  // Create a connection to the host to pair with |listen_conn|.
  auto sock_family = base::GetSockFamily(client_socket_name_.c_str());
  auto client_conn =
      base::UnixSocket::Connect(client_socket_name_, this, task_runner_,
                                sock_family, base::SockType::kStream);

  // Pre-queue the SetPeerIdentity request. By enqueueing it into the buffer,
  // this will be sent out as first frame as soon as we connect to the real
  // traced.
  //
  // This code pretends that we received a SetPeerIdentity frame from the
  // connecting producer (while instead we are just forging it). The host traced
  // will only accept only one SetPeerIdentity request pre-queued here.
  int32_t pid = base::kInvalidPid;
#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX_BUT_NOT_QNX) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  pid = server_conn->peer_pid_linux();
#endif
  auto req = GenerateSetPeerIdentityRequest(
      pid, static_cast<int32_t>(server_conn->peer_uid_posix()),
      machine_id_hint_);
  // Buffer the SetPeerIdentity request.
  SocketWithBuffer server, client;
  PERFETTO_CHECK(server.available_bytes() >= req.size());
  memcpy(server.buffer(), req.data(), req.size());
  server.EnqueueData(req.size());

  // Shut down all callbacks associated with the socket in preparation for the
  // transfer to |socket_relay_handler_|.
  server.sock = server_conn->ReleaseSocket();
  auto new_socket_pair =
      std::make_unique<SocketPair>(std::move(server), std::move(client));
  pending_connections_.push_back(
      {std::move(new_socket_pair), std::move(client_conn)});
}

void RelayService::OnConnect(base::UnixSocket* self, bool connected) {
  // This only happens when the client connection is connected or has failed.
  auto it =
      std::find_if(pending_connections_.begin(), pending_connections_.end(),
                   [&](const PendingConnection& pending_conn) {
                     return pending_conn.connecting_client_conn.get() == self;
                   });
  PERFETTO_CHECK(it != pending_connections_.end());
  // Need to remove the element in |pending_connections_| regardless of
  // |connected|.
  auto remover = base::OnScopeExit([&]() { pending_connections_.erase(it); });

  if (!connected)
    return;  // This closes both sockets in PendingConnection.

  // Shut down event handlers and pair with a server connection.
  it->socket_pair->second.sock = self->ReleaseSocket();

  // Transfer the socket pair to SocketRelayHandler.
  socket_relay_handler_.AddSocketPair(std::move(it->socket_pair));
}

void RelayService::OnDisconnect(base::UnixSocket*) {
  PERFETTO_DFATAL("Should be unreachable.");
}

void RelayService::OnDataAvailable(base::UnixSocket*) {
  PERFETTO_DFATAL("Should be unreachable.");
}

void RelayService::ReconnectRelayClient() {
  static constexpr uint32_t kMaxRelayClientRetryDelayMs = 30000;
  task_runner_->PostDelayedTask([this]() { this->ConnectRelayClient(); },
                                relay_client_retry_delay_ms_);
  relay_client_retry_delay_ms_ =
      relay_client_->ipc_client_connected()
          ? kDefaultRelayClientRetryDelayMs
          : std::min(kMaxRelayClientRetryDelayMs,
                     relay_client_retry_delay_ms_ * 2);
}

void RelayService::ConnectRelayClient() {
  if (relay_client_disabled_for_testing_)
    return;

  relay_client_ = std::make_unique<RelayClient>(
      client_socket_name_, machine_id_hint_, task_runner_,
      [this]() { this->ReconnectRelayClient(); });
}

std::string RelayService::GetMachineIdHint(
    bool use_pseudo_boot_id_for_testing) {
  // Gets kernel boot ID if available.
  std::string boot_id;
  if (!use_pseudo_boot_id_for_testing &&
      base::ReadFile("/proc/sys/kernel/random/boot_id", &boot_id)) {
    return base::StripSuffix(boot_id, "\n");
  }

#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) ||   \
    PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
  auto get_pseudo_boot_id = []() -> std::string {
    base::Hasher hasher;
    // Generate a pseudo-unique identifier for the current machine.
    // Source 1: system boot timestamp from the creation time of /dev inode.
#if PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
    // Mac or iOS, just use stat(2).
    const char* dev_path = "/dev";
    struct stat stat_buf{};
    int rc = PERFETTO_EINTR(stat(dev_path, &stat_buf));
    if (rc == -1)
      return std::string();
    hasher.Update(reinterpret_cast<const char*>(&stat_buf.st_birthtimespec),
                  sizeof(stat_buf.st_birthtimespec));
#elif PERFETTO_BUILDFLAG(PERFETTO_OS_QNX)
    // QNX doesn't support the file birthtime flag in the stat structure.
    // In order to still calculate the system boot time in epoch seconds
    // we get the current epoch seconds and subtract the amount of time
    // since boot. This is a more generic approach that could be used in
    // general for POSIX operating systems (even though is not as accurate).
    timespec system_boottime;
    uint64_t timesinceboot_secs;

    // Get current epoch time
    int rc = clock_gettime(CLOCK_REALTIME, &system_boottime);
    if (rc == 0)
      return std::string();

    // Get seconds since system boot
    timesinceboot_secs = ClockCycles() / SYSPAGE_ENTRY(qtime)->cycles_per_sec;

    // Calculate system boot time in epoch seconds
    if (timesinceboot_secs > static_cast<uint64_t>(system_boottime.tv_sec))
      return std::string();

    system_boottime.tv_sec -= timesinceboot_secs;

    hasher.Update(reinterpret_cast<const char*>(&system_boottime),
                  sizeof(system_boottime));
#else
    // Android or Linux, use statx(2)
    const char* dev_path = "/dev";
    struct statx stat_buf{};
    auto rc = PERFETTO_EINTR(syscall(__NR_statx, /*dirfd=*/-1, dev_path,
                                     /*flags=*/0, STATX_BTIME, &stat_buf));
    if (rc == -1)
      return std::string();
    hasher.Update(reinterpret_cast<const char*>(&stat_buf.stx_btime),
                  sizeof(stat_buf.stx_btime));
#endif

    // Source 2: uname(2).
    utsname kernel_info{};
    if (uname(&kernel_info) == -1)
      return std::string();

    // Create a non-cryptographic digest of bootup timestamp and everything in
    // utsname.
    hasher.Update(reinterpret_cast<const char*>(&kernel_info),
                  sizeof(kernel_info));
    return base::Uint64ToHexStringNoPrefix(hasher.digest());
  };

  auto pseudo_boot_id = get_pseudo_boot_id();
  if (!pseudo_boot_id.empty())
    return pseudo_boot_id;
#endif

  // If all above failed, return nothing.
  return std::string();
}

}  // namespace perfetto
