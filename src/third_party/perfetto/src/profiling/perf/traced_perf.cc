/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "src/profiling/perf/traced_perf.h"

#include <stdio.h>
#include <stdlib.h>

#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/getopt.h"
#include "perfetto/ext/base/lock_free_task_runner.h"
#include "perfetto/ext/base/version.h"
#include "perfetto/tracing/default_socket.h"
#include "src/profiling/perf/perf_producer.h"
#include "src/profiling/perf/proc_descriptors.h"

namespace perfetto {

namespace {
#if PERFETTO_BUILDFLAG(PERFETTO_ANDROID_BUILD)
static constexpr char kTracedPerfSocketEnvVar[] = "ANDROID_SOCKET_traced_perf";

int GetRawInheritedListeningSocket() {
  const char* sock_fd = getenv(kTracedPerfSocketEnvVar);
  if (sock_fd == nullptr)
    PERFETTO_FATAL("Did not inherit socket from init.");
  char* end;
  int raw_fd = static_cast<int>(strtol(sock_fd, &end, 10));
  if (*end != '\0')
    PERFETTO_FATAL("Invalid env variable format. Expected decimal integer.");
  return raw_fd;
}
#endif
}  // namespace

// TODO(rsavitski): watchdog.
int TracedPerfMain(int argc, char** argv) {
  enum LongOption {
    OPT_BACKGROUND = 1000,
    OPT_VERSION,
  };

  bool background = false;

  static const option long_options[] = {
      {"background", no_argument, nullptr, OPT_BACKGROUND},
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
      case OPT_VERSION:
        printf("%s\n", base::GetVersionString());
        return 0;
      default:
        fprintf(stderr, "Usage: %s [--background] [--version]\n", argv[0]);
        return 1;
    }
  }

  if (background) {
    base::Daemonize([] { return 0; });
  }

  base::MaybeLockFreeTaskRunner task_runner;

// TODO(rsavitski): support standalone --root or similar on android.
#if PERFETTO_BUILDFLAG(PERFETTO_ANDROID_BUILD)
  AndroidRemoteDescriptorGetter proc_fd_getter{GetRawInheritedListeningSocket(),
                                               &task_runner};
#else
  DirectDescriptorGetter proc_fd_getter;
#endif

  profiling::PerfProducer producer(&proc_fd_getter, &task_runner);
  const char* env_notif = getenv("TRACED_PERF_NOTIFY_FD");
  if (env_notif) {
    int notif_fd = atoi(env_notif);
    producer.SetAllDataSourcesRegisteredCb([notif_fd] {
      PERFETTO_CHECK(base::WriteAll(notif_fd, "1", 1) == 1);
      PERFETTO_CHECK(base::CloseFile(notif_fd) == 0);
    });
  }
  producer.ConnectWithRetries(GetProducerSocket());
  task_runner.Run();
  return 0;
}

}  // namespace perfetto
