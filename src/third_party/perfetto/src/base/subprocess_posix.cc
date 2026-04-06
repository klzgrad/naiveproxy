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

#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) ||   \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_FREEBSD) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <thread>
#include <tuple>

#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX_BUT_NOT_QNX) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
#include <sys/prctl.h>
#endif

#include "perfetto/base/logging.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/utils.h"

// In MacOS this is not defined in any header.
extern "C" char** environ;

namespace perfetto {
namespace base {

namespace {

struct ChildProcessArgs {
  Subprocess::Args* create_args;
  const char* exec_cmd = nullptr;
  std::vector<char*> argv;
  std::vector<char*> env;
  int stdin_pipe_rd = -1;
  int stdouterr_pipe_wr = -1;
};

// Don't add any dynamic allocation in this function. This will be invoked
// under a fork(), potentially in a state where the allocator lock is held.
void __attribute__((noreturn)) ChildProcess(ChildProcessArgs* args) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX_BUT_NOT_QNX) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  // In no case we want a child process to outlive its parent process. This is
  // relevant for tests, so that a test failure/crash doesn't leave child
  // processes around that get reparented to init.
  prctl(PR_SET_PDEATHSIG, SIGKILL);
#endif

  auto die = [args](const char* err) __attribute__((noreturn)) {
    base::ignore_result(write(args->stdouterr_pipe_wr, err, strlen(err)));
    base::ignore_result(write(args->stdouterr_pipe_wr, "\n", 1));
    // From https://www.gnu.org/software/libc/manual/html_node/Exit-Status.html
    // "In particular, the value 128 is used to indicate failure to execute
    // another program in a subprocess. This convention is not universally
    // obeyed, but it is a good idea to follow it in your programs."
    _exit(128);
  };

  if (args->create_args->posix_proc_group_id.has_value()) {
    if (setpgid(0 /*self*/, args->create_args->posix_proc_group_id.value())) {
      die("setpgid() failed");
    }
  }

  auto set_fd_close_on_exec = [&die](int fd, bool close_on_exec) {
    int flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0)
      die("fcntl(F_GETFD) failed");
    flags = close_on_exec ? (flags | FD_CLOEXEC) : (flags & ~FD_CLOEXEC);
    if (fcntl(fd, F_SETFD, flags) < 0)
      die("fcntl(F_SETFD) failed");
  };

  if (getppid() == 1)
    die("terminating because parent process died");

  switch (args->create_args->stdin_mode) {
    case Subprocess::InputMode::kBuffer:
      if (dup2(args->stdin_pipe_rd, STDIN_FILENO) == -1)
        die("Failed to dup2(STDIN)");
      close(args->stdin_pipe_rd);
      break;
    case Subprocess::InputMode::kDevNull:
      if (dup2(open("/dev/null", O_RDONLY), STDIN_FILENO) == -1)
        die("Failed to dup2(STDOUT)");
      break;
  }

  switch (args->create_args->stdout_mode) {
    case Subprocess::OutputMode::kInherit:
      break;
    case Subprocess::OutputMode::kDevNull: {
      if (dup2(open("/dev/null", O_RDWR), STDOUT_FILENO) == -1)
        die("Failed to dup2(STDOUT)");
      break;
    }
    case Subprocess::OutputMode::kBuffer:
      if (dup2(args->stdouterr_pipe_wr, STDOUT_FILENO) == -1)
        die("Failed to dup2(STDOUT)");
      break;
    case Subprocess::OutputMode::kFd:
      if (dup2(*args->create_args->out_fd, STDOUT_FILENO) == -1)
        die("Failed to dup2(STDOUT)");
      break;
  }

  switch (args->create_args->stderr_mode) {
    case Subprocess::OutputMode::kInherit:
      break;
    case Subprocess::OutputMode::kDevNull: {
      if (dup2(open("/dev/null", O_RDWR), STDERR_FILENO) == -1)
        die("Failed to dup2(STDERR)");
      break;
    }
    case Subprocess::OutputMode::kBuffer:
      if (dup2(args->stdouterr_pipe_wr, STDERR_FILENO) == -1)
        die("Failed to dup2(STDERR)");
      break;
    case Subprocess::OutputMode::kFd:
      if (dup2(*args->create_args->out_fd, STDERR_FILENO) == -1)
        die("Failed to dup2(STDERR)");
      break;
  }

  // Close all FDs % stdin/out/err and the ones that the client explicitly
  // asked to retain. The reason for this is twofold:
  // 1. For exec-only (i.e. entrypoint == empty) cases: it avoids leaking FDs
  //    that didn't get marked as O_CLOEXEC by accident.
  // 2. In fork() mode (entrypoint not empty) avoids retaining a dup of eventfds
  //    that would prevent the parent process to receive EOFs (tests usually use
  //    pipes as a synchronization mechanism between subprocesses).
  const auto& preserve_fds = args->create_args->preserve_fds;
  for (int i = 0; i < 512; i++) {
    if (i != STDIN_FILENO && i != STDERR_FILENO && i != STDOUT_FILENO &&
        i != args->stdouterr_pipe_wr &&
        !std::count(preserve_fds.begin(), preserve_fds.end(), i)) {
      close(i);
    }
  }

  // Clears O_CLOEXEC from stdin/out/err and the |preserve_fds| list. These are
  // the only FDs that we want to be preserved after the exec().
  set_fd_close_on_exec(STDIN_FILENO, false);
  set_fd_close_on_exec(STDOUT_FILENO, false);
  set_fd_close_on_exec(STDERR_FILENO, false);

  for (auto fd : preserve_fds)
    set_fd_close_on_exec(fd, false);

  // If the caller specified a std::function entrypoint, run that first.
  if (args->create_args->posix_entrypoint_for_testing)
    args->create_args->posix_entrypoint_for_testing();

  // If the caller specified only an entrypoint, without any args, exit now.
  // Otherwise proceed with the exec() below.
  if (!args->exec_cmd)
    _exit(0);

  // If |args[0]| is a path use execv() (which takes a path), otherwise use
  // exevp(), which uses the shell and follows PATH.
  if (strchr(args->exec_cmd, '/')) {
    char** env = args->env.empty() ? environ : args->env.data();
    execve(args->exec_cmd, args->argv.data(), env);
  } else {
    // There is no execvpe() on Mac.
    if (!args->env.empty())
      die("A full path is required for |exec_cmd| when setting |env|");
    execvp(args->exec_cmd, args->argv.data());
  }

  // Reached only if execv fails.
  die("execve() failed");
}

}  // namespace

// static
const int Subprocess::kTimeoutSignal = SIGKILL;

void Subprocess::Start() {
  ChildProcessArgs proc_args;
  proc_args.create_args = &args;

  // Setup argv.
  if (!args.exec_cmd.empty()) {
    proc_args.exec_cmd = args.exec_cmd[0].c_str();
    for (const std::string& arg : args.exec_cmd)
      proc_args.argv.push_back(const_cast<char*>(arg.c_str()));
    proc_args.argv.push_back(nullptr);

    if (!args.posix_argv0_override_for_testing.empty()) {
      proc_args.argv[0] =
          const_cast<char*>(args.posix_argv0_override_for_testing.c_str());
    }
  }

  // Setup env.
  if (!args.env.empty()) {
    for (const std::string& str : args.env)
      proc_args.env.push_back(const_cast<char*>(str.c_str()));
    proc_args.env.push_back(nullptr);
  }

  // Setup the pipes for stdin/err redirection.
  if (args.stdin_mode == InputMode::kBuffer) {
    s_->stdin_pipe = base::Pipe::Create(base::Pipe::kWrNonBlock);
    proc_args.stdin_pipe_rd = *s_->stdin_pipe.rd;
  }
  s_->stdouterr_pipe = base::Pipe::Create(base::Pipe::kRdNonBlock);
  proc_args.stdouterr_pipe_wr = *s_->stdouterr_pipe.wr;

  // Spawn the child process that will exec().
  s_->pid = fork();
  PERFETTO_CHECK(s_->pid >= 0);
  if (s_->pid == 0) {
    // Close the parent-ends of the pipes.
    s_->stdin_pipe.wr.reset();
    s_->stdouterr_pipe.rd.reset();
    ChildProcess(&proc_args);
    // ChildProcess() doesn't return, not even in case of failures.
    PERFETTO_FATAL("not reached");
  }

  s_->status = kRunning;

  // Close the child-end of the pipes.
  // Deliberately NOT closing the s_->stdin_pipe.rd. This is to avoid crashing
  // with a SIGPIPE if the process exits without consuming its stdin, while
  // the parent tries to write() on the other end of the stdin pipe.
  s_->stdouterr_pipe.wr.reset();
  proc_args.create_args->out_fd.reset();

  // Spawn a thread that is blocked on waitpid() and writes the termination
  // status onto a pipe. The problem here is that waipid() doesn't have a
  // timeout option and can't be passed to poll(). The alternative would be
  // using a SIGCHLD handler, but anecdotally signal handlers introduce more
  // problems than what they solve.
  s_->exit_status_pipe = base::Pipe::Create(base::Pipe::kRdNonBlock);

  // Both ends of the pipe are closed after the thread.join().
  int pid = s_->pid;
  int exit_status_pipe_wr = s_->exit_status_pipe.wr.release();
  auto* rusage = s_->rusage.get();
  s_->waitpid_thread = std::thread([pid, exit_status_pipe_wr, rusage] {
    int pid_stat = -1;
    struct rusage usg{};
    int wait_res = PERFETTO_EINTR(wait4(pid, &pid_stat, 0, &usg));
    PERFETTO_CHECK(wait_res == pid);

    auto tv_to_ms = [](const struct timeval& tv) {
      return static_cast<uint32_t>(tv.tv_sec * 1000 + tv.tv_usec / 1000);
    };
    rusage->cpu_utime_ms = tv_to_ms(usg.ru_utime);
    rusage->cpu_stime_ms = tv_to_ms(usg.ru_stime);
    rusage->max_rss_kb = static_cast<uint32_t>(usg.ru_maxrss) / 1000;
    rusage->min_page_faults = static_cast<uint32_t>(usg.ru_minflt);
    rusage->maj_page_faults = static_cast<uint32_t>(usg.ru_majflt);
    rusage->vol_ctx_switch = static_cast<uint32_t>(usg.ru_nvcsw);
    rusage->invol_ctx_switch = static_cast<uint32_t>(usg.ru_nivcsw);

    base::ignore_result(PERFETTO_EINTR(
        write(exit_status_pipe_wr, &pid_stat, sizeof(pid_stat))));
    PERFETTO_CHECK(close(exit_status_pipe_wr) == 0 || errno == EINTR);
  });
}

Subprocess::Status Subprocess::Poll() {
  if (s_->status != kRunning)
    return s_->status;  // Nothing to poll.
  while (PollInternal(0 /* don't block*/)) {
  }
  return s_->status;
}

// |timeout_ms| semantic:
//   -1: Block indefinitely.
//    0: Don't block, return immediately.
//   >0: Block for at most X ms.
// Returns:
//  True: Read at least one fd (so there might be more queued).
//  False: if all fds reached quiescent (no data to read/write).
bool Subprocess::PollInternal(int poll_timeout_ms) {
  struct pollfd fds[3]{};
  size_t num_fds = 0;
  if (s_->exit_status_pipe.rd) {
    fds[num_fds].fd = *s_->exit_status_pipe.rd;
    fds[num_fds].events = POLLIN;
    num_fds++;
  }
  if (s_->stdouterr_pipe.rd) {
    fds[num_fds].fd = *s_->stdouterr_pipe.rd;
    fds[num_fds].events = POLLIN;
    num_fds++;
  }
  if (s_->stdin_pipe.wr) {
    fds[num_fds].fd = *s_->stdin_pipe.wr;
    fds[num_fds].events = POLLOUT;
    num_fds++;
  }

  if (num_fds == 0)
    return false;

  auto nfds = static_cast<nfds_t>(num_fds);
  int poll_res = PERFETTO_EINTR(poll(fds, nfds, poll_timeout_ms));
  PERFETTO_CHECK(poll_res >= 0);

  TryReadStdoutAndErr();
  TryPushStdin();
  TryReadExitStatus();

  return poll_res > 0;
}

bool Subprocess::Wait(int timeout_ms) {
  PERFETTO_CHECK(s_->status != kNotStarted);

  // Break out of the loop only after both conditions are satisfied:
  // - All stdout/stderr data has been read (if kBuffer).
  // - The process exited.
  // Note that the two events can happen arbitrary order. After the process
  // exits, there might be still data in the pipe buffer, which we want to
  // read fully.
  //
  // Instead, don't wait on the stdin to be fully written. The child process
  // might exit prematurely (or crash). If that happens, we can end up in a
  // state where the write(stdin_pipe_.wr) will never unblock.

  const int64_t t_start = base::GetWallTimeMs().count();
  while (s_->exit_status_pipe.rd || s_->stdouterr_pipe.rd) {
    int poll_timeout_ms = -1;  // Block until a FD is ready.
    if (timeout_ms > 0) {
      const int64_t now = GetWallTimeMs().count();
      poll_timeout_ms = timeout_ms - static_cast<int>(now - t_start);
      if (poll_timeout_ms <= 0)
        return false;
    }
    PollInternal(poll_timeout_ms);
  }  // while(...)
  return true;
}

void Subprocess::TryReadExitStatus() {
  if (!s_->exit_status_pipe.rd)
    return;

  int pid_stat = -1;
  int64_t rsize = PERFETTO_EINTR(
      read(*s_->exit_status_pipe.rd, &pid_stat, sizeof(pid_stat)));
  if (rsize < 0 && errno == EAGAIN)
    return;

  if (rsize > 0) {
    PERFETTO_CHECK(rsize == sizeof(pid_stat));
  } else if (rsize < 0) {
    PERFETTO_PLOG("Subprocess read(s_->exit_status_pipe) failed");
  }
  s_->waitpid_thread.join();
  s_->exit_status_pipe.rd.reset();

  s_->status = kTerminated;
  if (WIFEXITED(pid_stat)) {
    s_->returncode = WEXITSTATUS(pid_stat);
  } else if (WIFSIGNALED(pid_stat)) {
    s_->returncode = 128 + WTERMSIG(pid_stat);  // Follow bash convention.
  } else {
    PERFETTO_FATAL("waitpid() returned an unexpected value (%d)", pid_stat);
  }
}

// If the stidn pipe is still open, push input data and close it at the end.
void Subprocess::TryPushStdin() {
  if (!s_->stdin_pipe.wr)
    return;

  PERFETTO_DCHECK(args.input.empty() || s_->input_written < args.input.size());
  if (!args.input.empty()) {
    int64_t wsize =
        PERFETTO_EINTR(write(*s_->stdin_pipe.wr, &args.input[s_->input_written],
                             args.input.size() - s_->input_written));
    if (wsize < 0 && errno == EAGAIN)
      return;

    if (wsize >= 0) {
      // Whether write() can return 0 is one of the greatest mysteries of UNIX.
      // Just ignore it.
      s_->input_written += static_cast<size_t>(wsize);
    } else {
      PERFETTO_PLOG("Subprocess write(stdin) failed");
      s_->stdin_pipe.wr.reset();
    }
  }
  PERFETTO_DCHECK(s_->input_written <= args.input.size());
  if (s_->input_written == args.input.size())
    s_->stdin_pipe.wr.reset();  // Close stdin.
}

void Subprocess::TryReadStdoutAndErr() {
  if (!s_->stdouterr_pipe.rd)
    return;
  char buf[4096];
  int64_t rsize =
      PERFETTO_EINTR(read(*s_->stdouterr_pipe.rd, buf, sizeof(buf)));
  if (rsize < 0 && errno == EAGAIN)
    return;

  if (rsize > 0) {
    s_->output.append(buf, static_cast<size_t>(rsize));
  } else if (rsize == 0 /* EOF */) {
    s_->stdouterr_pipe.rd.reset();
  } else {
    PERFETTO_PLOG("Subprocess read(stdout/err) failed");
    s_->stdouterr_pipe.rd.reset();
  }
}

void Subprocess::KillAndWaitForTermination(int sig_num) {
  kill(s_->pid, sig_num ? sig_num : SIGKILL);
  Wait();
  // TryReadExitStatus must have joined the thread.
  PERFETTO_DCHECK(!s_->waitpid_thread.joinable());
}

}  // namespace base
}  // namespace perfetto

#endif  // PERFETTO_OS_LINUX || PERFETTO_OS_ANDROID || PERFETTO_OS_APPLE
