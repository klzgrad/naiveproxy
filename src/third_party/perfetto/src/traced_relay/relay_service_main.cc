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

#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/getopt.h"
#include "perfetto/ext/base/lock_free_task_runner.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/version.h"
#include "perfetto/ext/base/watchdog.h"
#include "perfetto/ext/traced/traced.h"
#include "perfetto/tracing/default_socket.h"
#include "src/traced_relay/relay_service.h"

namespace perfetto {
namespace {
void PrintUsage(const char* prog_name) {
  fprintf(stderr, R"(
Relays trace data to a remote tracing service. Cannot run alongside the "traced"
daemon.

Usage: %s [OPTION]...

Options:
  --background
      Run as a background process.
  --set-socket-permissions <GROUP>:<MODE>
      Set group ownership and permissions for the listening socket.
      Example: traced-producer:0660 (rw-rw----)
  --version
      Display version information and exit.

Environment Variable:
  PERFETTO_RELAY_SOCK_NAME
      Socket name of the remote tracing service.
      Example: 192.168.0.1:20001 or vsock://2:20001

Example:
  PERFETTO_RELAY_SOCK_NAME=192.168.0.1:20001 %s \
      --set-socket-permissions traced-producer:0660

  Starts the service, relaying trace data to 192.168.0.1:20001.
  The local listening socket's group is set to "traced-producer" with
  permissions 0660.
)",
          prog_name, prog_name);
}

}  // namespace

static int RelayServiceMain(int argc, char** argv) {
  enum LongOption {
    OPT_VERSION = 1000,
    OPT_SET_SOCKET_PERMISSIONS = 1001,
    OPT_BACKGROUND,
  };

  bool background = false;

  static const option long_options[] = {
      {"background", no_argument, nullptr, OPT_BACKGROUND},
      {"version", no_argument, nullptr, OPT_VERSION},
      {"set-socket-permissions", required_argument, nullptr,
       OPT_SET_SOCKET_PERMISSIONS},
      {nullptr, 0, nullptr, 0}};

  std::string listen_socket_group, listen_socket_mode_bits;

  for (;;) {
    int option = getopt_long(argc, argv, "", long_options, nullptr);
    if (option == -1)
      break;
    switch (option) {
      case OPT_BACKGROUND:
        background = true;
        break;
      case OPT_VERSION:
        printf("%s\n", base::GetVersionString());
        return 0;
      case OPT_SET_SOCKET_PERMISSIONS: {
        // Check that the socket permission argument is well formed.
        auto parts = perfetto::base::SplitString(std::string(optarg), ":");
        PERFETTO_CHECK(parts.size() == 2);
        PERFETTO_CHECK(
            std::all_of(parts.cbegin(), parts.cend(),
                        [](const std::string& part) { return !part.empty(); }));
        listen_socket_group = parts[0];
        listen_socket_mode_bits = parts[1];
        break;
      }
      default:
        PrintUsage(argv[0]);
        return 1;
    }
  }

  if (GetRelaySocket().empty()) {
    PrintUsage(argv[0]);
    return 1;
  }

  if (background) {
    base::Daemonize([] { return 0; });
  }

  base::MaybeLockFreeTaskRunner task_runner;
  auto svc = std::make_unique<RelayService>(&task_runner);

  // traced_relay binds to the producer socket of the `traced` service. When
  // built for Android, this socket is created and bound during init, and its
  // file descriptor is passed through the environment variable.
  const char* env_prod = getenv("ANDROID_SOCKET_traced_producer");
  base::ScopedFile producer_fd;
  if (env_prod) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
    PERFETTO_CHECK(false);
#else
    auto opt_fd = base::CStringToInt32(env_prod);
    if (opt_fd.has_value())
      producer_fd.reset(*opt_fd);

    svc->Start(std::move(producer_fd), GetRelaySocket());
#endif
  } else {
    auto listen_socket = GetProducerSocket();
    remove(listen_socket);
    if (!listen_socket_group.empty()) {
      auto status = base::SetFilePermissions(listen_socket, listen_socket_group,
                                             listen_socket_mode_bits);
      if (!status.ok()) {
        PERFETTO_ELOG("Failed to set socket permissions: %s",
                      status.c_message());
        return 1;
      }
    }

    svc->Start(listen_socket, GetRelaySocket());
  }

  // Set the CPU limit and start the watchdog running. The memory limit will
  // be set inside the service code as it relies on the size of buffers.
  // The CPU limit is the generic one defined in watchdog.h.
  base::Watchdog* watchdog = base::Watchdog::GetInstance();
  watchdog->SetCpuLimit(base::kWatchdogDefaultCpuLimit,
                        base::kWatchdogDefaultCpuWindow);
  watchdog->Start();

  PERFETTO_ILOG("Started traced_relay, listening on %s, forwarding to %s",
                GetProducerSocket(), GetRelaySocket().c_str());

  task_runner.Run();
  return 0;
}
}  // namespace perfetto

int main(int argc, char** argv) {
  return perfetto::RelayServiceMain(argc, argv);
}
