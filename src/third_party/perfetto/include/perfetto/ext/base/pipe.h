/*
 * Copyright (C) 2018 The Android Open Source Project
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

#ifndef INCLUDE_PERFETTO_EXT_BASE_PIPE_H_
#define INCLUDE_PERFETTO_EXT_BASE_PIPE_H_

#include "perfetto/base/platform_handle.h"
#include "perfetto/ext/base/scoped_file.h"

namespace perfetto {
namespace base {

class Pipe {
 public:
  enum Flags {
    kBothBlock = 0,
#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
    kBothNonBlock,
    kRdNonBlock,
    kWrNonBlock,
#endif
  };

  static Pipe Create(Flags = kBothBlock);

  Pipe();
  Pipe(Pipe&&) noexcept;
  Pipe& operator=(Pipe&&);

  ScopedPlatformHandle rd;
  ScopedPlatformHandle wr;
};

}  // namespace base
}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_EXT_BASE_PIPE_H_
