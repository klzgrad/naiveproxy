/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "src/profiling/memory/heapprofd.h"

#include <stdlib.h>
#include <unistd.h>
#include <array>
#include <memory>
#include <vector>

#include <signal.h>

#include "perfetto/ext/base/event_fd.h"
#include "perfetto/ext/base/getopt.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/unix_socket.h"
#include "perfetto/ext/base/watchdog.h"
#include "perfetto/tracing/default_socket.h"
#include "src/profiling/memory/heapprofd_producer.h"
#include "src/profiling/memory/java_hprof_producer.h"
#include "src/profiling/memory/wire_protocol.h"

#include "perfetto/ext/base/unix_task_runner.h"

// TODO(rsavitski): the task runner watchdog spawns a thread (normally for
// tracking cpu/mem usage) that we don't strictly need.

namespace perfetto {
namespace profiling {
namespace {

int StartCentralHeapprofd();

int GetListeningSocket() {
  const char* sock_fd = getenv(kHeapprofdSocketEnvVar);
  if (sock_fd == nullptr)
    PERFETTO_FATAL("Did not inherit socket from init.");
  char* end;
  int raw_fd = static_cast<int>(strtol(sock_fd, &end, 10));
  if (*end != '\0')
    PERFETTO_FATAL("Invalid %s. Expected decimal integer.",
                   kHeapprofdSocketEnvVar);
  return raw_fd;
}

base::EventFd* g_dump_evt = nullptr;

int StartCentralHeapprofd() {
  // We set this up before launching any threads, so we do not have to use a
  // std::atomic for g_dump_evt.
  g_dump_evt = new base::EventFd();

  base::UnixTaskRunner task_runner;
  base::Watchdog::GetInstance()->Start();  // crash on exceedingly long tasks
  HeapprofdProducer producer(HeapprofdMode::kCentral, &task_runner,
                             /* exit_when_done= */ false);

  int listening_raw_socket = GetListeningSocket();
  auto listening_socket = base::UnixSocket::Listen(
      base::ScopedFile(listening_raw_socket), &producer.socket_delegate(),
      &task_runner, base::SockFamily::kUnix, base::SockType::kStream);

  struct sigaction action = {};
  action.sa_handler = [](int) { g_dump_evt->Notify(); };
  // Allow to trigger a full dump by sending SIGUSR1 to heapprofd.
  // This will allow manually deciding when to dump on userdebug.
  PERFETTO_CHECK(sigaction(SIGUSR1, &action, nullptr) == 0);
  task_runner.AddFileDescriptorWatch(g_dump_evt->fd(), [&producer] {
    g_dump_evt->Clear();
    producer.DumpAll();
  });
  producer.ConnectWithRetries(GetProducerSocket());
  // TODO(fmayer): Create one producer that manages both heapprofd and Java
  // producers, so we do not have two connections to traced.
  JavaHprofProducer java_producer(&task_runner);
  java_producer.ConnectWithRetries(GetProducerSocket());
  task_runner.Run();
  return 0;
}

}  // namespace

int HeapprofdMain(int argc, char** argv) {
  bool cleanup_crash = false;

  enum { kCleanupCrash = 256, kTargetPid, kTargetCmd, kInheritFd };
  static option long_options[] = {
      {"cleanup-after-crash", no_argument, nullptr, kCleanupCrash},
      {nullptr, 0, nullptr, 0}};
  int c;
  while ((c = getopt_long(argc, argv, "", long_options, nullptr)) != -1) {
    switch (c) {
      case kCleanupCrash:
        cleanup_crash = true;
        break;
    }
  }

  if (cleanup_crash) {
    PERFETTO_LOG(
        "Recovering from crash: unsetting heapprofd system properties. "
        "Expect SELinux denials for unrelated properties.");
    SystemProperties::ResetHeapprofdProperties();
    PERFETTO_LOG(
        "Finished unsetting heapprofd system properties. "
        "SELinux denials about properties are unexpected after "
        "this point.");
    return 0;
  }

  // start as a central daemon.
  return StartCentralHeapprofd();
}

}  // namespace profiling
}  // namespace perfetto
