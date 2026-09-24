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

#ifndef SRC_TRACE_PROCESSOR_RPC_UNIXD_H_
#define SRC_TRACE_PROCESSOR_RPC_UNIXD_H_

#include <cstdint>
#include <string>

#include "perfetto/base/status.h"
#include "src/trace_processor/rpc/session_lifecycle.h"

namespace perfetto::trace_processor {

class Rpc;

struct UnixServerArgs {
  // Absolute path of the AF_UNIX socket to bind.
  std::string socket_path;
  // Human-facing session name, printed in the startup record.
  std::string session_name;
  // Idle-timeout before the server reaps itself; 0 disables reaping.
  uint32_t idle_timeout_ms = 0;
  // When the idle clock applies (see IdleStart).
  IdleStart idle_start = IdleStart::kAuto;
  // If true, detach into the background (POSIX only) before serving.
  bool daemonize = false;
};

// Runs an RPC server over an AF_UNIX socket at |args.socket_path|, serving the
// (already trace-loaded) |rpc|. Cleans up a stale socket left by a dead server,
// prints a one-line startup record to stdout, and blocks serving requests until
// the process is interrupted (SIGINT/SIGTERM on POSIX), unlinking the socket on
// the way out. Returns an error if the socket cannot be bound (e.g. a live
// server already holds the path).
base::Status RunUnixRpcServer(Rpc& rpc, const UnixServerArgs& args);

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_RPC_UNIXD_H_
