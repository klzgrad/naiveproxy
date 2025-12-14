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

#include "src/traced/probes/user_list/user_list_parser.h"

#include <stdlib.h>

#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/string_splitter.h"

namespace perfetto {

bool ReadUserListLine(char* line, User* user) {
  size_t idx = 0;
  for (base::StringSplitter ss(line, ' '); ss.Next();) {
    switch (idx) {
      case 0:
        user->type = std::string(ss.cur_token(), ss.cur_token_size());
        break;
      case 1: {
        char* end;
        long long uid = strtoll(ss.cur_token(), &end, 10);
        if ((*end != '\0' && *end != '\n') || *ss.cur_token() == '\0') {
          PERFETTO_ELOG("Failed to parse user.list uid.");
          return false;
        }
        user->uid = static_cast<uint64_t>(uid);
        break;
      }
    }
    ++idx;
  }
  return true;
}

}  // namespace perfetto
