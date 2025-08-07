
/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "src/profiling/symbolizer/subprocess.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)

#include <sstream>
#include <string>

#include <Windows.h>

#include "perfetto/base/logging.h"

namespace perfetto {
namespace profiling {

Subprocess::Subprocess(const std::string& file, std::vector<std::string> args) {
  std::stringstream cmd;
  cmd << file;
  for (auto arg : args) {
    cmd << " " << arg;
  }
  SECURITY_ATTRIBUTES attr;
  attr.nLength = sizeof(SECURITY_ATTRIBUTES);
  attr.bInheritHandle = true;
  attr.lpSecurityDescriptor = nullptr;
  // Create a pipe for the child process's STDOUT.
  if (!CreatePipe(&child_pipe_out_read_, &child_pipe_out_write_, &attr, 0) ||
      !SetHandleInformation(child_pipe_out_read_, HANDLE_FLAG_INHERIT, 0)) {
    PERFETTO_ELOG("Failed to create stdout pipe");
    return;
  }
  if (!CreatePipe(&child_pipe_in_read_, &child_pipe_in_write_, &attr, 0) ||
      !SetHandleInformation(child_pipe_in_write_, HANDLE_FLAG_INHERIT, 0)) {
    PERFETTO_ELOG("Failed to create stdin pipe");
    return;
  }

  PROCESS_INFORMATION proc_info;
  STARTUPINFOA start_info;
  bool success = false;
  // Set up members of the PROCESS_INFORMATION structure.
  ZeroMemory(&proc_info, sizeof(PROCESS_INFORMATION));

  // Set up members of the STARTUPINFO structure.
  // This structure specifies the STDIN and STDOUT handles for redirection.
  ZeroMemory(&start_info, sizeof(STARTUPINFOA));
  start_info.cb = sizeof(STARTUPINFOA);
  start_info.hStdError = child_pipe_out_write_;
  start_info.hStdOutput = child_pipe_out_write_;
  start_info.hStdInput = child_pipe_in_read_;
  start_info.dwFlags |= STARTF_USESTDHANDLES;

  // Create the child process.
  success = CreateProcessA(nullptr,
                           &(cmd.str()[0]),  // command line
                           nullptr,          // process security attributes
                           nullptr,      // primary thread security attributes
                           TRUE,         // handles are inherited
                           0,            // creation flags
                           nullptr,      // use parent's environment
                           nullptr,      // use parent's current directory
                           &start_info,  // STARTUPINFO pointer
                           &proc_info);  // receives PROCESS_INFORMATION

  // If an error occurs, exit the application.
  if (success) {
    CloseHandle(proc_info.hProcess);
    CloseHandle(proc_info.hThread);

    // Close handles to the stdin and stdout pipes no longer needed by the child
    // process. If they are not explicitly closed, there is no way to recognize
    // that the child process has ended.

    CloseHandle(child_pipe_out_write_);
    CloseHandle(child_pipe_in_read_);
  } else {
    PERFETTO_ELOG("Failed to launch: %s", cmd.str().c_str());
    child_pipe_in_read_ = nullptr;
    child_pipe_in_write_ = nullptr;
    child_pipe_out_write_ = nullptr;
    child_pipe_out_read_ = nullptr;
  }
}

Subprocess::~Subprocess() {
  CloseHandle(child_pipe_out_read_);
  CloseHandle(child_pipe_in_write_);
}

int64_t Subprocess::Write(const char* buffer, size_t size) {
  if (child_pipe_in_write_ == nullptr) {
    return -1;
  }
  DWORD bytes_written;
  if (WriteFile(child_pipe_in_write_, buffer, static_cast<DWORD>(size),
                &bytes_written, nullptr)) {
    return static_cast<int64_t>(bytes_written);
  }
  return -1;
}

int64_t Subprocess::Read(char* buffer, size_t size) {
  if (child_pipe_out_read_ == nullptr) {
    return -1;
  }
  DWORD bytes_read;
  if (ReadFile(child_pipe_out_read_, buffer, static_cast<DWORD>(size),
               &bytes_read, nullptr)) {
    return static_cast<int64_t>(bytes_read);
  }
  return -1;
}

}  // namespace profiling
}  // namespace perfetto

#endif  // PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
