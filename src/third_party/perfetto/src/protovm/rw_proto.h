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

#ifndef SRC_PROTOVM_RW_PROTO_H_
#define SRC_PROTOVM_RW_PROTO_H_

#include "perfetto/protozero/message.h"

#include "src/protovm/allocator.h"
#include "src/protovm/node.h"
#include "src/protovm/rw_proto_cursor.h"

namespace perfetto {
namespace protovm {

// RwProto provides an API to create and manipulate protobuf messages without
// requiring prior knowledge of the schema. Meaning that protobuf messages can
// be dynamically built on-the-fly, without pre-compiled .proto definitions.
//
// Key features:
//
// - Schema-agnostic: RwProto dynamically learns the schema as fields and
//   messages are added.
//
// - Tree-like representation: protobuf messages are represented internally as a
//   tree of nodes, mirroring the nested structure of protobufs. This tree can
//   be traversed and manipulated using a Cursor object.
//
// - Fine-grained Memory Management: a centralized allocator is used to manage
//   memory for nodes, ensuring good data locality and fine-grained control over
//   memory usage.
//
// Overview of RwProto's internal node structure:
//
//                             ┌─────────┐
//                             │         │
//                             │ Message │
//                             │         │ Map<field_id, OwnedPtr<Node>>
//                             └─┬┬┬┬┬┬──┘
//                               │││││└────────────────────────────────┐
//                               ││││└───────────────────────┐         │
//               ┌───────────────┘││└──────────────┐         │         │
//               │            ┌───┘└────┐          │         │         │
//               │            │    ┌────▼────┐ ┌───▼───┐ ┌───▼───┐ ┌───▼────┐
//               │            │    │         │ │       │ │       │ │        │
//               │            │    │ Message │ │ Empty │ │ Bytes │ │ Scalar │
//               │            │    │         │ │       │ │       │ │        │
//               │            │    └────┬────┘ └───────┘ └───────┘ └────────┘
//               │            │         ▼
//               │            │        ...
//               │            │
//               │            │
//               │   *********▼********
//               │   *                *
//               │   * Mapped         *
//               │   * RepeatedField  *
//               │   *                *
//               │   ******************  Map<key, OwnedPtr<Node>>
//               │           │ │
//               │       ┌───┘ └────┐
//               │       │          │
//               │  ┌────▼────┐ ┌───▼───┐
//               │  │         │ │       │
//               │  │ Message │ │ Bytes │
//               │  │         │ │       │
//               │  └────┬────┘ └───────┘
//               │       ▼
//               │      ...
//               │
//
//       ********▼*********
//       *                *
//       * Indexed        *
//       * RepeatedField  *
//       *                *
//       ******************  Map<index, OwnedPtr<Node>>
//             ││││
//      ┌──────┘││└────────────────────┐
//      │       │└───────────┐         │
//      │       └──┐         │         │
//      │          │         │         │
// ┌────▼────┐ ┌───▼───┐ ┌───▼───┐ ┌───▼────┐
// │         │ │       │ │       │ │        │
// │ Message │ │ Empty │ │ Bytes │ │ Scalar │
// │         │ │       │ │       │ │        │
// └────┬────┘ └───────┘ └───────┘ └────────┘
//      ▼
//     ...
//
//
// ┌─┐
// │ │  Data node: corresponds to an actual field/message in the proto schema
// └─┘             and eventually becomes part of the final serialized proto.
//
// ***
// * *  Structural node: used to organize and provide access to message fields.
// ***                   It is an internal helper and doesn't appear in the
//                       final serialized proto.
//
//
// Message: Represents a protobuf message. It maps field_id to corresponding
//          node/value. It allows accessing fields as well as adding and
//          removing them.
//
//
// Empty: Represents an empty or uninitialized node. It is used as a
//        placeholder before a node's type is determined. E.g. between an
//        "enter field" and a "set field value" operation.
//
//
// Bytes: Stores a sequence of bytes, typically representing a length-delimited
//        protobuf field.
//
//
// Scalar: Stores a scalar value.
//
//
// IndexedRepeatedField: Represents a protobuf repeated field where elements
//                       are accessed by index.
//
//
// MappedRepeatedField: Represents a protobuf repeated field where elements
//                      are accessed by key.
class RwProto {
 public:
  using Cursor = RwProtoCursor;

  explicit RwProto(Allocator* allocator);
  ~RwProto();
  Cursor GetRoot();
  std::string SerializeAsString() const;

 private:
  void SerializeField(uint32_t field_id,
                      const Node& node,
                      protozero::Message* proto) const;

  Allocator* allocator_{nullptr};
  Node root_;
};

}  // namespace protovm
}  // namespace perfetto

#endif  // SRC_PROTOVM_RW_PROTO_H_
