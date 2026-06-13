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

#include "src/protovm/test/utils.h"

namespace perfetto {
namespace protovm {
namespace test {

protozero::ConstBytes AsConstBytes(const std::string& s) {
  return protozero::ConstBytes{
      reinterpret_cast<uint8_t*>(const_cast<char*>(s.data())), s.size()};
}

}  // namespace test
}  // namespace protovm
}  // namespace perfetto
