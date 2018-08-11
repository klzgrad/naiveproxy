// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/launch.h"

#include <fdio/limits.h>
#include <fdio/namespace.h>
#include <fdio/util.h>
#include <launchpad/launchpad.h>
#include <stdint.h>
#include <unistd.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/fuchsia/default_job.h"
#include "base/fuchsia/fuchsia_logging.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/scoped_generic.h"

namespace base {

namespace {

bool GetAppOutputInternal(const CommandLine& cmd_line,
                          bool include_stderr,
                          std::string* output,
                          int* exit_code) {
  DCHECK(exit_code);

  LaunchOptions options;

  // LaunchProcess will automatically clone any stdio fd we do not explicitly
  // map.
  int pipe_fd[2];
  if (pipe(pipe_fd) < 0)
    return false;
  options.fds_to_remap.emplace_back(pipe_fd[1], STDOUT_FILENO);
  if (include_stderr)
    options.fds_to_remap.emplace_back(pipe_fd[1], STDERR_FILENO);

  Process process = LaunchProcess(cmd_line, options);
  close(pipe_fd[1]);
  if (!process.IsValid()) {
    close(pipe_fd[0]);
    return false;
  }

  output->clear();
  for (;;) {
    char buffer[256];
    ssize_t bytes_read = read(pipe_fd[0], buffer, sizeof(buffer));
    if (bytes_read <= 0)
      break;
    output->append(buffer, bytes_read);
  }
  close(pipe_fd[0]);

  return process.WaitForExit(exit_code);
}

bool MapPathsToLaunchpad(const std::vector<FilePath>& paths_to_map,
                         launchpad_t* lp) {
  zx_status_t status;

  // Build a array of null terminated strings, which which will be used as an
  // argument for launchpad_set_nametable().
  std::vector<const char*> paths_c_str;
  paths_c_str.reserve(paths_to_map.size());

  for (size_t paths_idx = 0; paths_idx < paths_to_map.size(); ++paths_idx) {
    const FilePath& next_path = paths_to_map[paths_idx];
    if (!PathExists(next_path)) {
      DLOG(ERROR) << "Path does not exist: " << next_path;
      return false;
    }

    File dir(next_path, File::FLAG_OPEN | File::FLAG_READ);
    ScopedPlatformFile scoped_fd(dir.TakePlatformFile());
    zx_handle_t handles[FDIO_MAX_HANDLES] = {};
    uint32_t types[FDIO_MAX_HANDLES] = {};
    zx_status_t num_handles =
        fdio_transfer_fd(scoped_fd.get(), 0, handles, types);
    // fdio_transfer_fd() returns number of transferred handles, or negative
    // error.
    if (num_handles <= 0) {
      DCHECK_LT(num_handles, 0);
      ZX_LOG(ERROR, num_handles) << "fdio_transfer_fd";
      return false;
    }
    ScopedZxHandle scoped_handle(handles[0]);
    ignore_result(scoped_fd.release());

    // Close the handles that we won't use.
    for (int i = 1; i < num_handles; ++i) {
      zx_handle_close(handles[i]);
    }

    if (types[0] != PA_FDIO_REMOTE) {
      LOG(ERROR) << "Handle type for " << next_path.AsUTF8Unsafe()
                 << " is not PA_FDIO_REMOTE: " << types[0];
      return false;
    }

    // Add the handle to the child's nametable.
    // We use the macro PA_HND(..., <index>) to relate the handle to its
    // position in the nametable, which is stored as an array of path strings
    // |paths_str|.
    status = launchpad_add_handle(lp, scoped_handle.release(),
                                  PA_HND(PA_NS_DIR, paths_idx));
    if (status != ZX_OK) {
      ZX_LOG(ERROR, status) << "launchpad_add_handle";
      return false;
    }
    paths_c_str.push_back(next_path.value().c_str());
  }

  if (!paths_c_str.empty()) {
    status =
        launchpad_set_nametable(lp, paths_c_str.size(), paths_c_str.data());
    if (status != ZX_OK) {
      ZX_LOG(ERROR, status) << "launchpad_set_nametable";
      return false;
    }
  }

  return true;
}

struct LaunchpadScopedTraits {
  static launchpad_t* InvalidValue() { return nullptr; }

  static void Free(launchpad_t* lp) { launchpad_destroy(lp); }
};

using ScopedLaunchpad = ScopedGeneric<launchpad_t*, LaunchpadScopedTraits>;

}  // namespace

Process LaunchProcess(const CommandLine& cmdline,
                      const LaunchOptions& options) {
  return LaunchProcess(cmdline.argv(), options);
}

// TODO(768416): Investigate whether we can make LaunchProcess() create
// unprivileged processes by default (no implicit capabilities are granted).
Process LaunchProcess(const std::vector<std::string>& argv,
                      const LaunchOptions& options) {
  std::vector<const char*> argv_cstr;
  argv_cstr.reserve(argv.size() + 1);
  for (const auto& arg : argv)
    argv_cstr.push_back(arg.c_str());
  argv_cstr.push_back(nullptr);

  // Note that per launchpad.h, the intention is that launchpad_ functions are
  // used in a "builder" style. From launchpad_create() to launchpad_go() the
  // status is tracked in the launchpad_t object, and launchpad_go() reports on
  // the final status, and cleans up |lp| (assuming it was even created).
  zx_handle_t job = options.job_handle != ZX_HANDLE_INVALID ? options.job_handle
                                                            : GetDefaultJob();
  DCHECK_NE(ZX_HANDLE_INVALID, job);
  ScopedLaunchpad lp;
  zx_status_t status;
  if ((status = launchpad_create(job, argv_cstr[0], lp.receive())) != ZX_OK) {
    ZX_LOG(ERROR, status) << "launchpad_create(job)";
    return Process();
  }

  if ((status = launchpad_load_from_file(lp.get(), argv_cstr[0])) != ZX_OK) {
    ZX_LOG(ERROR, status) << "launchpad_load_from_file(" << argv_cstr[0] << ")";
    return Process();
  }

  if ((status = launchpad_set_args(lp.get(), argv.size(), argv_cstr.data())) !=
      ZX_OK) {
    ZX_LOG(ERROR, status) << "launchpad_set_args";
    return Process();
  }

  uint32_t to_clone = options.clone_flags;

  std::unique_ptr<char* []> new_environ;
  char* const empty_environ = nullptr;
  char* const* old_environ = environ;
  if (options.clear_environ)
    old_environ = &empty_environ;

  EnvironmentMap environ_modifications = options.environ;
  if (!options.current_directory.empty()) {
    environ_modifications["PWD"] = options.current_directory.value();
  } else {
    FilePath cwd;
    GetCurrentDirectory(&cwd);
    environ_modifications["PWD"] = cwd.value();
  }

  if (to_clone & LP_CLONE_DEFAULT_JOB) {
    // Override Fuchsia's built in default job cloning behavior with our own
    // logic which uses |job| instead of zx_job_default().
    // This logic is based on the launchpad implementation.
    zx_handle_t job_duplicate = ZX_HANDLE_INVALID;
    if ((status = zx_handle_duplicate(job, ZX_RIGHT_SAME_RIGHTS,
                                      &job_duplicate)) != ZX_OK) {
      ZX_LOG(ERROR, status) << "zx_handle_duplicate";
      return Process();
    }
    launchpad_add_handle(lp.get(), job_duplicate, PA_HND(PA_JOB_DEFAULT, 0));
    to_clone &= ~LP_CLONE_DEFAULT_JOB;
  }

  if (!environ_modifications.empty())
    new_environ = AlterEnvironment(old_environ, environ_modifications);

  if (!environ_modifications.empty() || options.clear_environ)
    launchpad_set_environ(lp.get(), new_environ.get());
  else
    to_clone |= LP_CLONE_ENVIRON;

  if (!options.paths_to_map.empty()) {
    DCHECK(!(to_clone & LP_CLONE_FDIO_NAMESPACE));
    if (!MapPathsToLaunchpad(options.paths_to_map, lp.get())) {
      return Process();
    }
  }

  launchpad_clone(lp.get(), to_clone);

  // Clone the mapped file-descriptors, plus any of the stdio descriptors
  // which were not explicitly specified.
  bool stdio_already_mapped[3] = {false};
  for (const auto& src_target : options.fds_to_remap) {
    if (static_cast<size_t>(src_target.second) <
        arraysize(stdio_already_mapped)) {
      stdio_already_mapped[src_target.second] = true;
    }
    launchpad_clone_fd(lp.get(), src_target.first, src_target.second);
  }
  if (to_clone & LP_CLONE_FDIO_STDIO) {
    for (size_t stdio_fd = 0; stdio_fd < arraysize(stdio_already_mapped);
         ++stdio_fd) {
      if (!stdio_already_mapped[stdio_fd])
        launchpad_clone_fd(lp.get(), stdio_fd, stdio_fd);
    }
    to_clone &= ~LP_CLONE_FDIO_STDIO;
  }

  for (const auto& id_and_handle : options.handles_to_transfer) {
    launchpad_add_handle(lp.get(), id_and_handle.handle, id_and_handle.id);
  }

  zx_handle_t process_handle;
  const char* errmsg;
  if ((status = launchpad_go(lp.get(), &process_handle, &errmsg)) != ZX_OK) {
    ZX_LOG(ERROR, status) << "launchpad_go failed: " << errmsg;
    return Process();
  }
  ignore_result(lp.release());  // launchpad_go() took ownership.

  Process process(process_handle);
  if (options.wait) {
    status = zx_object_wait_one(process.Handle(), ZX_TASK_TERMINATED,
                                ZX_TIME_INFINITE, nullptr);
    DCHECK(status == ZX_OK)
        << "zx_object_wait_one: " << zx_status_get_string(status);
  }

  return process;
}

bool GetAppOutput(const CommandLine& cl, std::string* output) {
  int exit_code;
  bool result = GetAppOutputInternal(cl, false, output, &exit_code);
  return result && exit_code == EXIT_SUCCESS;
}

bool GetAppOutput(const std::vector<std::string>& argv, std::string* output) {
  return GetAppOutput(CommandLine(argv), output);
}

bool GetAppOutputAndError(const CommandLine& cl, std::string* output) {
  int exit_code;
  bool result = GetAppOutputInternal(cl, true, output, &exit_code);
  return result && exit_code == EXIT_SUCCESS;
}

bool GetAppOutputAndError(const std::vector<std::string>& argv,
                          std::string* output) {
  return GetAppOutputAndError(CommandLine(argv), output);
}

bool GetAppOutputWithExitCode(const CommandLine& cl,
                              std::string* output,
                              int* exit_code) {
  // Contrary to GetAppOutput(), |true| return here means that the process was
  // launched and the exit code was waited upon successfully, but not
  // necessarily that the exit code was EXIT_SUCCESS.
  return GetAppOutputInternal(cl, false, output, exit_code);
}

void RaiseProcessToHighPriority() {
  // Fuchsia doesn't provide an API to change process priority.
}

}  // namespace base
