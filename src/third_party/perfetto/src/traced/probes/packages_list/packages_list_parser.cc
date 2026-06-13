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

#include "src/traced/probes/packages_list/packages_list_parser.h"

#include <stdlib.h>

#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/string_splitter.h"

namespace perfetto {

bool ReadPackagesListLine(char* line, Package* package) {
  size_t idx = 0;
  for (base::StringSplitter ss(line, ' '); ss.Next();) {
    switch (idx) {
      case 0:
        package->name = std::string(ss.cur_token(), ss.cur_token_size());
        break;
      case 1: {
        char* end;
        long long uid = strtoll(ss.cur_token(), &end, 10);
        if ((*end != '\0' && *end != '\n') || *ss.cur_token() == '\0') {
          PERFETTO_ELOG("Failed to parse packages.list uid.");
          return false;
        }
        package->uid = static_cast<uint64_t>(uid);
        break;
      }
      case 2: {
        char* end;
        long long debuggable = strtoll(ss.cur_token(), &end, 10);
        if ((*end != '\0' && *end != '\n') || *ss.cur_token() == '\0') {
          PERFETTO_ELOG("Failed to parse packages.list debuggable.");
          return false;
        }
        package->debuggable = debuggable != 0;
        break;
      }
      case 6: {
        char* end;
        long long profilable_from_shell = strtoll(ss.cur_token(), &end, 10);
        if ((*end != '\0' && *end != '\n') || *ss.cur_token() == '\0') {
          PERFETTO_ELOG("Failed to parse packages.list profilable_from_shell.");
          return false;
        }
        package->profileable_from_shell = profilable_from_shell != 0;
        break;
      }
      case 7: {
        char* end;
        long long version_code = strtoll(ss.cur_token(), &end, 10);
        if ((*end != '\0' && *end != '\n') || *ss.cur_token() == '\0') {
          PERFETTO_ELOG("Failed to parse packages.list version_code: %s.",
                        ss.cur_token());
          return false;
        }
        package->version_code = version_code;
        break;
      }
      case 8: {
        char* end;
        long long profileable = strtoll(ss.cur_token(), &end, 10);
        if ((*end != '\0' && *end != '\n') || *ss.cur_token() == '\0') {
          PERFETTO_ELOG("Failed to parse packages.list profileable.");
          return false;
        }
        package->profileable = profileable != 0;
        break;
      }
      case 9:
        package->installed_by =
            std::string(ss.cur_token(), ss.cur_token_size());
        break;
    }
    ++idx;
  }
  return true;
}

}  // namespace perfetto
