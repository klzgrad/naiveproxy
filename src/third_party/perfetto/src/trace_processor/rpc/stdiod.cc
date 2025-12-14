/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "src/trace_processor/rpc/stdiod.h"

#include <cstddef>
#include <cstdint>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/utils.h"
#include "src/trace_processor/rpc/rpc.h"

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <unistd.h>
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN) && !defined(STDIN_FILENO)
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#endif

namespace perfetto::trace_processor {

base::Status RunStdioRpcServer(Rpc& rpc) {
  char buffer[4096];
  for (;;) {
    auto ret = base::Read(STDIN_FILENO, buffer, base::ArraySize(buffer));
    if (ret == -1) {
      return base::ErrStatus("Failed while reading the buffer");
    }
    if (ret == 0) {
      return base::OkStatus();
    }
    rpc.SetRpcResponseFunction([](const void* ptr, uint32_t size) {
      auto ret = base::WriteAll(STDOUT_FILENO, ptr, size);
      if (ret < 0 || static_cast<uint32_t>(ret) != size) {
        PERFETTO_FATAL("Failed to write response");
      }
    });
    rpc.OnRpcRequest(buffer, static_cast<size_t>(ret));
    rpc.SetRpcResponseFunction(nullptr);
  }
}

}  // namespace perfetto::trace_processor
