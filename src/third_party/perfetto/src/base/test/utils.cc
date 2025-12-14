/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "src/base/test/utils.h"

#include <stdlib.h>

#include <memory>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/utils.h"

namespace perfetto {
namespace base {

std::string GetTestDataPath(const std::string& path) {
  std::string self_path = GetCurExecutableDir();
  std::string full_path = self_path + "/../../" + path;
  if (FileExists(full_path))
    return full_path;
  full_path = self_path + "/" + path;
  if (FileExists(full_path))
    return full_path;
  // Fall back to relative to root dir.
  return path;
}

}  // namespace base
}  // namespace perfetto
