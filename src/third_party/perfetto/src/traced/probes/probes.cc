/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/getopt.h"
#include "perfetto/ext/base/lock_free_task_runner.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/ext/base/version.h"
#include "perfetto/ext/traced/traced.h"
#include "perfetto/tracing/default_socket.h"

#include "src/traced/probes/ftrace/tracefs.h"
#include "src/traced/probes/probes_producer.h"

namespace perfetto {

int PERFETTO_EXPORT_ENTRYPOINT ProbesMain(int argc, char** argv) {
  enum LongOption {
    OPT_CLEANUP_AFTER_CRASH = 1000,
    OPT_VERSION,
    OPT_BACKGROUND,
    OPT_RESET_FTRACE,
  };

  bool background = false;
  bool reset_ftrace = false;

  static const option long_options[] = {
      {"background", no_argument, nullptr, OPT_BACKGROUND},
      {"cleanup-after-crash", no_argument, nullptr, OPT_CLEANUP_AFTER_CRASH},
      {"reset-ftrace", no_argument, nullptr, OPT_RESET_FTRACE},
      {"version", no_argument, nullptr, OPT_VERSION},
      {nullptr, 0, nullptr, 0}};

  for (;;) {
    int option = getopt_long(argc, argv, "", long_options, nullptr);
    if (option == -1)
      break;
    switch (option) {
      case OPT_BACKGROUND:
        background = true;
        break;
      case OPT_CLEANUP_AFTER_CRASH:
        // Used by perfetto.rc in Android.
        PERFETTO_LOG("Hard resetting ftrace state.");
        HardResetFtraceState();
        return 0;
      case OPT_RESET_FTRACE:
        // This is like --cleanup-after-crash but doesn't quit.
        reset_ftrace = true;
        break;
      case OPT_VERSION:
        printf("%s\n", base::GetVersionString());
        return 0;
      default:
        fprintf(
            stderr,
            "Usage: %s [--background] [--reset-ftrace] [--cleanup-after-crash] "
            "[--version]\n",
            argv[0]);
        return 1;
    }
  }

  if (reset_ftrace && !HardResetFtraceState()) {
    PERFETTO_ELOG(
        "Failed to reset ftrace. Either run this as root or run "
        "`sudo chown -R $USER /sys/kernel/tracing`");
  }

  if (background) {
    base::Daemonize([] { return 0; });
  }

  base::Watchdog* watchdog = base::Watchdog::GetInstance();
  // The memory watchdog will be updated soon after connect, once the shmem
  // buffer size is known, in ProbesProducer::OnTracingSetup().
  watchdog->SetMemoryLimit(base::kWatchdogDefaultMemorySlack,
                           base::kWatchdogDefaultMemoryWindow);
  watchdog->SetCpuLimit(base::kWatchdogDefaultCpuLimit,
                        base::kWatchdogDefaultCpuWindow);
  watchdog->Start();

  PERFETTO_LOG("Starting %s service", argv[0]);

  // This environment variable is set by Android's init to a fd to /dev/kmsg
  // opened for writing (see perfetto.rc). We cannot open the file directly
  // due to permissions.
  const char* env = getenv("ANDROID_FILE__dev_kmsg");
  if (env) {
    Tracefs::g_kmesg_fd = atoi(env);
    // The file descriptor passed by init doesn't have the FD_CLOEXEC bit set.
    // Set it so we don't leak this fd while invoking atrace.
    int res = fcntl(Tracefs::g_kmesg_fd, F_SETFD, FD_CLOEXEC);
    PERFETTO_DCHECK(res == 0);
  }

  base::MaybeLockFreeTaskRunner task_runner;
  ProbesProducer producer;
  // If the TRACED_PROBES_NOTIFY_FD env var is set, write 1 and close the FD,
  // when all data sources have been registered. This is used for //src/tracebox
  // --background-wait, to make sure that the data sources are registered before
  // waiting for them to be started.
  const char* env_notif = getenv("TRACED_PROBES_NOTIFY_FD");
  if (env_notif) {
    int notif_fd = atoi(env_notif);
    producer.SetAllDataSourcesRegisteredCb([notif_fd] {
      PERFETTO_CHECK(base::WriteAll(notif_fd, "1", 1) == 1);
      PERFETTO_CHECK(base::CloseFile(notif_fd) == 0);
    });
  }
  producer.ConnectWithRetries(GetProducerSocket(), &task_runner);

  task_runner.Run();
  return 0;
}

}  // namespace perfetto
