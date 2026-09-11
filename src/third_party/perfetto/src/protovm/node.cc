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

#include "src/protovm/node.h"

namespace perfetto {
namespace protovm {

const char* Node::GetTypeName() const {
  return std::visit(
      [](auto&& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, Node::Bytes>) {
          return "Bytes";
        } else if constexpr (std::is_same_v<T, Node::Empty>) {
          return "Empty";
        } else if constexpr (std::is_same_v<T, Node::IndexedRepeatedField>) {
          return "IndexedRepeatedField";
        } else if constexpr (std::is_same_v<T, Node::MappedRepeatedField>) {
          return "MappedRepeatedField";
        } else if constexpr (std::is_same_v<T, Node::Message>) {
          return "Message";
        } else if constexpr (std::is_same_v<T, Scalar>) {
          return "Scalar";
        } else {
          return "<unknown>";
        }
      },
      value);
}

}  // namespace protovm
}  // namespace perfetto
