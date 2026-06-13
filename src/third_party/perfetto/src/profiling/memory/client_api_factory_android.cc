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

#include "src/profiling/memory/client_api_factory.h"

#include <string>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <optional>

#include "perfetto/base/build_config.h"
#include "perfetto/ext/base/unix_socket.h"
#include "src/profiling/memory/client.h"

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
#error "Must be built on Android."
#endif

namespace perfetto {
namespace profiling {

void StartHeapprofdIfStatic() {}

std::shared_ptr<Client> ConstructClient(
    UnhookedAllocator<perfetto::profiling::Client> unhooked_allocator) {
  PERFETTO_LOG("Constructing client for central daemon.");
  using perfetto::profiling::Client;

  std::optional<perfetto::base::UnixSocketRaw> sock =
      Client::ConnectToHeapprofd(perfetto::profiling::kHeapprofdSocketFile);
  if (!sock) {
    PERFETTO_ELOG("Failed to connect to %s.",
                  perfetto::profiling::kHeapprofdSocketFile);
    return nullptr;
  }
  return Client::CreateAndHandshake(std::move(sock.value()),
                                    unhooked_allocator);
}

}  // namespace profiling
}  // namespace perfetto
