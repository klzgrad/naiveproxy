/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "src/trace_processor/shell/server_subcommand.h"

#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/ext/trace_processor/trace_processor_shell.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/trace_processor/rpc/rpc.h"
#include "src/trace_processor/rpc/session_lifecycle.h"
#include "src/trace_processor/rpc/session_paths.h"
#include "src/trace_processor/rpc/stdiod.h"
#include "src/trace_processor/shell/common_flags.h"
#include "src/trace_processor/shell/metatrace.h"
#include "src/trace_processor/shell/subcommand.h"

#if PERFETTO_BUILDFLAG(PERFETTO_TP_HTTPD)
#include "src/trace_processor/rpc/httpd.h"
#endif

// The unix server depends on the IPC layer (AF_UNIX sockets), which is not
// available on all platforms (e.g. Chromium+Windows).
#if PERFETTO_BUILDFLAG(PERFETTO_IPC)
#include "src/trace_processor/rpc/unixd.h"
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) ||   \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_FREEBSD) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
#define PERFETTO_HAS_SIGNAL_H() 1
#include <signal.h>
#else
#define PERFETTO_HAS_SIGNAL_H() 0
#endif

// `server kill` signals a process by pid.
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <windows.h>
#else
#include <csignal>
#endif

namespace perfetto::trace_processor::shell {

const char* ServerSubcommand::name() const {
  return "server";
}

const char* ServerSubcommand::description() const {
  return "Start an RPC server.";
}

const char* ServerSubcommand::usage_args() const {
  return "<mode> [trace_file|name]";
}

const char* ServerSubcommand::detailed_help() const {
  return R"(Start (or stop) an RPC server for remote trace processor access.

Modes:
  http   Start an HTTP server (default port 9001). This is what the
         Perfetto UI (ui.perfetto.dev) connects to. Configure with
         --port and --ip-address.
  stdio  Communicate via stdin/stdout using length-prefixed RPC protocol.
         Used by tooling that embeds trace processor as a subprocess.
  unix   Serve over an AF_UNIX socket addressed by name, keeping the trace
         warm for repeated 'query --remote <name>' calls. Use --name to pick
         a name (otherwise one is generated) or --path for an explicit socket
         path. Runs in the foreground; press Ctrl-C to stop.
  kill   Stop a running `server unix` session by name or socket path.

The trace file is optional in http and unix modes; a client can load one
later over the wire.)";
}

std::vector<FlagSpec> ServerSubcommand::GetFlags() {
  return {
      StringFlag("port", '\0', "PORT", "HTTP port.", &port_number_),
      StringFlag("ip-address", '\0', "IP", "HTTP bind address.", &listen_ip_),
      StringFlag("additional-cors-origins", '\0', "O1,O2,...",
                 "Additional CORS origins for HTTP mode.",
                 &additional_cors_origins_str_),
      StringFlag("name", '\0', "NAME",
                 "Session name for unix mode (default: auto-generated).",
                 &session_name_),
      StringFlag("path", '\0', "PATH",
                 "Explicit socket path for unix mode (overrides --name).",
                 &socket_path_),
      StringFlag("idle-timeout", '\0', "auto|DUR",
                 "Reap the server after this much inactivity (e.g. 30m, 90s). "
                 "'auto' = 30m for unix, never for http; 0/never disables.",
                 &idle_timeout_str_),
      StringFlag("idle-start", '\0', "auto|orphaned|last-query",
                 "When the idle clock applies (default auto: owner-aware).",
                 &idle_start_str_),
      BoolFlag("daemonize", '\0',
               "Detach into the background (unix mode, POSIX only).",
               &daemonize_),
  };
}

namespace {

[[maybe_unused]] base::StatusOr<IdleStart> ParseIdleStart(
    const std::string& s) {
  if (s == "auto")
    return IdleStart::kAuto;
  if (s == "orphaned")
    return IdleStart::kOrphaned;
  if (s == "last-query")
    return IdleStart::kLastQuery;
  return base::ErrStatus(
      "Invalid --idle-start '%s' (expected auto, orphaned or last-query)",
      s.c_str());
}

// Resolves --idle-timeout to milliseconds. "auto" means |auto_default_ms|.
[[maybe_unused]] base::StatusOr<uint32_t> ResolveIdleTimeout(
    const std::string& s,
    uint32_t auto_default_ms) {
  if (s.empty() || s == "auto")
    return auto_default_ms;
  return session::ParseDurationMs(s);
}

[[maybe_unused]] constexpr uint32_t kUnixDefaultIdleMs =
    30 * 60 * 1000;  // 30 minutes.

// Returns true if |pid| is alive (POSIX probe; Windows opens a handle).
bool IsPidAlive(int pid) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
  if (!h)
    return false;
  bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
  CloseHandle(h);
  return alive;
#else
  return kill(pid, 0) == 0;
#endif
}

base::Status TerminatePid(int pid) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
  if (!h || !TerminateProcess(h, 0)) {
    if (h)
      CloseHandle(h);
    return base::ErrStatus("Failed to terminate pid %d", pid);
  }
  CloseHandle(h);
  return base::OkStatus();
#else
  if (kill(pid, SIGTERM) != 0)
    return base::ErrStatus("Failed to signal pid %d", pid);
  return base::OkStatus();
#endif
}

// Stops a running `server unix` session by name or socket path.
base::Status KillServer(const std::string& addr) {
  std::string socket_path;
  switch (session::ClassifyRemoteAddr(addr)) {
    case session::RemoteAddrKind::kHttp:
      return base::ErrStatus(
          "server kill over HTTP (%s) is not supported; stop the server with "
          "Ctrl-C or run it with --idle-timeout.",
          addr.c_str());
    case session::RemoteAddrKind::kUnixPath:
      socket_path = addr;
      break;
    case session::RemoteAddrKind::kSessionName: {
      ASSIGN_OR_RETURN(socket_path, session::SessionSocketPath(addr));
      break;
    }
  }

  // The server writes its pid beside the socket; stop it by pid.
  std::string pid_path = socket_path + session::kPidFileSuffix;
  std::string contents;
  std::optional<int32_t> pid;
  if (base::ReadFile(pid_path, &contents))
    pid = base::StringToInt32(base::TrimWhitespace(contents));
  if (!pid.has_value() || !IsPidAlive(*pid)) {
    remove(pid_path.c_str());  // Stale pid/socket from a crashed server.
    return base::ErrStatus("No live session at '%s'", addr.c_str());
  }

  RETURN_IF_ERROR(TerminatePid(*pid));
  // POSIX unlinks these from the signal handler; Windows kill is abrupt, so
  // clean up here too (remove() of a gone path is harmless).
  remove(pid_path.c_str());
  remove(socket_path.c_str());
  printf("Stopped session '%s' (pid %d)\n", addr.c_str(), *pid);
  return base::OkStatus();
}

}  // namespace

base::Status ServerSubcommand::Run(const SubcommandContext& ctx) {
  RETURN_IF_ERROR(RejectExtraPositionals(ctx, "server", 2));
  // First positional arg is the mode.
  if (ctx.positional_args.empty()) {
    return base::ErrStatus(
        "server: must specify mode (expected http, stdio, unix or kill)");
  }
  const std::string& mode = ctx.positional_args[0];

  // `kill` needs no trace processor, so handle it before the setup below.
  if (mode == "kill") {
    if (ctx.positional_args.size() < 2) {
      return base::ErrStatus(
          "server kill: a session name or socket path is required");
    }
    return KillServer(ctx.positional_args[1]);
  }

  // Optional trace file is second positional arg.
  std::string trace_file;
  if (ctx.positional_args.size() >= 2) {
    trace_file = ctx.positional_args[1];
  }

  ASSIGN_OR_RETURN(Config config, BuildConfig(*ctx.global, ctx.platform));
  ASSIGN_OR_RETURN(auto tp,
                   SetupTraceProcessor(*ctx.global, config, ctx.platform));

  if (!trace_file.empty()) {
    ASSIGN_OR_RETURN(auto t_load,
                     LoadTraceFile(tp.get(), ctx.platform, trace_file));
    base::ignore_result(t_load);
  }

  bool has_trace = !trace_file.empty();

  if (mode == "stdio") {
    Rpc rpc(std::move(tp), has_trace, config, ctx.platform,
            [&ctx](TraceProcessor* new_tp) {
              ctx.platform->OnTraceProcessorCreated(new_tp);
            });
#if PERFETTO_HAS_SIGNAL_H()
    static Rpc* g_rpc_for_signal_handler = &rpc;
    signal(SIGINT, [](int) {
      g_rpc_for_signal_handler->trace_processor()->InterruptQuery();
    });
#endif
    return RunStdioRpcServer(rpc);
  }

  if (mode == "http") {
#if PERFETTO_BUILDFLAG(PERFETTO_TP_HTTPD)
    std::vector<std::string> additional_cors_origins;
    if (!additional_cors_origins_str_.empty()) {
      additional_cors_origins =
          base::SplitString(additional_cors_origins_str_, ",");
    }
    // http defaults to never reaping (the UI relies on an always-on server);
    // an explicit --idle-timeout opts in.
    ASSIGN_OR_RETURN(IdleStart idle_start, ParseIdleStart(idle_start_str_));
    ASSIGN_OR_RETURN(uint32_t idle_timeout_ms,
                     ResolveIdleTimeout(idle_timeout_str_, /*auto=*/0));
    Rpc rpc(std::move(tp), has_trace, config, ctx.platform,
            [&ctx](TraceProcessor* new_tp) {
              ctx.platform->OnTraceProcessorCreated(new_tp);
            });
#if PERFETTO_HAS_SIGNAL_H()
    if (ctx.global->metatrace_path.empty()) {
      signal(SIGINT, SIG_DFL);
    } else {
      static std::string* metatrace_path = &ctx.global->metatrace_path;
      static Rpc* g_rpc_for_signal_handler = &rpc;
      signal(SIGINT, [](int) {
        MaybeWriteMetatrace(g_rpc_for_signal_handler->trace_processor(),
                            *metatrace_path);
        exit(1);
      });
    }
#endif
    RunHttpRPCServer(rpc, listen_ip_, port_number_, additional_cors_origins,
                     idle_timeout_ms, idle_start);
    // Returns only if the idle reaper fired (idle_timeout_ms > 0).
    return base::OkStatus();
#else
    return base::ErrStatus("HTTP RPC module not supported in this build");
#endif
  }

  if (mode == "unix") {
#if !PERFETTO_BUILDFLAG(PERFETTO_IPC)
    return base::ErrStatus("Unix RPC module not supported in this build");
#else
    // --path and --name are mutually exclusive: --path is an explicit socket
    // path while --name selects one in the managed session dir.
    if (!socket_path_.empty() && !session_name_.empty()) {
      return base::ErrStatus(
          "server unix: --path and --name are mutually exclusive");
    }

    std::string socket_path = socket_path_;
    std::string session_name = session_name_;
    if (socket_path.empty()) {
      if (session_name.empty()) {
        session_name = session::GenerateSessionName();
      } else if (!session::IsValidSessionName(session_name)) {
        return base::ErrStatus(
            "server unix: invalid --name '%s' (must match "
            "[A-Za-z0-9][A-Za-z0-9_-]*)",
            session_name.c_str());
      }
      ASSIGN_OR_RETURN(socket_path, session::SessionSocketPath(session_name));
    } else {
      // Anchor a relative --path before binding: daemonizing chdirs to "/".
      socket_path = session::MakeAbsolutePath(socket_path);
      RETURN_IF_ERROR(session::ValidateAfUnixPathLength(socket_path));
      if (session_name.empty())
        session_name = socket_path;
    }

    ASSIGN_OR_RETURN(IdleStart idle_start, ParseIdleStart(idle_start_str_));
    ASSIGN_OR_RETURN(uint32_t idle_timeout_ms,
                     ResolveIdleTimeout(idle_timeout_str_, kUnixDefaultIdleMs));

    Rpc rpc(std::move(tp), has_trace, config, ctx.platform,
            [&ctx](TraceProcessor* new_tp) {
              ctx.platform->OnTraceProcessorCreated(new_tp);
            });
    UnixServerArgs server_args;
    server_args.socket_path = socket_path;
    server_args.session_name = session_name;
    server_args.idle_timeout_ms = idle_timeout_ms;
    server_args.idle_start = idle_start;
    server_args.daemonize = daemonize_;
    return RunUnixRpcServer(rpc, server_args);
#endif
  }

  return base::ErrStatus(
      "server: unknown mode '%s' (expected http, stdio, unix or kill)",
      mode.c_str());
}

}  // namespace perfetto::trace_processor::shell
