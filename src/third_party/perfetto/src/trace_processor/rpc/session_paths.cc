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

#include "src/trace_processor/rpc/session_paths.h"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>

#include "perfetto/base/build_config.h"
#include "perfetto/base/proc_utils.h"
#include "perfetto/base/time.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <direct.h>  // _getcwd()
#else
#include <unistd.h>  // getcwd()
#endif

#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/murmur_hash.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/temp_file.h"
#include "perfetto/ext/base/utils.h"

namespace perfetto::trace_processor::session {
namespace {

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
constexpr char kPathSep = '\\';
#else
constexpr char kPathSep = '/';
#endif

// Conservative AF_UNIX sun_path limit (108 on Linux, 104 on macOS/BSD). We use
// the smallest so a path that validates here binds on every platform.
constexpr size_t kMaxAfUnixPath = 104;

// Curated, lowercase word banks for auto-generated names. Three banks of 16
// give 4096 combinations; live collisions are resolved by the caller's bind
// check, so this only needs to make them rare and memorable.
constexpr const char* kAdjectives[] = {
    "calm",  "brave", "lucky", "happy", "swift", "quiet", "bold",  "eager",
    "merry", "kind",  "neat",  "proud", "witty", "warm",  "shiny", "gentle"};
constexpr const char* kColors[] = {
    "amber", "azure", "blue", "coral", "green", "ivory", "jade", "lilac",
    "olive", "peach", "plum", "ruby",  "rust",  "teal",  "gold", "violet"};
constexpr const char* kAnimals[] = {
    "otter",  "falcon", "lynx",  "panda", "tapir", "heron", "ibex", "koala",
    "marten", "quokka", "raven", "shrew", "stork", "viper", "wren", "yak"};

// Returns true if the substring after the last ':' is non-empty and all digits
// (i.e. a "host:port" address).
bool HasTrailingPort(const std::string& addr) {
  size_t colon = addr.rfind(':');
  if (colon == std::string::npos || colon + 1 == addr.size())
    return false;
  for (size_t i = colon + 1; i < addr.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(addr[i])))
      return false;
  }
  return true;
}

}  // namespace

bool IsValidSessionName(const std::string& name) {
  if (name.empty() || name.size() > kMaxSessionNameLen)
    return false;
  if (!std::isalnum(static_cast<unsigned char>(name[0])))
    return false;
  for (char c : name) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-')
      return false;
  }
  return true;
}

std::string GenerateSessionName() {
  uint64_t seed = static_cast<uint64_t>(base::GetWallTimeNs().count()) ^
                  (static_cast<uint64_t>(base::GetProcessId()) << 32);
  const char* adj = kAdjectives[base::MurmurHashCombine(seed, 0) %
                                base::ArraySize(kAdjectives)];
  const char* col =
      kColors[base::MurmurHashCombine(seed, 1) % base::ArraySize(kColors)];
  const char* ani =
      kAnimals[base::MurmurHashCombine(seed, 2) % base::ArraySize(kAnimals)];
  return std::string(adj) + "-" + col + "-" + ani;
}

base::StatusOr<std::string> EnsureSessionDir() {
  std::string base_dir;
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  if (const char* local = getenv("LOCALAPPDATA"); local && *local) {
    base_dir = local;
  } else {
    base_dir = base::GetSysTempDir();
  }
#elif PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
  base_dir = base::GetSysTempDir();  // $TMPDIR (per-user) or /tmp.
#else
  if (const char* xdg = getenv("XDG_RUNTIME_DIR"); xdg && *xdg) {
    base_dir = xdg;
  } else {
    base_dir = base::GetSysTempDir();
  }
#endif
  if (base_dir.empty())
    return base::ErrStatus("Could not determine a base directory for sessions");

  std::string dir = base_dir + kPathSep + "perfetto";
  // 0700: the socket exposes the loaded trace, so keep the directory private
  // (mode is ignored on Windows).
  if (!base::Mkdir(dir, 0700) && !base::FileExists(dir))
    return base::ErrStatus("Failed to create session directory %s",
                           dir.c_str());
  return dir;
}

base::StatusOr<std::string> SessionSocketPath(const std::string& name) {
  if (!IsValidSessionName(name)) {
    return base::ErrStatus(
        "Invalid session name '%s' (must match [A-Za-z0-9][A-Za-z0-9_-]* and "
        "be "
        "at most %zu chars)",
        name.c_str(), kMaxSessionNameLen);
  }
  ASSIGN_OR_RETURN(std::string dir, EnsureSessionDir());
  std::string path = dir + kPathSep + name + ".sock";
  RETURN_IF_ERROR(ValidateAfUnixPathLength(path));
  return path;
}

base::Status ValidateAfUnixPathLength(const std::string& path) {
  if (path.size() + 1 > kMaxAfUnixPath) {
    return base::ErrStatus(
        "Socket path is too long (%zu bytes, limit %zu): %s. Use --path with a "
        "shorter directory.",
        path.size(), kMaxAfUnixPath - 1, path.c_str());
  }
  return base::OkStatus();
}

std::string MakeAbsolutePath(const std::string& path) {
  if (path.empty() || base::IsAbsolutePath(path))
    return path;
  char cwd[4096];
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  if (!_getcwd(cwd, sizeof(cwd)))
    return path;
#else
  if (!getcwd(cwd, sizeof(cwd)))
    return path;
#endif
  return std::string(cwd) + kPathSep + path;
}

base::StatusOr<uint32_t> ParseDurationMs(const std::string& s) {
  if (s.empty())
    return base::ErrStatus("Empty duration");
  if (s == "never" || s == "off" || s == "0")
    return 0u;

  uint64_t mult_ms = 1000;  // Default unit is seconds.
  std::string num = s;
  char suffix = s.back();
  if (suffix == 's' || suffix == 'm' || suffix == 'h') {
    num = s.substr(0, s.size() - 1);
    mult_ms = suffix == 's' ? 1000 : (suffix == 'm' ? 60 * 1000 : 3600 * 1000);
  }
  std::optional<int64_t> value = base::StringToInt64(num);
  if (!value.has_value() || *value < 0) {
    return base::ErrStatus(
        "Invalid duration '%s' (expected e.g. 30s, 5m, 2h, or 'never')",
        s.c_str());
  }
  return static_cast<uint32_t>(static_cast<uint64_t>(*value) * mult_ms);
}

RemoteAddrKind ClassifyRemoteAddr(const std::string& addr) {
  if (base::EndsWith(addr, ".sock") || base::IsAbsolutePath(addr))
    return RemoteAddrKind::kUnixPath;
  if (addr.find("://") != std::string::npos || HasTrailingPort(addr))
    return RemoteAddrKind::kHttp;
  if (IsValidSessionName(addr))
    return RemoteAddrKind::kSessionName;
  return RemoteAddrKind::kHttp;
}

}  // namespace perfetto::trace_processor::session
