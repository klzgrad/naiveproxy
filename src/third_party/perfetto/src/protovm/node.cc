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
        } else if constexpr (std::is_same_v<T, Node::MapNode>) {
          return "MapEntry";
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

Node& GetOuterNode(Node::MapNode& map_node) {
  // Hackish solution to obtain offsetof(Node, MapNode). Alas, there isn't
  // anything like the standard offsetof that works with std::variant. Note that
  // the calculation can't be constexpr, however the compiler optimizes it out.
  const Node tmp{Node::MapNode{0, nullptr}};
  const auto* tmp_map_node = std::get_if<Node::MapNode>(&tmp.value);
  const auto kOffset = reinterpret_cast<const char*>(tmp_map_node) -
                       reinterpret_cast<const char*>(&tmp);

  auto* node = reinterpret_cast<char*>(&map_node);
  node -= kOffset;
  return *reinterpret_cast<Node*>(node);
}

}  // namespace protovm
}  // namespace perfetto
