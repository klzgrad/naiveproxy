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

#include "perfetto/ext/base/subprocess.h"

#include "perfetto/base/build_config.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)

#include <stdio.h>

#include <algorithm>
#include <mutex>
#include <tuple>

#include <Windows.h>

#include "perfetto/base/logging.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/pipe.h"
#include "perfetto/ext/base/utils.h"

namespace perfetto {
namespace base {

// static
const int Subprocess::kTimeoutSignal = static_cast<int>(STATUS_TIMEOUT);

void Subprocess::Start() {
  if (args.exec_cmd.empty()) {
    PERFETTO_ELOG("Subprocess.exec_cmd cannot be empty on Windows");
    return;
  }

  // Quote arguments but only when ambiguous. When quoting, CreateProcess()
  // assumes that the command is an absolute path and does not search in the
  // %PATH%. If non quoted, instead, CreateProcess() tries both. This is to
  // allow Subprocess("cmd.exe", "/c", "shell command").
  std::string cmd;
  for (const auto& part : args.exec_cmd) {
    if (part.find(" ") != std::string::npos) {
      cmd += "\"" + part + "\" ";
    } else {
      cmd += part + " ";
    }
  }
  // Remove trailing space.
  if (!cmd.empty())
    cmd.resize(cmd.size() - 1);

  if (args.stdin_mode == InputMode::kBuffer) {
    s_->stdin_pipe = Pipe::Create();
    // Allow the child process to inherit the other end of the pipe.
    PERFETTO_CHECK(
        ::SetHandleInformation(*s_->stdin_pipe.rd, HANDLE_FLAG_INHERIT, 1));
  }

  if (args.stderr_mode == OutputMode::kBuffer ||
      args.stdout_mode == OutputMode::kBuffer) {
    s_->stdouterr_pipe = Pipe::Create();
    PERFETTO_CHECK(
        ::SetHandleInformation(*s_->stdouterr_pipe.wr, HANDLE_FLAG_INHERIT, 1));
  }

  ScopedPlatformHandle nul_handle;
  if (args.stderr_mode == OutputMode::kDevNull ||
      args.stdout_mode == OutputMode::kDevNull) {
    nul_handle.reset(::CreateFileA(
        "NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    PERFETTO_CHECK(::SetHandleInformation(*nul_handle, HANDLE_FLAG_INHERIT, 1));
  }

  PROCESS_INFORMATION proc_info{};
  STARTUPINFOA start_info{};
  start_info.cb = sizeof(STARTUPINFOA);

  if (args.stderr_mode == OutputMode::kInherit) {
    start_info.hStdError = ::GetStdHandle(STD_ERROR_HANDLE);
  } else if (args.stderr_mode == OutputMode::kBuffer) {
    start_info.hStdError = *s_->stdouterr_pipe.wr;
  } else if (args.stderr_mode == OutputMode::kDevNull) {
    start_info.hStdError = *nul_handle;
  } else if (args.stderr_mode == OutputMode::kFd) {
    PERFETTO_CHECK(
        ::SetHandleInformation(*args.out_fd, HANDLE_FLAG_INHERIT, 1));
    start_info.hStdError = *args.out_fd;
  } else {
    PERFETTO_CHECK(false);
  }

  if (args.stdout_mode == OutputMode::kInherit) {
    start_info.hStdOutput = ::GetStdHandle(STD_OUTPUT_HANDLE);
  } else if (args.stdout_mode == OutputMode::kBuffer) {
    start_info.hStdOutput = *s_->stdouterr_pipe.wr;
  } else if (args.stdout_mode == OutputMode::kDevNull) {
    start_info.hStdOutput = *nul_handle;
  } else if (args.stdout_mode == OutputMode::kFd) {
    PERFETTO_CHECK(
        ::SetHandleInformation(*args.out_fd, HANDLE_FLAG_INHERIT, 1));
    start_info.hStdOutput = *args.out_fd;
  } else {
    PERFETTO_CHECK(false);
  }

  if (args.stdin_mode == InputMode::kBuffer) {
    start_info.hStdInput = *s_->stdin_pipe.rd;
  } else if (args.stdin_mode == InputMode::kDevNull) {
    start_info.hStdInput = *nul_handle;
  }

  start_info.dwFlags |= STARTF_USESTDHANDLES;

  // Create the child process.
  bool success =
      ::CreateProcessA(nullptr,      // App name. Needs to be null to use PATH.
                       &cmd[0],      // Command line.
                       nullptr,      // Process security attributes.
                       nullptr,      // Primary thread security attributes.
                       true,         // Handles are inherited.
                       0,            // Flags.
                       nullptr,      // Use parent's environment.
                       nullptr,      // Use parent's current directory.
                       &start_info,  // STARTUPINFO pointer.
                       &proc_info);  // Receives PROCESS_INFORMATION.

  // Close on our side the pipe ends that we passed to the child process.
  s_->stdin_pipe.rd.reset();
  s_->stdouterr_pipe.wr.reset();
  args.out_fd.reset();

  if (!success) {
    s_->returncode = ERROR_FILE_NOT_FOUND;
    s_->status = kTerminated;
    s_->stdin_pipe.wr.reset();
    s_->stdouterr_pipe.rd.reset();
    PERFETTO_ELOG("CreateProcess failed: %lx, cmd: %s", GetLastError(),
                  &cmd[0]);
    return;
  }

  s_->pid = proc_info.dwProcessId;
  s_->win_proc_handle = ScopedPlatformHandle(proc_info.hProcess);
  s_->win_thread_handle = ScopedPlatformHandle(proc_info.hThread);
  s_->status = kRunning;

  MovableState* s = s_.get();
  if (args.stdin_mode == InputMode::kBuffer) {
    s_->stdin_thread = std::thread(&Subprocess::StdinThread, s, args.input);
  }

  if (args.stderr_mode == OutputMode::kBuffer ||
      args.stdout_mode == OutputMode::kBuffer) {
    PERFETTO_DCHECK(s_->stdouterr_pipe.rd);
    s_->stdouterr_thread = std::thread(&Subprocess::StdoutErrThread, s);
  }
}

// static
void Subprocess::StdinThread(MovableState* s, std::string input) {
  size_t input_written = 0;
  while (input_written < input.size()) {
    DWORD wsize = 0;
    if (::WriteFile(*s->stdin_pipe.wr, input.data() + input_written,
                    static_cast<DWORD>(input.size() - input_written), &wsize,
                    nullptr)) {
      input_written += wsize;
    } else {
      // ERROR_BROKEN_PIPE is WAI when the child just closes stdin and stops
      // accepting input.
      auto err = ::GetLastError();
      if (err != ERROR_BROKEN_PIPE)
        PERFETTO_PLOG("Subprocess WriteFile(stdin) failed %lx", err);
      break;
    }
  }  // while(...)
  std::unique_lock<std::mutex> lock(s->mutex);
  s->stdin_pipe.wr.reset();
}

// static
void Subprocess::StdoutErrThread(MovableState* s) {
  char buf[4096];
  for (;;) {
    DWORD rsize = 0;
    bool res =
        ::ReadFile(*s->stdouterr_pipe.rd, buf, sizeof(buf), &rsize, nullptr);
    if (!res) {
      auto err = GetLastError();
      if (err != ERROR_BROKEN_PIPE)
        PERFETTO_PLOG("Subprocess ReadFile(stdouterr) failed %ld", err);
    }

    if (rsize > 0) {
      std::unique_lock<std::mutex> lock(s->mutex);
      s->locked_outerr_buf.append(buf, static_cast<size_t>(rsize));
    } else {  // EOF or some error.
      break;
    }
  }  // For(..)

  // Close the stdouterr_pipe. The main loop looks at the pipe closure to
  // determine whether the stdout/err thread has completed.
  {
    std::unique_lock<std::mutex> lock(s->mutex);
    s->stdouterr_pipe.rd.reset();
  }
  s->stdouterr_done_event.Notify();
}

Subprocess::Status Subprocess::Poll() {
  if (s_->status != kRunning)
    return s_->status;  // Nothing to poll.
  Wait(1 /*ms*/);
  return s_->status;
}

bool Subprocess::Wait(int timeout_ms) {
  PERFETTO_CHECK(s_->status != kNotStarted);
  const bool wait_forever = timeout_ms == 0;
  const int64_t wait_start_ms = base::GetWallTimeMs().count();

  // Break out of the loop only after both conditions are satisfied:
  // - All stdout/stderr data has been read (if OutputMode::kBuffer).
  // - The process exited.
  // Note that the two events can happen arbitrary order. After the process
  // exits, there might be still data in the pipe buffer, which we want to
  // read fully.
  // Note also that stdout/err might be "complete" before starting, if neither
  // is operating in OutputMode::kBuffer mode. In that case we just want to wait
  // for the process termination.
  //
  // Instead, don't wait on the stdin to be fully written. The child process
  // might exit prematurely (or crash). If that happens, we can end up in a
  // state where the write(stdin_pipe_.wr) will never unblock.
  bool stdouterr_complete = false;
  for (;;) {
    HANDLE wait_handles[2]{};
    DWORD num_handles = 0;

    // Check if the process exited.
    bool process_exited = !s_->win_proc_handle;
    if (!process_exited) {
      DWORD exit_code = STILL_ACTIVE;
      PERFETTO_CHECK(::GetExitCodeProcess(*s_->win_proc_handle, &exit_code));
      if (exit_code != STILL_ACTIVE) {
        s_->returncode = static_cast<int>(exit_code);
        s_->status = kTerminated;
        s_->win_proc_handle.reset();
        s_->win_thread_handle.reset();
        process_exited = true;
      }
    } else {
      PERFETTO_DCHECK(s_->status != kRunning);
    }
    if (!process_exited) {
      wait_handles[num_handles++] = *s_->win_proc_handle;
    }

    // Check if there is more output and if the stdout/err pipe has been closed.
    {
      std::unique_lock<std::mutex> lock(s_->mutex);
      // Move the output from the internal buffer shared with the
      // stdouterr_thread to the final buffer exposed to the client.
      if (!s_->locked_outerr_buf.empty()) {
        s_->output.append(std::move(s_->locked_outerr_buf));
        s_->locked_outerr_buf.clear();
      }
      stdouterr_complete = !s_->stdouterr_pipe.rd;
      if (!stdouterr_complete) {
        wait_handles[num_handles++] = s_->stdouterr_done_event.fd();
      }
    }  // lock(s_->mutex)

    if (num_handles == 0) {
      PERFETTO_DCHECK(process_exited && stdouterr_complete);
      break;
    }

    DWORD wait_ms;  // Note: DWORD is unsigned.
    if (wait_forever) {
      wait_ms = INFINITE;
    } else {
      const int64_t now = GetWallTimeMs().count();
      const int64_t wait_left_ms = timeout_ms - (now - wait_start_ms);
      if (wait_left_ms <= 0)
        return false;  // Timed out
      wait_ms = static_cast<DWORD>(wait_left_ms);
    }

    auto wait_res =
        ::WaitForMultipleObjects(num_handles, wait_handles, false, wait_ms);
    PERFETTO_CHECK(wait_res != WAIT_FAILED);
  }

  PERFETTO_DCHECK(!s_->win_proc_handle);
  PERFETTO_DCHECK(!s_->win_thread_handle);

  if (s_->stdin_thread.joinable())  // Might not exist if CreateProcess failed.
    s_->stdin_thread.join();
  if (s_->stdouterr_thread.joinable())
    s_->stdouterr_thread.join();

  // The stdin pipe is closed by the dedicated stdin thread. However if that is
  // not started (e.g. because of no redirection) force close it now. Needs to
  // happen after the join() to be thread safe.
  s_->stdin_pipe.wr.reset();
  s_->stdouterr_pipe.rd.reset();

  return true;
}

void Subprocess::KillAndWaitForTermination(int exit_code) {
  auto code = exit_code ? static_cast<DWORD>(exit_code) : STATUS_CONTROL_C_EXIT;
  ::TerminateProcess(*s_->win_proc_handle, code);
  Wait();
  // TryReadExitStatus must have joined the threads.
  PERFETTO_DCHECK(!s_->stdin_thread.joinable());
  PERFETTO_DCHECK(!s_->stdouterr_thread.joinable());
}

}  // namespace base
}  // namespace perfetto

#endif  // PERFETTO_OS_WIN
