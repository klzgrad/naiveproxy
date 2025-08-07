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

#include <stdio.h>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/profiling/deobfuscator.h"
#include "src/traceconv/deobfuscate_profile.h"
#include "src/traceconv/utils.h"

namespace perfetto {
namespace trace_to_text {

int DeobfuscateProfile(std::istream* input, std::ostream* output) {
  base::ignore_result(input);
  base::ignore_result(output);
  auto maybe_map = profiling::GetPerfettoProguardMapPath();
  if (maybe_map.empty()) {
    PERFETTO_ELOG("No PERFETTO_PROGUARD_MAP specified.");
    return 1;
  }
  if (!profiling::ReadProguardMapsToDeobfuscationPackets(
          maybe_map, [output](const std::string& trace_proto) {
            *output << trace_proto;
          })) {
    return 1;
  }

  return 0;
}

}  // namespace trace_to_text
}  // namespace perfetto
