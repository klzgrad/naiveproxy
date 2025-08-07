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

#include "src/android_internal/statsd_logging.h"

#include <cstdint>

#include <statslog_perfetto.h>

namespace perfetto::android_internal {

void StatsdLogUploadEvent(PerfettoStatsdAtom atom,
                          int64_t uuid_lsb,
                          int64_t uuid_msb,
                          const char* trigger_name) {
  stats_write(PERFETTO_UPLOADED, static_cast<int32_t>(atom), uuid_lsb, uuid_msb,
              trigger_name ? trigger_name : "");
}

void StatsdLogTriggerEvent(PerfettoTriggerAtom atom, const char* trigger_name) {
  stats_write(PERFETTO_TRIGGER, static_cast<int32_t>(atom), trigger_name);
}

}  // namespace perfetto::android_internal
