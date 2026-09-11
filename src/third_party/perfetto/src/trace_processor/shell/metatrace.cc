/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "src/trace_processor/shell/metatrace.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/trace_processor/metatrace_config.h"
#include "perfetto/trace_processor/trace_processor.h"

namespace perfetto::trace_processor {

metatrace::MetatraceCategories ParseMetatraceCategories(std::string s) {
  using Cat = metatrace::MetatraceCategories;
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  base::StringSplitter splitter(s, ',');

  Cat result = Cat::NONE;
  for (; splitter.Next();) {
    std::string cur = splitter.cur_token();
    if (cur == "all" || cur == "*") {
      result = Cat::ALL;
    } else if (cur == "query_toplevel") {
      result = static_cast<Cat>(result | Cat::QUERY_TIMELINE);
    } else if (cur == "query_detailed") {
      result = static_cast<Cat>(result | Cat::QUERY_DETAILED);
    } else if (cur == "function_call") {
      result = static_cast<Cat>(result | Cat::FUNCTION_CALL);
    } else if (cur == "db") {
      result = static_cast<Cat>(result | Cat::DB);
    } else if (cur == "api") {
      result = static_cast<Cat>(result | Cat::API_TIMELINE);
    } else {
      PERFETTO_ELOG("Unknown metatrace category %s", cur.data());
      exit(1);
    }
  }
  return result;
}

base::Status MaybeWriteMetatrace(TraceProcessor* trace_processor,
                                 const std::string& metatrace_path) {
  if (metatrace_path.empty()) {
    return base::OkStatus();
  }
  std::vector<uint8_t> serialized;
  RETURN_IF_ERROR(trace_processor->DisableAndReadMetatrace(&serialized));

  auto file = base::OpenFile(metatrace_path, O_CREAT | O_RDWR | O_TRUNC, 0600);
  if (!file)
    return base::ErrStatus("Unable to open metatrace file");

  auto res = base::WriteAll(*file, serialized.data(), serialized.size());
  if (res < 0)
    return base::ErrStatus("Error while writing metatrace file");
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor
