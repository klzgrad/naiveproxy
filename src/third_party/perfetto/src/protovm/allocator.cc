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

#include "src/protovm/allocator.h"

namespace perfetto {
namespace protovm {

Allocator::Allocator(size_t memory_limit_bytes)
    : memory_limit_bytes_{memory_limit_bytes}, used_memory_bytes_{0} {}

StatusOr<Node::Bytes> Allocator::AllocateAndCopyBytes(
    protozero::ConstBytes data) {
  if (used_memory_bytes_ + data.size > memory_limit_bytes_) {
    PROTOVM_ABORT(
        "Failed to allocate %zu bytes. Memory limit: %zu bytes. Used: %zu "
        "bytes.)",
        data.size, memory_limit_bytes_, used_memory_bytes_);
  }

  if (data.size == 0) {
    return Node::Bytes{nullptr, 0};
  }

  auto copy = OwnedPtr<void>{malloc(data.size)};
  if (!copy) {
    PROTOVM_ABORT("Failed to malloc %zu bytes", data.size);
  }
  used_memory_bytes_ += data.size;
  memcpy(copy.get(), data.data, data.size);

  return Node::Bytes{std::move(copy), data.size};
}

void Allocator::Delete(Node* node) {
  DeleteReferencedData(node);
  node->~Node();
  slab_allocator_.Free(node);
}

void Allocator::DeleteReferencedData(Node* node) {
  if (auto* message = node->GetIf<Node::Message>()) {
    DeleteReferencedData(message);
  } else if (auto* indexed_fields = node->GetIf<Node::IndexedRepeatedField>()) {
    for (auto it = indexed_fields->index_to_node.begin(); it;) {
      auto& map_node = *it;
      it = indexed_fields->index_to_node.Remove(it);
      Delete(&GetOuterNode(map_node));
    }
  } else if (auto* mapped_fields = node->GetIf<Node::MappedRepeatedField>()) {
    for (auto it = mapped_fields->key_to_node.begin(); it;) {
      auto& map_node = *it;
      it = mapped_fields->key_to_node.Remove(it);
      Delete(&GetOuterNode(map_node));
    }
  } else if (auto* map_node = node->GetIf<Node::MapNode>()) {
    Delete(map_node->value.release());
  } else if (auto* bytes = node->GetIf<Node::Bytes>()) {
    DeleteReferencedData(bytes);
  }
}

void Allocator::DeleteReferencedData(Node::Message* message) {
  for (auto it = message->field_id_to_node.begin(); it;) {
    auto& map_node = *it;
    it = message->field_id_to_node.Remove(it);
    Delete(&GetOuterNode(map_node));
  }
}

void Allocator::DeleteReferencedData(Node::Bytes* bytes) {
  DeallocateBytes(std::move(bytes->data), bytes->size);
}

void Allocator::DeallocateBytes(OwnedPtr<void> p, size_t size) {
  free(p.release());
  used_memory_bytes_ -= size;
}

}  // namespace protovm
}  // namespace perfetto
