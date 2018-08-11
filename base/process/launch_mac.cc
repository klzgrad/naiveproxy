// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/launch.h"

#include <crt_externs.h>
#include <mach/mach.h>
#include <spawn.h>
#include <string.h>
#include <sys/wait.h>

#include "base/logging.h"
#include "base/posix/eintr_wrapper.h"
#include "base/threading/thread_restrictions.h"

namespace base {

namespace {

// DPSXCHECK is a Debug Posix Spawn Check macro. The posix_spawn* family of
// functions return an errno value, as opposed to setting errno directly. This
// macro emulates a DPCHECK().
#define DPSXCHECK(expr)                                              \
  do {                                                               \
    int rv = (expr);                                                 \
    DCHECK_EQ(rv, 0) << #expr << ": -" << rv << " " << strerror(rv); \
  } while (0)

class PosixSpawnAttr {
 public:
  PosixSpawnAttr() { DPSXCHECK(posix_spawnattr_init(&attr_)); }

  ~PosixSpawnAttr() { DPSXCHECK(posix_spawnattr_destroy(&attr_)); }

  posix_spawnattr_t* get() { return &attr_; }

 private:
  posix_spawnattr_t attr_;
};

class PosixSpawnFileActions {
 public:
  PosixSpawnFileActions() {
    DPSXCHECK(posix_spawn_file_actions_init(&file_actions_));
  }

  ~PosixSpawnFileActions() {
    DPSXCHECK(posix_spawn_file_actions_destroy(&file_actions_));
  }

  void Open(int filedes, const char* path, int mode) {
    DPSXCHECK(posix_spawn_file_actions_addopen(&file_actions_, filedes, path,
                                               mode, 0));
  }

  void Dup2(int filedes, int newfiledes) {
    DPSXCHECK(
        posix_spawn_file_actions_adddup2(&file_actions_, filedes, newfiledes));
  }

  void Inherit(int filedes) {
    DPSXCHECK(posix_spawn_file_actions_addinherit_np(&file_actions_, filedes));
  }

  const posix_spawn_file_actions_t* get() const { return &file_actions_; }

 private:
  posix_spawn_file_actions_t file_actions_;

  DISALLOW_COPY_AND_ASSIGN(PosixSpawnFileActions);
};

}  // namespace

void RestoreDefaultExceptionHandler() {
  // This function is tailored to remove the Breakpad exception handler.
  // exception_mask matches s_exception_mask in
  // third_party/breakpad/breakpad/src/client/mac/handler/exception_handler.cc
  const exception_mask_t exception_mask = EXC_MASK_BAD_ACCESS |
                                          EXC_MASK_BAD_INSTRUCTION |
                                          EXC_MASK_ARITHMETIC |
                                          EXC_MASK_BREAKPOINT;

  // Setting the exception port to MACH_PORT_NULL may not be entirely
  // kosher to restore the default exception handler, but in practice,
  // it results in the exception port being set to Apple Crash Reporter,
  // the desired behavior.
  task_set_exception_ports(mach_task_self(), exception_mask, MACH_PORT_NULL,
                           EXCEPTION_DEFAULT, THREAD_STATE_NONE);
}

Process LaunchProcessPosixSpawn(const std::vector<std::string>& argv,
                                const LaunchOptions& options) {
  DCHECK(!options.pre_exec_delegate)
      << "LaunchProcessPosixSpawn does not support PreExecDelegate";
  DCHECK(options.current_directory.empty())
      << "LaunchProcessPosixSpawn does not support current_directory";

  PosixSpawnAttr attr;

  short flags = POSIX_SPAWN_CLOEXEC_DEFAULT;
  if (options.new_process_group) {
    flags |= POSIX_SPAWN_SETPGROUP;
    DPSXCHECK(posix_spawnattr_setpgroup(attr.get(), 0));
  }
  DPSXCHECK(posix_spawnattr_setflags(attr.get(), flags));

  PosixSpawnFileActions file_actions;

  // Process file descriptors for the child. By default, LaunchProcess will
  // open stdin to /dev/null and inherit stdout and stderr.
  bool inherit_stdout = true, inherit_stderr = true;
  bool null_stdin = true;
  for (const auto& dup2_pair : options.fds_to_remap) {
    if (dup2_pair.second == STDIN_FILENO) {
      null_stdin = false;
    } else if (dup2_pair.second == STDOUT_FILENO) {
      inherit_stdout = false;
    } else if (dup2_pair.second == STDERR_FILENO) {
      inherit_stderr = false;
    }

    if (dup2_pair.first == dup2_pair.second) {
      file_actions.Inherit(dup2_pair.second);
    } else {
      file_actions.Dup2(dup2_pair.first, dup2_pair.second);
    }
  }

  if (null_stdin) {
    file_actions.Open(STDIN_FILENO, "/dev/null", O_RDONLY);
  }
  if (inherit_stdout) {
    file_actions.Inherit(STDOUT_FILENO);
  }
  if (inherit_stderr) {
    file_actions.Inherit(STDERR_FILENO);
  }

  std::vector<char*> argv_cstr;
  argv_cstr.reserve(argv.size() + 1);
  for (const auto& arg : argv)
    argv_cstr.push_back(const_cast<char*>(arg.c_str()));
  argv_cstr.push_back(nullptr);

  std::unique_ptr<char* []> owned_environ;
  char** new_environ = options.clear_environ ? nullptr : *_NSGetEnviron();
  if (!options.environ.empty()) {
    owned_environ = AlterEnvironment(new_environ, options.environ);
    new_environ = owned_environ.get();
  }

  const char* executable_path = !options.real_path.empty()
                                    ? options.real_path.value().c_str()
                                    : argv_cstr[0];

  // Use posix_spawnp as some callers expect to have PATH consulted.
  pid_t pid;
  int rv = posix_spawnp(&pid, executable_path, file_actions.get(), attr.get(),
                        &argv_cstr[0], new_environ);

  if (rv != 0) {
    DLOG(ERROR) << "posix_spawnp(" << executable_path << "): -" << rv << " "
                << strerror(rv);
    return Process();
  }

  if (options.wait) {
    // While this isn't strictly disk IO, waiting for another process to
    // finish is the sort of thing ThreadRestrictions is trying to prevent.
    base::AssertBlockingAllowed();
    pid_t ret = HANDLE_EINTR(waitpid(pid, nullptr, 0));
    DPCHECK(ret > 0);
  }

  return Process(pid);
}

}  // namespace base
