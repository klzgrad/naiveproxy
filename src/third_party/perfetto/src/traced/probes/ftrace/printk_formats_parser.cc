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

#include "src/traced/probes/ftrace/printk_formats_parser.h"

#include <stdio.h>

#include <cinttypes>
#include <optional>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/string_utils.h"

namespace perfetto {

PrintkMap ParsePrintkFormats(const std::string& format) {
  PrintkMap mapping;
  for (base::StringSplitter lines(format, '\n'); lines.Next();) {
    // Lines have the format:
    // 0xdeadbeef : "not alive cow"
    // and may be duplicated.
    std::string line(lines.cur_token());

    auto index = line.find(':');
    if (index == std::string::npos)
      continue;
    std::string raw_address = line.substr(0, index);
    std::string name = line.substr(index);

    // Remove colon, space and surrounding quotes:
    raw_address = base::StripSuffix(raw_address, " ");
    name = base::StripPrefix(name, ":");
    name = base::StripPrefix(name, " ");
    name = base::StripPrefix(name, "\"");
    name = base::StripSuffix(name, "\"");

    if (name.empty())
      continue;

    std::optional<uint64_t> address = base::StringToUInt64(raw_address, 16);
    if (address && address.value() != 0)
      mapping.insert(address.value(), name);
  }
  return mapping;
}

}  // namespace perfetto
