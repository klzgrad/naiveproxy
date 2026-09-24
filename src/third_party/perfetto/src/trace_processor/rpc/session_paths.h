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

#ifndef SRC_TRACE_PROCESSOR_RPC_SESSION_PATHS_H_
#define SRC_TRACE_PROCESSOR_RPC_SESSION_PATHS_H_

#include <cstdint>
#include <string>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_or.h"

// Conventions shared by the trace_processor "warm session" server
// (`tp server unix`) and the `--remote` client, kept in one place so the two
// sides can't drift on how a session name maps to a socket path.
namespace perfetto::trace_processor::session {

// The server writes its pid to <socket-path> + this suffix so that
// `server kill` can stop it by pid without an RPC round-trip.
constexpr char kPidFileSuffix[] = ".pid";

// Maximum length of a session name. Bounded so the assembled AF_UNIX path
// stays within the ~104-108 byte sun_path limit on all platforms.
constexpr size_t kMaxSessionNameLen = 64;

// Returns true if |name| is a valid session name: a non-empty string matching
// ^[A-Za-z0-9][A-Za-z0-9_-]*$ and no longer than kMaxSessionNameLen. Such names
// are safe to embed directly into a filesystem path (no traversal, no escaping)
// and are disjoint from network addresses (which carry ':' or '/').
bool IsValidSessionName(const std::string& name);

// Generates a memorable, lowercase three-word session name (e.g.
// "calm-blue-otter"). Collision-resistant enough that the caller's bind check
// (try-connect, regenerate on a live collision) rarely fires.
std::string GenerateSessionName();

// Returns the directory under which session sockets live, creating it (0700 on
// POSIX) if necessary:
//   Linux:   $XDG_RUNTIME_DIR/perfetto  (fallback: $TMPDIR, then /tmp)
//   macOS:   $TMPDIR/perfetto           (fallback: /tmp)
//   Windows: %LOCALAPPDATA%\perfetto    (fallback: system temp dir)
base::StatusOr<std::string> EnsureSessionDir();

// Resolves a session name to its conventional socket path, creating the parent
// directory. Validates the name and the assembled AF_UNIX path length.
base::StatusOr<std::string> SessionSocketPath(const std::string& name);

// Returns an error if |path| does not fit within the AF_UNIX sun_path limit.
base::Status ValidateAfUnixPathLength(const std::string& path);

// Resolves |path| against the cwd if relative; returns it unchanged if already
// absolute (or empty, or cwd is unavailable). Daemonizing chdirs to "/", so the
// socket/pid paths must be absolute to survive the fork.
std::string MakeAbsolutePath(const std::string& path);

// Parses a human duration into milliseconds. Accepts a bare integer (seconds),
// or a value suffixed with 's' (seconds), 'm' (minutes) or 'h' (hours). "0",
// "never" and "off" all parse to 0 (meaning "no timeout").
base::StatusOr<uint32_t> ParseDurationMs(const std::string& s);

// How a `--remote <addr>` / `server kill <addr>` argument should be
// interpreted.
enum class RemoteAddrKind {
  kHttp,         // host:port or scheme://...  -> HTTP transport.
  kUnixPath,     // absolute path or *.sock    -> Unix socket at that path.
  kSessionName,  // valid session name         -> convention socket path.
};

// Classifies a remote address. Resolution order:
//   1. contains "://" or a trailing ":port"  -> kHttp
//   2. absolute path, or ends in ".sock"     -> kUnixPath
//   3. valid session name                    -> kSessionName
//   4. otherwise                             -> kHttp (let the HTTP path report
//      a useful error; a bare single-label host like "localhost" lands here).
RemoteAddrKind ClassifyRemoteAddr(const std::string& addr);

}  // namespace perfetto::trace_processor::session

#endif  // SRC_TRACE_PROCESSOR_RPC_SESSION_PATHS_H_
