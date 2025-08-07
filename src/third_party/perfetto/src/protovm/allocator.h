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

#ifndef SRC_PROTOVM_ALLOCATOR_H_
#define SRC_PROTOVM_ALLOCATOR_H_

#include <cstdlib>

#include "perfetto/protozero/field.h"

#include "src/protovm/error_handling.h"
#include "src/protovm/node.h"
#include "src/protovm/slab_allocator.h"

namespace perfetto {
namespace protovm {

// A centralized allocator to manage all the memory requests from RwProto. It
// enforces a strict memory usage limit, thus allowing fine-grained control of
// the overall proto VM's memory footprint.
class Allocator {
 public:
  explicit Allocator(size_t memory_limit_bytes);
  StatusOr<Node::Bytes> AllocateAndCopyBytes(protozero::ConstBytes data);

  // Deeply delete a node and all the referenced data. E.g. if the node holds a
  // Node::Message, recursively delete the message fields and finally delete the
  // node.
  void Delete(Node* node);

  // Deeply delete a node's referenced data, but do not delete the node itself.
  // E.g. if the node holds a Node::Message, recursively delete the message
  // fields.
  void DeleteReferencedData(Node* node);

  // Recursively delete a message's fields, but do not delete the message
  // itself.
  void DeleteReferencedData(Node::Message* message);

  // Delete the blob referenced by the Bytes struct, but do not delete the Bytes
  // struct itself.
  void DeleteReferencedData(Node::Bytes* bytes);

  template <class T, class... Args>
  StatusOr<OwnedPtr<Node>> CreateNode(Args&&... args) {
    if (used_memory_bytes_ + sizeof(Node) > memory_limit_bytes_) {
      PROTOVM_ABORT(
          "Failed to allocate node (%zu bytes). Memory limit: %zu [bytes]. "
          "Used: %zu "
          "[bytes].)",
          sizeof(Node), memory_limit_bytes_, used_memory_bytes_);
    }

    auto* p = slab_allocator_.Allocate();
    if (!p) {
      PROTOVM_ABORT("Failed to allocate node");
    }

    used_memory_bytes_ += sizeof(Node);

    // 1. Construct node's inner value T passing args (struct aggregate
    // initialization)
    // 2. Construct node passing T (struct aggregate initialization)
    new (p) Node{T{std::forward<Args>(args)...}};

    return OwnedPtr<Node>(static_cast<Node*>(p));
  }

 private:
  void DeallocateBytes(OwnedPtr<void> p, size_t size);

  size_t memory_limit_bytes_{0};
  size_t used_memory_bytes_{0};
  SlabAllocator<sizeof(Node), alignof(Node)> slab_allocator_;
};

}  // namespace protovm
}  // namespace perfetto

#endif  // SRC_PROTOVM_ALLOCATOR_H_
