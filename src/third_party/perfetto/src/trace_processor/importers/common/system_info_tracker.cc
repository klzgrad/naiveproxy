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

#include "src/trace_processor/importers/common/system_info_tracker.h"

#include <cstddef>
#include <optional>

#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/types/version_number.h"

namespace perfetto::trace_processor {

SystemInfoTracker::SystemInfoTracker() = default;
SystemInfoTracker::~SystemInfoTracker() = default;

void SystemInfoTracker::SetKernelVersion(base::StringView name,
                                         base::StringView release) {
  if (name.empty() || release.empty() || name != "Linux") {
    version_ = std::nullopt;
    return;
  }

  size_t first_dot_pos = release.find(".");
  size_t second_dot_pos = release.find(".", first_dot_pos + 1);
  auto major_version =
      base::StringToUInt32(release.substr(0, first_dot_pos).ToStdString());
  auto minor_version = base::StringToUInt32(
      release.substr(first_dot_pos + 1, second_dot_pos - (first_dot_pos + 1))
          .ToStdString());
  if (!major_version || !minor_version) {
    version_ = std::nullopt;
    return;
  }
  version_ = VersionNumber{major_version.value(), minor_version.value()};
}

}  // namespace perfetto::trace_processor
