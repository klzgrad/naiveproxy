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

#ifndef SRC_PROTOVM_NODE_H_
#define SRC_PROTOVM_NODE_H_

#include <variant>

#include "src/base/intrusive_tree.h"

#include "src/protovm/owned_ptr.h"
#include "src/protovm/scalar.h"

namespace perfetto {
namespace protovm {

struct Node;

namespace internal {
struct MapNode {
  struct Traits {
    using KeyType = uint64_t;
    static constexpr size_t NodeOffset() { return offsetof(MapNode, node); }
    static const KeyType& GetKey(const MapNode& n) { return n.key; }
  };

  MapNode(uint64_t k, OwnedPtr<Node> v) : key{k}, value{std::move(v)} {}

  base::IntrusiveTreeNode node{};
  uint64_t key;
  OwnedPtr<Node> value;
};
}  // namespace internal

using IntrusiveMap =
    base::IntrusiveTree<internal::MapNode, internal::MapNode::Traits>;

struct Node {
  using MapNode = internal::MapNode;

  struct Bytes {
    OwnedPtr<void> data;
    size_t size;
  };

  struct Empty {};

  struct MappedRepeatedField {
    IntrusiveMap key_to_node;
  };

  struct IndexedRepeatedField {
    IntrusiveMap index_to_node;

    // Flag needed to track the merge state of indexed repeated field
    // (see implementation of Merge operation in RW proto)
    bool has_been_merged;
  };

  struct Message {
    IntrusiveMap field_id_to_node;
  };

  template <class T>
  T* GetIf() {
    return std::get_if<T>(&value);
  }

  template <class T>
  const T* GetIf() const {
    return std::get_if<T>(&value);
  }

  const char* GetTypeName() const;

  std::variant<Bytes,
               Empty,
               IndexedRepeatedField,
               MapNode,
               MappedRepeatedField,
               Message,
               Scalar>
      value;
};

Node& GetOuterNode(Node::MapNode& map_node);

}  // namespace protovm
}  // namespace perfetto

#endif  // SRC_PROTOVM_NODE_H_
