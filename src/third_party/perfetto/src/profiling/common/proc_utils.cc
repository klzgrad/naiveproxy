/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "src/profiling/common/proc_utils.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cinttypes>
#include <optional>

#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/string_utils.h"
#include "src/profiling/common/proc_cmdline.h"

namespace perfetto {
namespace profiling {
namespace {

std::optional<uint32_t> ParseProcStatusSize(const std::string& status,
                                            const std::string& key) {
  auto entry_idx = status.find(key);
  if (entry_idx == std::string::npos)
    return {};
  entry_idx = status.find_first_not_of(" \t", entry_idx + key.size());
  if (entry_idx == std::string::npos)
    return {};
  int32_t val = atoi(status.c_str() + entry_idx);
  if (val < 0) {
    PERFETTO_ELOG("Unexpected value reading %s", key.c_str());
    return {};
  }
  return static_cast<uint32_t>(val);
}
}  // namespace

std::optional<std::string> ReadStatus(pid_t pid) {
  std::string path = "/proc/" + std::to_string(pid) + "/status";
  std::string status;
  bool read_proc = base::ReadFile(path, &status);
  if (!read_proc) {
    PERFETTO_ELOG("Failed to read %s", path.c_str());
    return std::nullopt;
  }
  return std::optional<std::string>(status);
}

std::optional<uint32_t> GetRssAnonAndSwap(const std::string& status) {
  auto anon_rss = ParseProcStatusSize(status, "RssAnon:");
  auto swap = ParseProcStatusSize(status, "VmSwap:");
  if (anon_rss.has_value() && swap.has_value()) {
    return *anon_rss + *swap;
  }
  return std::nullopt;
}

void RemoveUnderAnonThreshold(uint32_t min_size_kb, std::set<pid_t>* pids) {
  for (auto it = pids->begin(); it != pids->end();) {
    const pid_t pid = *it;

    std::optional<std::string> status = ReadStatus(pid);
    std::optional<uint32_t> rss_and_swap;
    if (status)
      rss_and_swap = GetRssAnonAndSwap(*status);

    if (rss_and_swap && rss_and_swap < min_size_kb) {
      PERFETTO_LOG("Removing pid %d from profiled set (anon: %u kB < %u)", pid,
                   *rss_and_swap, min_size_kb);
      it = pids->erase(it);
    } else {
      ++it;
    }
  }
}

std::optional<Uids> GetUids(const std::string& status) {
  auto entry_idx = status.find("Uid:");
  if (entry_idx == std::string::npos)
    return std::nullopt;

  Uids uids;
  const char* str = &status[entry_idx + 4];
  char* endptr;

  uids.real = strtoull(str, &endptr, 10);
  if (*endptr != ' ' && *endptr != '\t')
    return std::nullopt;

  str = endptr;
  uids.effective = strtoull(str, &endptr, 10);
  if (*endptr != ' ' && *endptr != '\t')
    return std::nullopt;

  str = endptr;
  uids.saved_set = strtoull(str, &endptr, 10);
  if (*endptr != ' ' && *endptr != '\t')
    return std::nullopt;

  str = endptr;
  uids.filesystem = strtoull(str, &endptr, 10);
  if (*endptr != '\n' && *endptr != '\0')
    return std::nullopt;
  return uids;
}

// Normalize cmdline in place. Stores new beginning of string in *cmdline_ptr.
// Returns new size of string (from new beginning).
// Modifies string in *cmdline_ptr.
ssize_t NormalizeCmdLine(char** cmdline_ptr, size_t size) {
  char* cmdline = *cmdline_ptr;
  char* first_arg = static_cast<char*>(memchr(cmdline, '\0', size));
  if (first_arg == nullptr) {
    errno = EOVERFLOW;
    return -1;
  }
  // For consistency with what we do with Java app cmdlines, trim everything
  // after the @ sign of the first arg.
  char* first_at = static_cast<char*>(memchr(cmdline, '@', size));
  if (first_at != nullptr && first_at < first_arg) {
    *first_at = '\0';
    first_arg = first_at;
  }
  char* start = static_cast<char*>(
      memrchr(cmdline, '/', static_cast<size_t>(first_arg - cmdline)));
  if (start == nullptr) {
    start = cmdline;
  } else {
    // Skip the /.
    start++;
  }
  *cmdline_ptr = start;
  return first_arg - start;
}

std::optional<std::vector<std::string>> NormalizeCmdlines(
    const std::vector<std::string>& cmdlines) {
  std::vector<std::string> normalized_cmdlines;
  normalized_cmdlines.reserve(cmdlines.size());

  for (size_t i = 0; i < cmdlines.size(); i++) {
    std::string cmdline = cmdlines[i];  // mutable copy
    // Add nullbyte to make sure it's a C string.
    cmdline.resize(cmdline.size() + 1, '\0');
    char* cmdline_cstr = &(cmdline[0]);
    ssize_t size = NormalizeCmdLine(&cmdline_cstr, cmdline.size());
    if (size == -1) {
      PERFETTO_PLOG("Failed to normalize cmdline %s. Stopping the parse.",
                    cmdlines[i].c_str());
      return std::nullopt;
    }
    normalized_cmdlines.emplace_back(cmdline_cstr, static_cast<size_t>(size));
  }
  return std::make_optional(normalized_cmdlines);
}

// This is mostly the same as GetHeapprofdProgramProperty in
// https://android.googlesource.com/platform/bionic/+/main/libc/bionic/malloc_common.cpp
// This should give the same result as GetHeapprofdProgramProperty.
bool GetCmdlineForPID(pid_t pid, std::string* name) {
  std::string filename = "/proc/" + std::to_string(pid) + "/cmdline";
  base::ScopedFile fd(base::OpenFile(filename, O_RDONLY | O_CLOEXEC));
  if (!fd) {
    PERFETTO_DPLOG("Failed to open %s", filename.c_str());
    return false;
  }
  char cmdline[512];
  const size_t max_read_size = sizeof(cmdline) - 1;
  ssize_t rd = read(*fd, cmdline, max_read_size);
  if (rd == -1) {
    PERFETTO_DPLOG("Failed to read %s", filename.c_str());
    return false;
  }

  if (rd == 0) {
    PERFETTO_DLOG("Empty cmdline for %" PRIdMAX ". Skipping.",
                  static_cast<intmax_t>(pid));
    return false;
  }

  // In some buggy kernels (before http://bit.ly/37R7qwL) /proc/pid/cmdline is
  // not NUL-terminated (see b/147438623). If we read < max_read_size bytes
  // assume we are hitting the aforementioned kernel bug and terminate anyways.
  const size_t rd_u = static_cast<size_t>(rd);
  if (rd_u >= max_read_size && memchr(cmdline, '\0', rd_u) == nullptr) {
    // We did not manage to read the first argument.
    PERFETTO_DLOG("Overflow reading cmdline for %" PRIdMAX,
                  static_cast<intmax_t>(pid));
    errno = EOVERFLOW;
    return false;
  }

  cmdline[rd] = '\0';
  char* cmdline_start = cmdline;
  ssize_t size = NormalizeCmdLine(&cmdline_start, rd_u);
  if (size == -1)
    return false;
  name->assign(cmdline_start, static_cast<size_t>(size));
  return true;
}

void FindAllProfilablePids(std::set<pid_t>* pids) {
  ForEachPid([pids](pid_t pid) {
    if (pid == getpid())
      return;

    char filename_buf[128];
    snprintf(filename_buf, sizeof(filename_buf), "/proc/%d/%s", pid, "cmdline");
    struct stat statbuf;
    // Check if we have permission to the process.
    if (stat(filename_buf, &statbuf) == 0)
      pids->emplace(pid);
  });
}

void FindPidsForCmdlines(const std::vector<std::string>& cmdlines,
                         std::set<pid_t>* pids) {
  ForEachPid([&cmdlines, pids](pid_t pid) {
    if (pid == getpid())
      return;
    std::string process_cmdline;
    process_cmdline.reserve(512);
    GetCmdlineForPID(pid, &process_cmdline);
    for (const std::string& cmdline : cmdlines) {
      if (process_cmdline == cmdline)
        pids->emplace(static_cast<pid_t>(pid));
    }
  });
}

namespace glob_aware {
void FindPidsForCmdlinePatterns(const std::vector<std::string>& patterns,
                                std::set<pid_t>* pids) {
  ForEachPid([&patterns, pids](pid_t pid) {
    if (pid == getpid())
      return;
    std::string cmdline;
    if (!glob_aware::ReadProcCmdlineForPID(pid, &cmdline))
      return;
    const char* binname =
        glob_aware::FindBinaryName(cmdline.c_str(), cmdline.size());

    for (const std::string& pattern : patterns) {
      if (glob_aware::MatchGlobPattern(pattern.c_str(), cmdline.c_str(),
                                       binname)) {
        pids->insert(pid);
      }
    }
  });
}
}  // namespace glob_aware

}  // namespace profiling
}  // namespace perfetto
