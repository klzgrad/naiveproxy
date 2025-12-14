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

#include "src/protovm/rw_proto_cursor.h"

#include "perfetto/protozero/proto_decoder.h"

namespace perfetto {
namespace protovm {

RwProtoCursor::RepeatedFieldIterator::RepeatedFieldIterator()
    : allocator_{nullptr}, it_{} {}

RwProtoCursor::RepeatedFieldIterator::RepeatedFieldIterator(
    Allocator* allocator,
    IntrusiveMap::Iterator it)
    : allocator_{allocator}, it_{it} {}

RwProtoCursor::RepeatedFieldIterator&
RwProtoCursor::RepeatedFieldIterator::RepeatedFieldIterator::operator++() {
  ++it_;
  return *this;
}

RwProtoCursor RwProtoCursor::RepeatedFieldIterator::GetCursor() {
  return RwProtoCursor{it_->value.get(), allocator_};
}

RwProtoCursor::RepeatedFieldIterator::operator bool() const {
  return static_cast<bool>(it_);
}

RwProtoCursor::RwProtoCursor() = default;

RwProtoCursor::RwProtoCursor(Node* node, Allocator* allocator)
    : node_{node}, allocator_{allocator} {}

StatusOr<bool> RwProtoCursor::HasField(uint32_t field_id) {
  PERFETTO_DCHECK(node_);

  // Eagerly decompose bytes because the field being tested will be entered
  // later anyways. See Executor::EnterField().
  auto status = ConvertToMessageIfNeeded(node_);
  PROTOVM_RETURN_IF_NOT_OK(status);
  auto* message = node_->GetIf<Node::Message>();
  bool found = static_cast<bool>(message->field_id_to_node.Find(field_id));
  return found;
}

StatusOr<void> RwProtoCursor::EnterField(uint32_t field_id) {
  PERFETTO_DCHECK(node_);

  auto status_or_it = FindOrCreateMessageField(node_, field_id);
  PROTOVM_RETURN_IF_NOT_OK(status_or_it);
  auto it = *status_or_it;

  if (it->value->GetIf<Node::IndexedRepeatedField>()) {
    PROTOVM_ABORT(
        "Attempted to enter field (id=%u) as a simple field but it is an "
        "indexed repeated field",
        field_id);
  }

  if (it->value->GetIf<Node::MappedRepeatedField>()) {
    PROTOVM_ABORT(
        "Attempted to enter field (id=%u) as a simple field but it is a "
        "mapped repeated field",
        field_id);
  }

  holding_map_and_node_ = {&node_->GetIf<Node::Message>()->field_id_to_node,
                           std::addressof(*it)};
  node_ = it->value.get();
  return StatusOr<void>::Ok();
}

StatusOr<void> RwProtoCursor::EnterRepeatedFieldAt(uint32_t field_id,
                                                   uint32_t index) {
  PERFETTO_DCHECK(node_);

  auto status_or_message_field = FindOrCreateMessageField(node_, field_id);
  PROTOVM_RETURN_IF_NOT_OK(status_or_message_field);
  auto message_field = *status_or_message_field;

  auto status =
      ConvertToIndexedRepeatedFieldIfNeeded(message_field->value.get());
  PROTOVM_RETURN_IF_NOT_OK(status);

  auto status_or_repeated_field =
      FindOrCreateIndexedRepeatedField(message_field->value.get(), index);
  PROTOVM_RETURN_IF_NOT_OK(status_or_repeated_field);

  holding_map_and_node_ = {
      &message_field->value->GetIf<Node::IndexedRepeatedField>()->index_to_node,
      std::addressof(**status_or_repeated_field)};
  node_ = (*status_or_repeated_field)->value.get();

  return StatusOr<void>::Ok();
}

StatusOr<RwProtoCursor::RepeatedFieldIterator>
RwProtoCursor::IterateRepeatedField(uint32_t field_id) {
  PERFETTO_DCHECK(node_);

  auto status_convertion_to_message = ConvertToMessageIfNeeded(node_);
  PROTOVM_RETURN_IF_NOT_OK(status_convertion_to_message);

  auto* message = node_->GetIf<Node::Message>();
  auto it = message->field_id_to_node.Find(field_id);

  if (!it) {
    return RepeatedFieldIterator{};
  }

  auto* field = it->value.get();
  auto status_convertion_to_repeated_field =
      ConvertToIndexedRepeatedFieldIfNeeded(field);
  PROTOVM_RETURN_IF_NOT_OK(status_convertion_to_repeated_field);

  return RepeatedFieldIterator{
      allocator_,
      field->GetIf<Node::IndexedRepeatedField>()->index_to_node.begin()};
}

StatusOr<void> RwProtoCursor::EnterRepeatedFieldByKey(uint32_t field_id,
                                                      uint32_t map_key_field_id,
                                                      uint64_t key) {
  PERFETTO_DCHECK(node_);

  auto status_or_message_field = FindOrCreateMessageField(node_, field_id);
  PROTOVM_RETURN_IF_NOT_OK(status_or_message_field);
  auto message_field = *status_or_message_field;

  auto status_conversion = ConvertToMappedRepeatedFieldIfNeeded(
      message_field->value.get(), map_key_field_id);
  PROTOVM_RETURN_IF_NOT_OK(status_conversion);

  auto status_or_repeated_field =
      FindOrCreateMappedRepeatedField(message_field->value.get(), key);
  PROTOVM_RETURN_IF_NOT_OK(status_or_repeated_field);

  holding_map_and_node_ = {
      &message_field->value->GetIf<Node::MappedRepeatedField>()->key_to_node,
      std::addressof(**status_or_repeated_field)};
  node_ = (*status_or_repeated_field)->value.get();

  return StatusOr<void>::Ok();
}

StatusOr<Scalar> RwProtoCursor::GetScalar() const {
  PERFETTO_DCHECK(node_);

  auto* scalar = node_->GetIf<Scalar>();
  if (!scalar) {
    PROTOVM_ABORT("Attempted \"get scalar\" operation but node has type %s",
                  node_->GetTypeName());
  }
  return *scalar;
}

StatusOr<void> RwProtoCursor::SetBytes(protozero::ConstBytes data) {
  PERFETTO_DCHECK(node_);

  if (bool is_compatible = node_->GetIf<Node::Empty>() ||
                           node_->GetIf<Node::Bytes>() ||
                           node_->GetIf<Node::Message>();
      !is_compatible) {
    PROTOVM_ABORT("Attempted \"set bytes\" operation but node has type %s",
                  node_->GetTypeName());
  }

  auto status_or_bytes = allocator_->AllocateAndCopyBytes(data);
  PROTOVM_RETURN_IF_NOT_OK(status_or_bytes);

  allocator_->DeleteReferencedData(node_);
  node_->value = Node::Bytes{std::move(*status_or_bytes)};

  return StatusOr<void>::Ok();
}

StatusOr<void> RwProtoCursor::SetScalar(Scalar scalar) {
  PERFETTO_DCHECK(node_);

  if (bool is_compatible =
          node_->GetIf<Node::Empty>() || node_->GetIf<Scalar>();
      !is_compatible) {
    PROTOVM_ABORT("Attempted \"set scalar\" operation but node has type %s",
                  node_->GetTypeName());
  }

  node_->value = scalar;
  return StatusOr<void>::Ok();
}

StatusOr<void> RwProtoCursor::Merge(protozero::ConstBytes data) {
  PERFETTO_DCHECK(node_);

  if (bool is_compatible = node_->GetIf<Node::Empty>() ||
                           node_->GetIf<Node::Message>() ||
                           node_->GetIf<Node::Bytes>();
      !is_compatible) {
    PROTOVM_ABORT("Attempted MERGE operation but node has type %s",
                  node_->GetTypeName());
  }

  if (data.size == 0) {
    return StatusOr<void>::Ok();
  }

  auto status_convertion = ConvertToMessageIfNeeded(node_);
  PROTOVM_RETURN_IF_NOT_OK(status_convertion);
  auto* message = node_->GetIf<Node::Message>();

  protozero::ProtoDecoder decoder(data);

  for (auto field = decoder.ReadField(); field.valid();
       field = decoder.ReadField()) {
    auto status_or_map_value = CreateNodeFromField(field);
    PROTOVM_RETURN_IF_NOT_OK(status_or_map_value);

    auto it = message->field_id_to_node.Find(field.id());

    if (!it) {
      auto status_or_it = MapInsert(&message->field_id_to_node, field.id(),
                                    std::move(*status_or_map_value));
      PROTOVM_RETURN_IF_NOT_OK(status_or_it);
      continue;
    }

    if (it->value->GetIf<Node::MappedRepeatedField>()) {
      PROTOVM_ABORT(
          "Merge operation of mapped repeated field is not supported (field id "
          "= %u)",
          field.id());
    }

    if (auto* indexed_fields = it->value->GetIf<Node::IndexedRepeatedField>()) {
      // Implements merge semantics for repeated fields: all existing fields are
      // removed and replaced with the newly received fields.
      if (!indexed_fields->has_been_merged) {
        // Optimization opportunity: reuse the existing nodes to avoid N
        // allocation-deallocation pairs, where N is the number of newly
        // received repeated fields.
        allocator_->DeleteReferencedData(it->value.get());
        indexed_fields->has_been_merged = true;
      }
      MapInsert(&indexed_fields->index_to_node,
                indexed_fields->index_to_node.Size(),
                std::move(*status_or_map_value));
      continue;
    }

    // Optimization oppurtunity: reuse the existing node to avoid one
    // allocation-deallocation pair
    allocator_->Delete(it->value.release());
    it->value = std::move(*status_or_map_value);
  }

  // Reset the merge state of repeated fields
  for (auto& field : message->field_id_to_node) {
    if (auto* indexed_fields =
            field.value->GetIf<Node::IndexedRepeatedField>()) {
      indexed_fields->has_been_merged = false;
    }
  }

  return StatusOr<void>::Ok();
}

StatusOr<void> RwProtoCursor::Delete() {
  PERFETTO_DCHECK(node_);

  bool is_root_node = !holding_map_and_node_.first;
  if (is_root_node) {
    node_->value = Node::Empty{};
    return StatusOr<void>::Ok();
  }

  auto [holding_map, map_node] = holding_map_and_node_;
  PERFETTO_DCHECK(holding_map);
  PERFETTO_DCHECK(map_node);
  holding_map->Remove(*map_node);
  allocator_->Delete(&GetOuterNode(*map_node));

  node_ = nullptr;  // Delete operation invalidates cursor

  return StatusOr<void>::Ok();
}

StatusOr<void> RwProtoCursor::ConvertToMessageIfNeeded(Node* node) {
  if (node->GetIf<Node::Message>()) {
    return StatusOr<void>::Ok();
  }

  if (node->GetIf<Node::Empty>()) {
    node->value = Node::Message{};
    return StatusOr<void>::Ok();
  }

  auto* bytes = node->GetIf<Node::Bytes>();
  if (!bytes) {
    PROTOVM_ABORT("Attempted conversion to message but node has type %s",
                  node->GetTypeName());
  }

  Node::Message message;

  protozero::ProtoDecoder decoder(protozero::ConstBytes{
      static_cast<const uint8_t*>(bytes->data.get()), bytes->size});

  for (auto field = decoder.ReadField(); field.valid();
       field = decoder.ReadField()) {
    auto status_or_map_value = CreateNodeFromField(field);
    if (!status_or_map_value.IsOk()) {
      allocator_->DeleteReferencedData(&message);
      PROTOVM_RETURN(status_or_map_value);
    }

    auto it = message.field_id_to_node.Find(field.id());

    // First occurrence of this field id. Just insert a new field into the
    // map.
    if (!it) {
      auto status_or_it = MapInsert(&message.field_id_to_node, field.id(),
                                    std::move(*status_or_map_value));
      if (!status_or_it.IsOk()) {
        allocator_->DeleteReferencedData(&message);
        PROTOVM_RETURN(status_or_it, "Insert message field (id = %u)",
                       field.id());
      }
      continue;
    }

    // Nth occurrence of this field id:
    // 1. Make sure we have an IndexedRepeatedField node
    // 2. Append into the IndexedRepeatedField's map
    auto status_conversion =
        ConvertToIndexedRepeatedFieldIfNeeded(it->value.get());
    if (!status_conversion.IsOk()) {
      allocator_->DeleteReferencedData(&message);
      allocator_->Delete(status_or_map_value->release());
      PROTOVM_RETURN(status_conversion);
    }

    auto& index_to_node =
        it->value->GetIf<Node::IndexedRepeatedField>()->index_to_node;
    auto status_or_it = MapInsert(&index_to_node, index_to_node.Size(),
                                  std::move(*status_or_map_value));
    if (!status_or_it.IsOk()) {
      allocator_->DeleteReferencedData(&message);
      PROTOVM_RETURN(status_or_it,
                     "Insert repeated field (id = %u, index = %d)", field.id(),
                     static_cast<int>(index_to_node.Size()));
    }
  }

  allocator_->DeleteReferencedData(node);
  node->value = message;

  return StatusOr<void>::Ok();
}

StatusOr<OwnedPtr<Node>> RwProtoCursor::CreateNodeFromField(
    protozero::Field field) {
  if (field.type() == protozero::proto_utils::ProtoWireType::kLengthDelimited) {
    auto status_or_bytes = allocator_->AllocateAndCopyBytes(field.as_bytes());
    PROTOVM_RETURN_IF_NOT_OK(status_or_bytes);
    auto status_or_node =
        allocator_->CreateNode<Node::Bytes>(std::move(*status_or_bytes));
    if (!status_or_node.IsOk()) {
      allocator_->DeleteReferencedData(&status_or_bytes.value());
      PROTOVM_RETURN(status_or_node);
    }

    return std::move(*status_or_node);
  }

  auto status_or_node =
      allocator_->CreateNode<Scalar>(field.type(), field.as_uint64());
  if (!status_or_node.IsOk()) {
    PROTOVM_RETURN(status_or_node);
  }

  return std::move(*status_or_node);
}

StatusOr<void> RwProtoCursor::ConvertToMappedRepeatedFieldIfNeeded(
    Node* node,
    uint32_t map_key_field_id) {
  if (node->GetIf<Node::MappedRepeatedField>()) {
    return StatusOr<void>::Ok();
  }

  if (node->GetIf<Node::Empty>()) {
    node->value = Node::MappedRepeatedField{};
    return StatusOr<void>::Ok();
  }

  // If the current node contains a message (either raw bytes or a decomposed
  // Node::Message), convert it into a Node::MappedRepeatedField.
  //
  // The process is:
  // 1. Extract the key (identified by 'map_key_field_id') from the message data
  // within the current 'node'.
  // 2. Allocate a new node. The original message content from the current node
  // is moved into this new node.
  // 3. Repurpose the current node, replacing it with an empty
  // Node::MappedRepeatedField
  //    (intrusive tree that stores message fields ordered by key).
  // 4. Insert the new node (containing Node::Message) into the repurposed
  // current node (Node::MappedRepeatedField) using the key extracted in step 1.
  if (node->GetIf<Node::Bytes>() || node->GetIf<Node::Message>()) {
    auto status_or_key = ReadScalarField(*node, map_key_field_id);
    PROTOVM_RETURN_IF_NOT_OK(status_or_key);

    auto status_or_map_value = allocator_->CreateNode<Node::Empty>();
    PROTOVM_RETURN_IF_NOT_OK(status_or_map_value);

    auto map_value = std::move(*status_or_map_value);
    map_value->value = std::move(node->value);
    node->value = Node::MappedRepeatedField{};

    auto status_or_it =
        MapInsert(&node->GetIf<Node::MappedRepeatedField>()->key_to_node,
                  *status_or_key, std::move(map_value));
    PROTOVM_RETURN(status_or_it);
  }

  // If the current node is a Node::IndexedRepeatedField (intrusive tree with
  // message fields ordered by index), this block converts it into a
  // Node::MappedRepeatedField (intrusive tree with message fields ordered by
  // key).
  //
  // The process is:
  // 1. Initialize an empty Node::MappedRepeatedField
  // 2. For each field in the current node (Node::IndexedRepeatedField):
  //    2.1. Extract the key (identified by 'map_key_field_id'). Note that the
  //         iterated fields must be messages.
  //    2.2. Remove the field from the current node
  //         (Node::IndexedRepeatedField).
  //    2.3. Insert the field into the Node::MappedRepeatedField using the key
  //         extracted in step 2.1.
  // 3. Assign the Node::MappedRepeatedField to the current node, i.e. repurpose
  //    current node from Node::IndexedRepeatedField to
  //    Node::MappedRepeatedField)
  if (auto* indexed = node->GetIf<Node::IndexedRepeatedField>()) {
    Node::MappedRepeatedField mapped_repeated_field;

    for (auto it = indexed->index_to_node.begin(); it;) {
      auto& map_entry = *it;
      auto& value_node = *it->value;

      it = indexed->index_to_node.Remove(it);

      auto status_or_key = ReadScalarField(value_node, map_key_field_id);
      PROTOVM_RETURN_IF_NOT_OK(status_or_key);

      map_entry.key = *status_or_key;
      mapped_repeated_field.key_to_node.Insert(map_entry);
    }

    node->value = mapped_repeated_field;

    return StatusOr<void>::Ok();
  }

  PROTOVM_ABORT(
      "Attempted to access field as MappedRepeatedField but node has type "
      "%s",
      node->GetTypeName());
}

StatusOr<void> RwProtoCursor::ConvertToIndexedRepeatedFieldIfNeeded(
    Node* node) {
  if (node->GetIf<Node::IndexedRepeatedField>()) {
    return StatusOr<void>::Ok();
  }

  if (node->GetIf<Node::MappedRepeatedField>()) {
    PROTOVM_ABORT(
        "Attempted \"convert to indexed repeated field\" operation but "
        "node has type %s",
        node->GetTypeName());
  }

  if (node->GetIf<Node::Empty>()) {
    node->value = Node::IndexedRepeatedField{};
    return StatusOr<void>::Ok();
  }

  auto status_or_map_value = allocator_->CreateNode<Node::Empty>();
  PROTOVM_RETURN_IF_NOT_OK(status_or_map_value);

  auto map_value = std::move(*status_or_map_value);
  map_value->value = std::move(node->value);
  node->value = Node::IndexedRepeatedField{};

  auto status_or_it =
      MapInsert(&node->GetIf<Node::IndexedRepeatedField>()->index_to_node, 0,
                std::move(map_value));
  PROTOVM_RETURN(status_or_it);
}

StatusOr<IntrusiveMap::Iterator> RwProtoCursor::FindOrCreateMessageField(
    Node* node,
    uint32_t field_id) {
  auto status = ConvertToMessageIfNeeded(node);
  PROTOVM_RETURN_IF_NOT_OK(status);

  auto* message = node->GetIf<Node::Message>();
  auto it = message->field_id_to_node.Find(field_id);
  if (it) {
    return it;
  }

  auto status_or_map_value = allocator_->CreateNode<Node::Empty>();
  PROTOVM_RETURN_IF_NOT_OK(status_or_map_value);

  auto status_or_it = MapInsert(&message->field_id_to_node, field_id,
                                std::move(*status_or_map_value));
  PROTOVM_RETURN(status_or_it);
}

StatusOr<IntrusiveMap::Iterator>
RwProtoCursor::FindOrCreateIndexedRepeatedField(Node* node, uint32_t index) {
  auto& index_to_node =
      node->GetIf<Node::IndexedRepeatedField>()->index_to_node;

  auto it = index_to_node.Find(index);
  if (it) {
    return it;
  }

  bool requires_creation_and_is_not_simple_append =
      index > index_to_node.Size();
  if (requires_creation_and_is_not_simple_append) {
    PROTOVM_ABORT(
        "Attempted to insert repeated field at arbitrary position (only "
        "append operation is supported)");
  }

  auto status_or_map_value = allocator_->CreateNode<Node::Empty>();
  PROTOVM_RETURN_IF_NOT_OK(status_or_map_value);

  return MapInsert(&node->GetIf<Node::IndexedRepeatedField>()->index_to_node,
                   index, std::move(*status_or_map_value));
}

StatusOr<IntrusiveMap::Iterator> RwProtoCursor::FindOrCreateMappedRepeatedField(
    Node* node,
    uint64_t key) {
  auto it = node->GetIf<Node::MappedRepeatedField>()->key_to_node.Find(key);

  if (it) {
    return it;
  }

  auto status_or_map_value = allocator_->CreateNode<Node::Empty>();
  PROTOVM_RETURN_IF_NOT_OK(status_or_map_value);

  return MapInsert(&node->GetIf<Node::MappedRepeatedField>()->key_to_node, key,
                   std::move(*status_or_map_value));
}

StatusOr<IntrusiveMap::Iterator> RwProtoCursor::MapInsert(
    IntrusiveMap* map,
    uint64_t key,
    OwnedPtr<Node> map_value) {
  auto status_or_map_node =
      allocator_->CreateNode<Node::MapNode>(key, std::move(map_value));
  if (!status_or_map_node.IsOk()) {
    allocator_->Delete(map_value.release());
    PROTOVM_RETURN(status_or_map_node, "Failed to allocate node");
  }

  auto [it, inserted] =
      map->Insert(*status_or_map_node->release()->GetIf<Node::MapNode>());
  if (!inserted) {
    allocator_->Delete(map_value.release());
    allocator_->Delete(status_or_map_node->release());
    PROTOVM_ABORT(
        "Failed to insert intrusive map entry (key = %d). Duplicated key?",
        static_cast<int>(key));
  }

  return it;
}

StatusOr<uint64_t> RwProtoCursor::ReadScalarField(const Node& node,
                                                  uint32_t field_id) {
  if (auto* bytes = node.GetIf<Node::Bytes>()) {
    protozero::ProtoDecoder decoder(protozero::ConstBytes{
        static_cast<const uint8_t*>(bytes->data.get()), bytes->size});

    auto field = decoder.ReadField();
    while (field.valid() && field.id() != field_id) {
      field = decoder.ReadField();
    }

    if (!field.valid()) {
      PROTOVM_ABORT(
          "Attempted to read scalar field (id=%u) but it is not present",
          field_id);
    }

    if (field.type() ==
        protozero::proto_utils::ProtoWireType::kLengthDelimited) {
      PROTOVM_ABORT("Attempted to length-delimited field (id=%u) as scalar",
                    field_id);
    }

    return field.as_uint64();
  }

  if (auto* message = node.GetIf<Node::Message>()) {
    auto it = message->field_id_to_node.Find(field_id);
    if (!it) {
      PROTOVM_ABORT(
          "Attempted to read scalar field (id=%u) but it is not present",
          field_id);
    }

    auto* scalar = it->value->GetIf<Scalar>();
    if (!scalar) {
      PROTOVM_ABORT(
          "Attempted to read scalar field (id=%u) from node with type %s",
          field_id, it->value->GetTypeName());
    }

    return scalar->value;
  }

  PROTOVM_ABORT(
      "Attempted to read scalar field (id=%u) but parent node has type %s",
      field_id, node.GetTypeName());
}

}  // namespace protovm
}  // namespace perfetto
