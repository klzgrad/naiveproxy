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

#include "src/traced/probes/ftrace/vendor_tracepoints.h"

#include <errno.h>
#include <string.h>

#include <map>
#include <string>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/string_splitter.h"
#include "src/traced/probes/ftrace/atrace_hal_wrapper.h"
#include "src/traced/probes/ftrace/ftrace_procfs.h"
#include "src/traced/probes/ftrace/proto_translation_table.h"

namespace perfetto {
namespace vendor_tracepoints {
namespace {

using EmptyTokenMode = ::perfetto::base::StringSplitter::EmptyTokenMode;

std::vector<GroupAndName> DiscoverTracepoints(AtraceHalWrapper* hal,
                                              FtraceProcfs* ftrace,
                                              const std::string& category) {
  ftrace->DisableAllEvents();
  hal->EnableCategories({category});

  std::vector<GroupAndName> events;
  for (const std::string& group_name : ftrace->ReadEnabledEvents()) {
    size_t pos = group_name.find('/');
    PERFETTO_CHECK(pos != std::string::npos);
    events.push_back(
        GroupAndName(group_name.substr(0, pos), group_name.substr(pos + 1)));
  }

  hal->DisableAllCategories();
  ftrace->DisableAllEvents();
  return events;
}

base::Status ParseEventLine(base::StringView line,
                            std::vector<GroupAndName>* category) {
  // `line` is a line in the vendor file that starts with one or more whitespace
  // and is expected to contain the path to an ftrace event like:
  // ```
  //  cma/cma_alloc_start
  // ```
  while (!line.empty() && (line.at(0) == ' ' || line.at(0) == '\t')) {
    line = line.substr(1);
  }
  if (line.empty()) {
    return base::OkStatus();
  }
  size_t pos = line.find('/');
  if (pos == line.npos) {
    return base::ErrStatus("Ftrace event path not in group/event format");
  }
  base::StringView group = line.substr(0, pos);
  if (group.empty()) {
    return base::ErrStatus("Ftrace event path group is empty");
  }
  base::StringView name = line.substr(pos + 1);
  if (name.find('/') != name.npos) {
    return base::ErrStatus("Ftrace event path has extra / in event name");
  }
  if (name.empty()) {
    return base::ErrStatus("Ftrace event name empty");
  }
  category->push_back(GroupAndName(group.ToStdString(), name.ToStdString()));
  return base::OkStatus();
}

}  // namespace

std::map<std::string, std::vector<GroupAndName>>
DiscoverVendorTracepointsWithHal(AtraceHalWrapper* hal, FtraceProcfs* ftrace) {
  std::map<std::string, std::vector<GroupAndName>> results;
  for (const auto& category : hal->ListCategories()) {
    results.emplace(category, DiscoverTracepoints(hal, ftrace, category));
  }
  return results;
}

base::Status DiscoverVendorTracepointsWithFile(
    const std::string& vendor_atrace_categories_path,
    std::map<std::string, std::vector<GroupAndName>>* categories_map) {
  std::string content;
  if (!base::ReadFile(vendor_atrace_categories_path, &content)) {
    return base::ErrStatus("Cannot read vendor atrace file: %s (errno: %d, %s)",
                           vendor_atrace_categories_path.c_str(), errno,
                           strerror(errno));
  }
  // The file should contain a list of categories (one per line) and, for each
  // category, a list of ftrace events (one per line, nested):
  // ```
  // gfx
  //  mali/gpu_power_state
  //  mali/mali_pm_status
  // thermal_tj
  //  thermal_exynos/thermal_cpu_pressure
  //  thermal_exynos/thermal_exynos_arm_update
  // ```
  std::vector<GroupAndName>* category = nullptr;
  for (base::StringSplitter lines(std::move(content), '\n',
                                  EmptyTokenMode::DISALLOW_EMPTY_TOKENS);
       lines.Next();) {
    base::StringView line(lines.cur_token());
    if (line.empty()) {
      continue;
    }
    char firstchar = line.at(0);
    if (firstchar == '\t' || firstchar == ' ') {
      // The line begins with a whitespace. It should contain an ftrace event
      // path, part of a previously defined category.
      if (category == nullptr) {
        return base::ErrStatus(
            "Ftrace event path before category. Malformed vendor atrace file");
      }
      base::Status status = ParseEventLine(line, category);
      if (!status.ok()) {
        return status;
      }
    } else {
      // The line doesn't begin with a whitespace. Start a new category.
      category = &(*categories_map)[line.ToStdString()];
    }
  }
  return base::OkStatus();
}

base::Status DiscoverAccessibleVendorTracepointsWithFile(
    const std::string& vendor_atrace_categories_path,
    std::map<std::string, std::vector<GroupAndName>>* categories_map,
    FtraceProcfs* ftrace) {
  categories_map->clear();
  base::Status status = DiscoverVendorTracepointsWithFile(
      vendor_atrace_categories_path, categories_map);
  if (!status.ok()) {
    return status;
  }

  for (auto& it : *categories_map) {
    std::vector<GroupAndName>& events = it.second;
    events.erase(std::remove_if(events.begin(), events.end(),
                                [ftrace](const GroupAndName& event) {
                                  return !ftrace->IsEventAccessible(
                                      event.group(), event.name());
                                }),
                 events.end());
  }

  return base::OkStatus();
}

}  // namespace vendor_tracepoints
}  // namespace perfetto
