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

#ifndef SRC_PROTOVM_RW_PROTO_CURSOR_H_
#define SRC_PROTOVM_RW_PROTO_CURSOR_H_

#include "perfetto/protozero/field.h"

#include "src/protovm/allocator.h"
#include "src/protovm/error_handling.h"
#include "src/protovm/node.h"

namespace perfetto {
namespace protovm {

class RwProtoCursor {
 public:
  class RepeatedFieldIterator {
   public:
    RepeatedFieldIterator();
    RepeatedFieldIterator(Allocator* allocator, IntrusiveMap::Iterator it);
    RepeatedFieldIterator& operator++();
    RwProtoCursor GetCursor();
    explicit operator bool() const;

   private:
    Allocator* allocator_{nullptr};
    IntrusiveMap::Iterator it_;
  };

  RwProtoCursor();
  explicit RwProtoCursor(Node* node, Allocator* allocator);
  StatusOr<bool> HasField(uint32_t field_id);
  StatusOr<void> EnterField(uint32_t field_id);
  StatusOr<void> EnterRepeatedFieldAt(uint32_t field_id, uint32_t index);
  StatusOr<RepeatedFieldIterator> IterateRepeatedField(uint32_t field_id);

  // Enters a specific repeated field, treating it as a map of key-value pairs.
  //
  // The function operates on the principle that the repeated field (identified
  // by <field_id>) can be viewed as a collection of key-value pairs, where:
  //  - Each message element within the repetition is a "value".
  //  - A specific subfield within each message element (identified by
  //   <map_key_field_id>) serves as the unique key for that element.
  //
  // The function performs a lookup using the provided <key> and, if a match
  // is found, moves the cursor into the corresponding repeated field.
  //
  // Example:
  //
  // // Message currently pointer by the cursor
  // message CurrentMessage {
  //   repeated KeyValuePair entries = <field_id>;
  // }
  //
  // message KeyValuePair {
  //   uint32 key = <map_key_field_id>;
  //   string value_x = ...;
  //   string value_y = ...;
  // }
  //
  // In this example <field_id> points to CurrentMessage.entries,
  // <map_key_field_id> points to KeyValuePair.key, and <key> = 10. So the
  // function attempts to find and enter the KeyValuePair message where
  // key == 10.
  StatusOr<void> EnterRepeatedFieldByKey(uint32_t field_id,
                                         uint32_t map_key_field_id,
                                         uint64_t key);

  StatusOr<Scalar> GetScalar() const;
  StatusOr<void> SetBytes(protozero::ConstBytes data);
  StatusOr<void> SetScalar(Scalar scalar);

  // Perform a shallow (one level) merge of two messages.
  // The cursor must currently point to a message and 'data' must contain a
  // message. Merge fields from the 'data' message into the message pointed by
  // cursor. Existing fields in the cursor's message are overwritten by fields
  // from 'data'. Fields present in 'data' but not in the cursor's message are
  // be added/created.
  StatusOr<void> Merge(protozero::ConstBytes data);

  StatusOr<void> Delete();

 private:
  StatusOr<void> ConvertToMessageIfNeeded(Node* node);
  StatusOr<OwnedPtr<Node>> CreateNodeFromField(protozero::Field field);
  StatusOr<void> ConvertToMappedRepeatedFieldIfNeeded(
      Node* node,
      uint32_t map_key_field_id);
  StatusOr<void> ConvertToIndexedRepeatedFieldIfNeeded(Node* node);
  StatusOr<IntrusiveMap::Iterator> FindOrCreateMessageField(Node* node,
                                                            uint32_t field_id);
  StatusOr<IntrusiveMap::Iterator> FindOrCreateIndexedRepeatedField(
      Node* node,
      uint32_t index);
  StatusOr<IntrusiveMap::Iterator> FindOrCreateMappedRepeatedField(
      Node* node,
      uint64_t key);
  StatusOr<IntrusiveMap::Iterator> MapInsert(IntrusiveMap* map,
                                             uint64_t key,
                                             OwnedPtr<Node> map_value);
  StatusOr<uint64_t> ReadScalarField(const Node& node, uint32_t field_id);

  Node* node_ = nullptr;
  std::pair<IntrusiveMap*, Node::MapNode*> holding_map_and_node_ = {};
  Allocator* allocator_ = nullptr;
};

}  // namespace protovm
}  // namespace perfetto

#endif  // SRC_PROTOVM_RW_PROTO_CURSOR_H_
