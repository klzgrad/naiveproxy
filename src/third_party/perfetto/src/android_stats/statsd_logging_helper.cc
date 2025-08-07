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

#include "src/android_stats/statsd_logging_helper.h"

#include <cstdint>
#include <string>
#include <vector>

#include "perfetto/base/build_config.h"
#include "src/android_stats/perfetto_atoms.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) && \
    PERFETTO_BUILDFLAG(PERFETTO_ANDROID_BUILD)
#include "src/android_internal/lazy_library_loader.h"  // nogncheck
#include "src/android_internal/statsd_logging.h"       // nogncheck
#endif

namespace perfetto::android_stats {

// Make sure we don't accidentally log on non-Android tree build. Note that even
// removing this ifdef still doesn't make uploads work on OS_ANDROID.
// PERFETTO_LAZY_LOAD will return a nullptr on non-Android and non-in-tree
// builds as libperfetto_android_internal will not be available.
#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) && \
    PERFETTO_BUILDFLAG(PERFETTO_ANDROID_BUILD)

void MaybeLogUploadEvent(PerfettoStatsdAtom atom,
                         int64_t uuid_lsb,
                         int64_t uuid_msb,
                         const std::string& trigger_name) {
  PERFETTO_LAZY_LOAD(android_internal::StatsdLogUploadEvent, log_event_fn);
  if (log_event_fn) {
    log_event_fn(atom, uuid_lsb, uuid_msb, trigger_name.c_str());
  }
}

void MaybeLogTriggerEvent(PerfettoTriggerAtom atom,
                          const std::string& trigger_name) {
  PERFETTO_LAZY_LOAD(android_internal::StatsdLogTriggerEvent, log_event_fn);
  if (log_event_fn) {
    log_event_fn(atom, trigger_name.c_str());
  }
}

void MaybeLogTriggerEvents(PerfettoTriggerAtom atom,
                           const std::vector<std::string>& triggers) {
  PERFETTO_LAZY_LOAD(android_internal::StatsdLogTriggerEvent, log_event_fn);
  if (log_event_fn) {
    for (const std::string& trigger_name : triggers) {
      log_event_fn(atom, trigger_name.c_str());
    }
  }
}

#else
void MaybeLogUploadEvent(PerfettoStatsdAtom,
                         int64_t,
                         int64_t,
                         const std::string&) {}
void MaybeLogTriggerEvent(PerfettoTriggerAtom, const std::string&) {}
void MaybeLogTriggerEvents(PerfettoTriggerAtom,
                           const std::vector<std::string>&) {}
#endif

}  // namespace perfetto::android_stats
