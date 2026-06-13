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

#include "src/trace_processor/importers/proto/profile_packet_utils.h"
#include "perfetto/ext/base/string_utils.h"

namespace perfetto {
namespace trace_processor {

// static
std::string ProfilePacketUtils::MakeMappingName(
    const std::vector<base::StringView>& path_components) {
  std::string name;
  for (base::StringView p : path_components) {
    name.push_back('/');
    name.append(p.data(), p.size());
  }

  // When path strings just have single full path(like Chrome does), the mapping
  // path gets an extra '/' prepended, strip the extra '/'.
  if (base::StartsWith(name, "//")) {
    name = name.substr(1);
  }
  return name;
}

}  // namespace trace_processor
}  // namespace perfetto
