/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "src/profiling/common/proc_cmdline.h"

#include <fcntl.h>
#include <fnmatch.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "perfetto/ext/base/file_utils.h"

namespace perfetto {
namespace profiling {
namespace glob_aware {

// Edge cases: the raw cmdline as read out of the kernel can have several
// shapes, the process can rewrite the contents to be arbitrary, and overly long
// cmdlines can be truncated as we use a 511 byte limit. Some examples to
// consider for the implementation:
// * "echo\0hello\0"
// * "/bin/top\0\0\0\0\0\0\0"
// * "arbitrary string as rewritten by the process\0"
// * "some_bugged_kernels_forget_final_nul_terminator"
//
// The approach when performing the read->derive->match is to minimize early
// return codepaths for the caller. So even if we read a non-conforming cmdline
// (e.g. just a single nul byte), it can still be fed through FindBinaryName and
// MatchGlobPattern. It'll just make the intermediate strings be empty (so
// starting with a nul byte, but never nullptr).
//
// NB: bionic/libc/bionic/malloc_heapprofd will require a parallel
// implementation of these functions (to avoid a bionic->perfetto dependency).
// Keep them as STL-free as possible to allow for both implementations to be
// close to verbatim copies.

// TODO(rsavitski): consider changing to std::optional<> return type.
bool ReadProcCmdlineForPID(pid_t pid, std::string* cmdline_out) {
  std::string filename = "/proc/" + std::to_string(pid) + "/cmdline";
  base::ScopedFile fd(base::OpenFile(filename, O_RDONLY));
  if (!fd) {
    PERFETTO_DPLOG("Failed to open %s", filename.c_str());
    return false;
  }

  // buf is 511 bytes to match an implementation that adds a null terminator to
  // the back of a 512 byte buffer.
  char buf[511];
  ssize_t rd = PERFETTO_EINTR(read(*fd, buf, sizeof(buf)));
  if (rd < 0) {
    PERFETTO_DPLOG("Failed to read %s", filename.c_str());
    return false;
  }

  cmdline_out->assign(buf, static_cast<size_t>(rd));
  return true;
}

// Returns a pointer into |cmdline| corresponding to the argv0 without any
// leading directories if the binary path is absolute. |cmdline_len| corresponds
// to the length of the cmdline string as read out of procfs as a C string -
// length doesn't include the final nul terminator, but it must be present at
// cmdline[cmdline_len]. Note that normally the string itself will contain nul
// bytes, as that's what the kernel uses to separate arguments.
//
// Function output examples:
// * /system/bin/adb\0--flag -> adb
// * adb -> adb
// * com.example.app -> com.example.app
const char* FindBinaryName(const char* cmdline, size_t cmdline_len) {
  // Find the first nul byte that signifies the end of argv0. We might not find
  // one if the process rewrote its cmdline without nul separators, and/or the
  // cmdline didn't fully fit into our read buffer. In such cases, proceed with
  // the full string to do best-effort matching.
  const char* argv0_end =
      static_cast<const char*>(memchr(cmdline, '\0', cmdline_len));
  if (argv0_end == nullptr) {
    argv0_end = cmdline + cmdline_len;  // set to final nul terminator
  }
  // Find the last path separator of argv0, if it exists.
  const char* name_start = static_cast<const char*>(
      memrchr(cmdline, '/', static_cast<size_t>(argv0_end - cmdline)));
  if (name_start == nullptr) {
    name_start = cmdline;
  } else {
    name_start++;  // skip the separator
  }
  return name_start;
}

// All inputs must be non-nullptr, but can start with a nul byte.
bool MatchGlobPattern(const char* pattern,
                      const char* cmdline,
                      const char* binname) {
  if (pattern[0] == '/') {
    return fnmatch(pattern, cmdline, FNM_NOESCAPE) == 0;
  }
  return fnmatch(pattern, binname, FNM_NOESCAPE) == 0;
}

}  // namespace glob_aware
}  // namespace profiling
}  // namespace perfetto
