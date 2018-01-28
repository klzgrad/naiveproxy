// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/exec_process.h"

#include <stddef.h>

#include <memory>

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/process/kill.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "build/build_config.h"

#if defined(OS_WIN)
#include <windows.h>

#include "base/win/scoped_handle.h"
#include "base/win/scoped_process_information.h"
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "base/posix/eintr_wrapper.h"
#include "base/posix/file_descriptor_shuffle.h"
#endif

namespace internal {

#if defined(OS_WIN)
bool ExecProcess(const base::CommandLine& cmdline,
                 const base::FilePath& startup_dir,
                 std::string* std_out,
                 std::string* std_err,
                 int* exit_code) {
  SECURITY_ATTRIBUTES sa_attr;
  // Set the bInheritHandle flag so pipe handles are inherited.
  sa_attr.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa_attr.bInheritHandle = TRUE;
  sa_attr.lpSecurityDescriptor = nullptr;

  // Create the pipe for the child process's STDOUT.
  HANDLE out_read = nullptr;
  HANDLE out_write = nullptr;
  if (!CreatePipe(&out_read, &out_write, &sa_attr, 0)) {
    NOTREACHED() << "Failed to create pipe";
    return false;
  }
  base::win::ScopedHandle scoped_out_read(out_read);
  base::win::ScopedHandle scoped_out_write(out_write);

  // Create the pipe for the child process's STDERR.
  HANDLE err_read = nullptr;
  HANDLE err_write = nullptr;
  if (!CreatePipe(&err_read, &err_write, &sa_attr, 0)) {
    NOTREACHED() << "Failed to create pipe";
    return false;
  }
  base::win::ScopedHandle scoped_err_read(err_read);
  base::win::ScopedHandle scoped_err_write(err_write);

  // Ensure the read handle to the pipe for STDOUT/STDERR is not inherited.
  if (!SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0)) {
    NOTREACHED() << "Failed to disabled pipe inheritance";
    return false;
  }
  if (!SetHandleInformation(err_read, HANDLE_FLAG_INHERIT, 0)) {
    NOTREACHED() << "Failed to disabled pipe inheritance";
    return false;
  }

  base::FilePath::StringType cmdline_str(cmdline.GetCommandLineString());

  STARTUPINFO start_info = {};

  start_info.cb = sizeof(STARTUPINFO);
  start_info.hStdOutput = out_write;
  // Keep the normal stdin.
  start_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  // FIXME(brettw) set stderr here when we actually read it below.
  //start_info.hStdError = err_write;
  start_info.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  start_info.dwFlags |= STARTF_USESTDHANDLES;

  // Create the child process.
  PROCESS_INFORMATION temp_process_info = {};
  if (!CreateProcess(nullptr,
                     &cmdline_str[0],
                     nullptr, nullptr,
                     TRUE,  // Handles are inherited.
                     0, nullptr,
                     startup_dir.value().c_str(),
                     &start_info, &temp_process_info)) {
    return false;
  }
  base::win::ScopedProcessInformation proc_info(temp_process_info);

  // Close our writing end of pipes now. Otherwise later read would not be able
  // to detect end of child's output.
  scoped_out_write.Close();
  scoped_err_write.Close();

  // Read output from the child process's pipe for STDOUT
  const int kBufferSize = 1024;
  char buffer[kBufferSize];

  // FIXME(brettw) read from stderr here! This is complicated because we want
  // to read both of them at the same time, probably need overlapped I/O.
  // Also uncomment start_info code above.
  for (;;) {
    DWORD bytes_read = 0;
    BOOL success =
        ReadFile(out_read, buffer, kBufferSize, &bytes_read, nullptr);
    if (!success || bytes_read == 0)
      break;
    std_out->append(buffer, bytes_read);
  }

  // Let's wait for the process to finish.
  WaitForSingleObject(proc_info.process_handle(), INFINITE);

  DWORD dw_exit_code;
  GetExitCodeProcess(proc_info.process_handle(), &dw_exit_code);
  *exit_code = static_cast<int>(dw_exit_code);

  return true;
}
#else
// Reads from the provided file descriptor and appends to output. Returns false
// if the fd is closed or there is an unexpected error (not
// EINTR/EAGAIN/EWOULDBLOCK).
bool ReadFromPipe(int fd, std::string* output) {
  char buffer[256];
  int bytes_read = HANDLE_EINTR(read(fd, buffer, sizeof(buffer)));
  if (bytes_read == -1) {
    return errno == EAGAIN || errno == EWOULDBLOCK;
  } else if (bytes_read <= 0) {
    return false;
  }
  output->append(buffer, bytes_read);
  return true;
}

bool ExecProcess(const base::CommandLine& cmdline,
                 const base::FilePath& startup_dir,
                 std::string* std_out,
                 std::string* std_err,
                 int* exit_code) {
  *exit_code = EXIT_FAILURE;

  std::vector<std::string> argv = cmdline.argv();

  int out_fd[2], err_fd[2];
  pid_t pid;
  base::InjectiveMultimap fd_shuffle1, fd_shuffle2;
  std::unique_ptr<char* []> argv_cstr(new char*[argv.size() + 1]);

  fd_shuffle1.reserve(3);
  fd_shuffle2.reserve(3);

  if (pipe(out_fd) < 0)
    return false;
  base::ScopedFD out_read(out_fd[0]), out_write(out_fd[1]);

  if (pipe(err_fd) < 0)
    return false;
  base::ScopedFD err_read(err_fd[0]), err_write(err_fd[1]);

  if (out_read.get() >= FD_SETSIZE || err_read.get() >= FD_SETSIZE)
    return false;

  switch (pid = fork()) {
    case -1:  // error
      return false;
    case 0:  // child
      {
        // DANGER: no calls to malloc are allowed from now on:
        // http://crbug.com/36678
        //
        // STL iterators are also not allowed (including those implied
        // by range-based for loops), since debug iterators use locks.

        // Obscure fork() rule: in the child, if you don't end up doing exec*(),
        // you call _exit() instead of exit(). This is because _exit() does not
        // call any previously-registered (in the parent) exit handlers, which
        // might do things like block waiting for threads that don't even exist
        // in the child.
        int dev_null = open("/dev/null", O_WRONLY);
        if (dev_null < 0)
          _exit(127);

        fd_shuffle1.push_back(
            base::InjectionArc(out_write.get(), STDOUT_FILENO, true));
        fd_shuffle1.push_back(
            base::InjectionArc(err_write.get(), STDERR_FILENO, true));
        fd_shuffle1.push_back(
            base::InjectionArc(dev_null, STDIN_FILENO, true));
        // Adding another element here? Remeber to increase the argument to
        // reserve(), above.

        // DANGER: Do NOT convert to range-based for loop!
        for (size_t i = 0; i < fd_shuffle1.size(); ++i)
          fd_shuffle2.push_back(fd_shuffle1[i]);

        if (!ShuffleFileDescriptors(&fd_shuffle1))
          _exit(127);

        base::SetCurrentDirectory(startup_dir);

        // TODO(brettw) the base version GetAppOutput does a
        // CloseSuperfluousFds call here. Do we need this?

        // DANGER: Do NOT convert to range-based for loop!
        for (size_t i = 0; i < argv.size(); i++)
          argv_cstr[i] = const_cast<char*>(argv[i].c_str());
        argv_cstr[argv.size()] = nullptr;
        execvp(argv_cstr[0], argv_cstr.get());
        _exit(127);
      }
    default:  // parent
      {
        // Close our writing end of pipe now. Otherwise later read would not
        // be able to detect end of child's output (in theory we could still
        // write to the pipe).
        out_write.reset();
        err_write.reset();

        bool out_open = true, err_open = true;
        while (out_open || err_open) {
          fd_set read_fds;
          FD_ZERO(&read_fds);
          FD_SET(out_read.get(), &read_fds);
          FD_SET(err_read.get(), &read_fds);
          int res =
              HANDLE_EINTR(select(std::max(out_read.get(), err_read.get()) + 1,
                                  &read_fds, nullptr, nullptr, nullptr));
          if (res <= 0)
            break;
          if (FD_ISSET(out_read.get(), &read_fds))
            out_open = ReadFromPipe(out_read.get(), std_out);
          if (FD_ISSET(err_read.get(), &read_fds))
            err_open = ReadFromPipe(err_read.get(), std_err);
        }

        base::Process process(pid);
        return process.WaitForExit(exit_code);
      }
  }

  return false;
}
#endif

}  // namespace internal

