/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/proc_utils.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/pipe.h"
#include "perfetto/ext/base/subprocess.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/ext/traced/traced.h"
#include "src/perfetto_cmd/perfetto_cmd.h"
#include "src/websocket_bridge/websocket_bridge.h"

#if PERFETTO_BUILDFLAG(PERFETTO_TRACED_PERF)
#include "src/profiling/perf/traced_perf.h"
#endif

namespace perfetto {
namespace {

struct Applet {
  using MainFunction = int (*)(int /*argc*/, char** /*argv*/);
  const char* name;
  MainFunction entrypoint;
};

const Applet g_applets[]{
    {"traced", ServiceMain},
    {"traced_probes", ProbesMain},
#if PERFETTO_BUILDFLAG(PERFETTO_TRACED_PERF)
    {"traced_perf", TracedPerfMain},
#endif
    {"perfetto", PerfettoCmdMain},
    {"trigger_perfetto", TriggerPerfettoMain},
    {"websocket_bridge", WebsocketBridgeMain},
};

void PrintUsage() {
  printf(R"(Welcome to Perfetto tracing!

Tracebox is a bundle containing all the tracing services and the perfetto
cmdline client in one binary. It can be used either to spawn manually the
various subprocess or in "autostart" mode, which will take care of starting
and tearing down the services for you.

Usage in autostart mode:
  tracebox -t 10s -o trace_file.perfetto-trace sched/sched_switch
  See tracebox --help for more options.

Usage in manual mode:
  tracebox applet_name [args ...]  (e.g. ./tracebox traced --help)
  Applets:)");

  for (const Applet& applet : g_applets)
    printf(" %s", applet.name);

  printf(R"(

See also:
  * https://perfetto.dev/docs/
  * The config editor in the record page of https://ui.perfetto.dev/
)");
}

void PrintTraceboxUsage() {
  printf(R"(
Tracebox-specific args
  --system-sockets      : Forces the use of system-sockets when using autostart
                          mode. Cannot be used in applet mode.
)");
}

int TraceboxMain(int argc, char** argv) {
  // Manual mode: if either the 1st argument (argv[1]) or the exe name (argv[0])
  // match the name of an applet, directly invoke that without further
  // modifications.

  // Extract the file name from argv[0].
  char* slash = strrchr(argv[0], '/');
  char* argv0 = slash ? slash + 1 : argv[0];

  for (const Applet& applet : g_applets) {
    if (!strcmp(argv0, applet.name))
      return applet.entrypoint(argc, argv);
    if (argc > 1 && !strcmp(argv[1], applet.name))
      return applet.entrypoint(argc - 1, &argv[1]);
  }

  // If no matching applet is found, switch to the autostart mode. In this mode
  // we make tracebox behave like the cmdline client (without needing to prefix
  // it with "perfetto"), but will also start traced and traced_probes.
  // As part of this we also use a different namespace for the producer/consumer
  // sockets, to avoid clashing with the system daemon.

  if (argc <= 1) {
    PrintUsage();
    return 1;
  }

  auto* end = std::remove_if(argv, argv + argc, [](char* arg) {
    return !strcmp(arg, "--system-sockets");
  });
  if (end < (argv + argc - 1)) {
    PERFETTO_ELOG("Cannot specify --system-sockets multiple times");
    return 1;
  }
  if (bool system_sockets = end == (argv + argc - 1); system_sockets) {
    argc--;
  } else {
    auto pid_str = std::to_string(static_cast<uint64_t>(base::GetProcessId()));
#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
    // Use an unlinked abstract domain socket on Linux/Android.
    std::string consumer_socket = "@traced-c-" + pid_str;
    std::string producer_socket = "@traced-p-" + pid_str;
#elif PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
    std::string consumer_socket = "/tmp/traced-c-" + pid_str;
    std::string producer_socket = "/tmp/traced-p-" + pid_str;
#else
    PERFETTO_FATAL("The autostart mode is not supported on this platform");
#endif

    // If the caller has set the PERFETTO_*_SOCK_NAME, respect those.
    if (const char* env = getenv("PERFETTO_CONSUMER_SOCK_NAME"); env) {
      consumer_socket = env;
    }
    if (const char* env = getenv("PERFETTO_PRODUCER_SOCK_NAME"); env) {
      producer_socket = env;
    }
    base::SetEnv("PERFETTO_CONSUMER_SOCK_NAME", consumer_socket);
    base::SetEnv("PERFETTO_PRODUCER_SOCK_NAME", producer_socket);
  }

  PerfettoCmd perfetto_cmd;

  // If the cmdline parsing fails, stop here, no need to spawn services.
  // It will daemonize if --background. In that case the subprocesses will be
  // spawned by the damonized cmdline client, which is what we want so killing
  // the backgrounded cmdline client will also kill the other services, as they
  // will live in the same background session.
  auto opt_res = perfetto_cmd.ParseCmdlineAndMaybeDaemonize(argc, argv);
  if (opt_res.has_value()) {
    if (*opt_res != 0) {
      PrintTraceboxUsage();
    }
    return *opt_res;
  }

  std::string self_path = base::GetCurExecutablePath();
  base::Subprocess traced({self_path, "traced"});
#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  // |traced_sync_pipe| is used to synchronize with traced socket creation.
  // traced will write "1" and close the FD when the IPC socket is listening
  // (or traced crashed).
  base::Pipe traced_sync_pipe = base::Pipe::Create();
  int traced_fd = *traced_sync_pipe.wr;
  base::SetEnv("TRACED_NOTIFY_FD", std::to_string(traced_fd));
  traced.args.preserve_fds.emplace_back(traced_fd);
  // Create a new process group so CTRL-C is delivered only to the cmdline
  // process (the tracebox one) and not to traced. traced will still exit once
  // the main process exits, but this allows graceful stopping of the trace
  // without abruptedly killing traced{,probes} when hitting CTRL+C.
  traced.args.posix_proc_group_id = 0;  // 0 = start a new process group.
#endif
  traced.Start();

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  traced_sync_pipe.wr.reset();

  std::string traced_notify_msg;
  base::ReadPlatformHandle(*traced_sync_pipe.rd, &traced_notify_msg);
  if (traced_notify_msg != "1")
    PERFETTO_FATAL("The tracing service failed unexpectedly. Check the logs");
#endif

  base::Subprocess traced_probes(
      {self_path, "traced_probes", "--reset-ftrace"});
  // Put traced_probes in the same process group as traced. Same reason (CTRL+C)
  // but it's not worth creating a new group.
  traced_probes.args.posix_proc_group_id = traced.pid();
#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  // |traced_probes_sync_pipe| is used to synchronize with traced socket
  // creation. traced will write "1" and close the FD when the IPC socket is
  // listening (or traced crashed).
  base::Pipe traced_probes_sync_pipe = base::Pipe::Create();
  int traced_probes_fd = *traced_probes_sync_pipe.wr;
  base::SetEnv("TRACED_PROBES_NOTIFY_FD", std::to_string(traced_probes_fd));
  traced_probes.args.preserve_fds.emplace_back(traced_probes_fd);
#endif
  traced_probes.Start();

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  traced_probes_sync_pipe.wr.reset();

  std::string traced_probes_notify_msg;
  base::ReadPlatformHandle(*traced_probes_sync_pipe.rd,
                           &traced_probes_notify_msg);
  if (traced_probes_notify_msg != "1")
    PERFETTO_FATAL(
        "The traced_probes service failed unexpectedly. Check the logs");
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_TRACED_PERF)
  base::Subprocess traced_perf({self_path, "traced_perf"});
  // Put traced_perf in the same process group as traced. Same reason (CTRL+C)
  // but it's not worth creating a new group.
  traced_perf.args.posix_proc_group_id = traced.pid();

  base::Pipe traced_perf_sync_pipe = base::Pipe::Create();
  int traced_perf_fd = *traced_perf_sync_pipe.wr;
  base::SetEnv("TRACED_PERF_NOTIFY_FD", std::to_string(traced_perf_fd));
  traced_perf.args.preserve_fds.emplace_back(traced_perf_fd);
  traced_perf.Start();
  traced_perf_sync_pipe.wr.reset();

  std::string traced_perf_notify_msg;
  base::ReadPlatformHandle(*traced_perf_sync_pipe.rd, &traced_perf_notify_msg);
  if (traced_perf_notify_msg != "1")
    PERFETTO_FATAL(
        "The traced_perf service failed unexpectedly. Check the logs");
#endif

  perfetto_cmd.ConnectToServiceRunAndMaybeNotify();
  return 0;
}

}  // namespace
}  // namespace perfetto

int main(int argc, char** argv) {
  return perfetto::TraceboxMain(argc, argv);
}
