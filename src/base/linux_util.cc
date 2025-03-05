// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/390223051): Remove C-library calls to fix the errors.
#pragma allow_unsafe_libc_calls
#endif

#include "base/linux_util.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <iomanip>
#include <memory>
#include <string_view>

#include "base/base_export.h"
#include "base/files/dir_reader_posix.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/strings/safe_sprintf.h"
#include "base/strings/span_printf.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_tokenizer.h"
#include "base/strings/string_util.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"

namespace base {

namespace {

#if !BUILDFLAG(IS_CHROMEOS_ASH)
std::string GetKeyValueFromOSReleaseFile(const std::string& input,
                                         const char* key) {
  StringPairs key_value_pairs;
  SplitStringIntoKeyValuePairs(input, '=', '\n', &key_value_pairs);
  for (const auto& pair : key_value_pairs) {
    const std::string& key_str = pair.first;
    const std::string& value_str = pair.second;
    if (key_str == key) {
      // It can contain quoted characters.
      std::stringstream ss;
      std::string pretty_name;
      ss << value_str;
      // Quoted with a single tick?
      if (value_str[0] == '\'') {
        ss >> std::quoted(pretty_name, '\'');
      } else {
        ss >> std::quoted(pretty_name);
      }

      return pretty_name;
    }
  }

  return "";
}

bool ReadDistroFromOSReleaseFile(const char* file) {
  static const char kPrettyName[] = "PRETTY_NAME";

  std::string os_release_content;
  if (!ReadFileToString(FilePath(file), &os_release_content)) {
    return false;
  }

  std::string pretty_name =
      GetKeyValueFromOSReleaseFile(os_release_content, kPrettyName);
  if (pretty_name.empty()) {
    return false;
  }

  SetLinuxDistro(pretty_name);
  return true;
}

// https://www.freedesktop.org/software/systemd/man/os-release.html
class DistroNameGetter {
 public:
  DistroNameGetter() {
    static const char* const kFilesToCheck[] = {"/etc/os-release",
                                                "/usr/lib/os-release"};
    for (const char* file : kFilesToCheck) {
      if (ReadDistroFromOSReleaseFile(file)) {
        return;
      }
    }
  }
};
#endif  // !BUILDFLAG(IS_CHROMEOS_ASH)

bool GetThreadsFromProcessDir(const char* dir_path, std::vector<pid_t>* tids) {
  DirReaderPosix dir_reader(dir_path);

  if (!dir_reader.IsValid()) {
    DLOG(WARNING) << "Cannot open " << dir_path;
    return false;
  }

  while (dir_reader.Next()) {
    pid_t tid;
    if (StringToInt(dir_reader.name(), &tid)) {
      tids->push_back(tid);
    }
  }

  return true;
}

// Account for the terminating null character.
constexpr int kDistroSize = 128 + 1;

}  // namespace

// We use this static string to hold the Linux distro info. If we
// crash, the crash handler code will send this in the crash dump.
char g_linux_distro[kDistroSize] =
#if BUILDFLAG(IS_CHROMEOS_ASH)
    "CrOS";
#elif BUILDFLAG(IS_ANDROID)
    "Android";
#else
    "Unknown";
#endif

// This function is only supposed to be used in tests. The declaration in the
// header file is guarded by "#if defined(UNIT_TEST)" so that they can be used
// by tests but not non-test code. However, this .cc file is compiled as part
// of "base" where "UNIT_TEST" is not defined. So we need to specify
// "BASE_EXPORT" here again so that they are visible to tests.
BASE_EXPORT std::string GetKeyValueFromOSReleaseFileForTesting(
    const std::string& input,
    const char* key) {
#if !BUILDFLAG(IS_CHROMEOS_ASH)
  return GetKeyValueFromOSReleaseFile(input, key);
#else
  return "";
#endif  // !BUILDFLAG(IS_CHROMEOS_ASH)
}

std::string GetLinuxDistro() {
#if !BUILDFLAG(IS_CHROMEOS_ASH)
  // We do this check only once per process. If it fails, there's
  // little reason to believe it will work if we attempt to run it again.
  static DistroNameGetter distro_name_getter;
#endif
  return g_linux_distro;
}

void SetLinuxDistro(const std::string& distro) {
  std::string trimmed_distro;
  TrimWhitespaceASCII(distro, TRIM_ALL, &trimmed_distro);
  strlcpy(g_linux_distro, trimmed_distro.c_str(), kDistroSize);
}

bool GetThreadsForProcess(pid_t pid, std::vector<pid_t>* tids) {
  // 25 > strlen("/proc//task") + strlen(base::NumberToString(INT_MAX)) + 1 = 22
  char buf[25];
  strings::SafeSPrintf(buf, "/proc/%d/task", pid);
  return GetThreadsFromProcessDir(buf, tids);
}

bool GetThreadsForCurrentProcess(std::vector<pid_t>* tids) {
  return GetThreadsFromProcessDir("/proc/self/task", tids);
}

pid_t FindThreadIDWithSyscall(pid_t pid,
                              const std::string& expected_data,
                              bool* syscall_supported) {
  if (syscall_supported) {
    *syscall_supported = false;
  }

  std::vector<pid_t> tids;
  if (!GetThreadsForProcess(pid, &tids)) {
    return -1;
  }

  std::vector<char> syscall_data(expected_data.size());
  for (pid_t tid : tids) {
    char buf[256];
    base::SpanPrintf(buf, "/proc/%d/task/%d/syscall", pid, tid);
    ScopedFD fd(open(buf, O_RDONLY));
    if (!fd.is_valid()) {
      continue;
    }

    *syscall_supported = true;
    if (!ReadFromFD(fd.get(), syscall_data)) {
      continue;
    }

    if (0 == strncmp(expected_data.c_str(), syscall_data.data(),
                     expected_data.size())) {
      return tid;
    }
  }
  return -1;
}

pid_t FindThreadID(pid_t pid, pid_t ns_tid, bool* ns_pid_supported) {
  *ns_pid_supported = false;

  std::vector<pid_t> tids;
  if (!GetThreadsForProcess(pid, &tids)) {
    return -1;
  }

  for (pid_t tid : tids) {
    char buf[256];
    base::SpanPrintf(buf, "/proc/%d/task/%d/status", pid, tid);
    std::string status;
    if (!ReadFileToString(FilePath(buf), &status)) {
      return -1;
    }
    StringTokenizer tokenizer(status, "\n");
    while (std::optional<std::string_view> token =
               tokenizer.GetNextTokenView()) {
      if (!StartsWith(token.value(), "NSpid")) {
        continue;
      }

      *ns_pid_supported = true;
      std::vector<std::string_view> split_value_str = SplitStringPiece(
          token.value(), "\t", TRIM_WHITESPACE, SPLIT_WANT_NONEMPTY);
      DCHECK_GE(split_value_str.size(), 2u);
      int value;
      // The last value in the list is the PID in the namespace.
      if (StringToInt(split_value_str.back(), &value) && value == ns_tid) {
        // The second value in the list is the real PID.
        if (StringToInt(split_value_str[1], &value)) {
          return value;
        }
      }
      break;
    }
  }
  return -1;
}

}  // namespace base
