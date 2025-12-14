/*
 * Copyright (C) 2025 The Android Open Source Project
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

#ifndef INCLUDE_PERFETTO_EXT_BASE_FLAGS_H_
#define INCLUDE_PERFETTO_EXT_BASE_FLAGS_H_

#include "perfetto/base/build_config.h"

#if PERFETTO_BUILDFLAG(PERFETTO_ANDROID_BUILD) && \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
#include <perfetto_flags.h>
#endif

namespace perfetto::base::flags {

// The list of all the read-only flags accessible to the Perfetto codebase.
//
// The first argument is the name of the flag. Should match 1:1 with the name
// in `perfetto_flags.aconfig`.
// The second argument is the default value of the flag in non-Android platform
// contexts.
//
// Note: For rt_mutex and rt_futex, the source of truth for non-Android platform
// is in rt_mutex.h
#define PERFETTO_READ_ONLY_FLAGS(X)                                    \
  X(test_read_only_flag, NonAndroidPlatformDefault_FALSE)              \
  X(use_murmur_hash_for_flat_hash_map, NonAndroidPlatformDefault_TRUE) \
  X(ftrace_clear_offline_cpus_only, NonAndroidPlatformDefault_TRUE)    \
  X(use_lockfree_taskrunner,                                           \
    PERFETTO_BUILDFLAG(PERFETTO_ENABLE_LOCKFREE_TASKRUNNER)            \
        ? NonAndroidPlatformDefault_TRUE                               \
        : NonAndroidPlatformDefault_FALSE)                             \
  X(use_rt_mutex, NonAndroidPlatformDefault_FALSE)                     \
  X(use_rt_futex, NonAndroidPlatformDefault_FALSE)                     \
  X(buffer_clone_preserve_read_iter, NonAndroidPlatformDefault_TRUE)   \
  X(sma_prevent_duplicate_immediate_flushes, NonAndroidPlatformDefault_TRUE)

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//                 implementation details start here                          //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

[[maybe_unused]] constexpr bool NonAndroidPlatformDefault_TRUE = true;
[[maybe_unused]] constexpr bool NonAndroidPlatformDefault_FALSE = false;

#if PERFETTO_BUILDFLAG(PERFETTO_ANDROID_BUILD) && \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
#define PERFETTO_FLAGS_DEF_GETTER(name, default_non_android_value) \
  [[maybe_unused]] constexpr bool name = ::perfetto::flags::name();
#else
#define PERFETTO_FLAGS_DEF_GETTER(name, default_non_android_value) \
  [[maybe_unused]] constexpr bool name = default_non_android_value;
#endif

PERFETTO_READ_ONLY_FLAGS(PERFETTO_FLAGS_DEF_GETTER)

}  // namespace perfetto::base::flags

#endif  // INCLUDE_PERFETTO_EXT_BASE_FLAGS_H_
