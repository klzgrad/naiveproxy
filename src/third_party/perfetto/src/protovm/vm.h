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

#ifndef SRC_PROTOVM_VM_H_
#define SRC_PROTOVM_VM_H_

#include <memory>

#include "perfetto/protozero/field.h"

#include "src/protovm/error_handling.h"
#include "src/protovm/executor.h"
#include "src/protovm/parser.h"

namespace perfetto {
namespace protovm {

// A VM that executes programs defined by data sources at registration time.
// Used by traced to apply patches (packets overwritten in the ring buffer) to
// an incremental state packet, thus allowing efficient incremental tracing of
// Layers/Windows/Views without requiring periodic invalidation and achieve
// perfect interning.
//
// Overview of the VM's architecture and interactions:
//
//
//         ***********
//         *         *
//         * Program *
//         *         *
//         ***********
//              │
//              │
//              │
//          ┌───┴──┐            ┌────────┐
//          │      │            │        │
//          │Parser├────────────┤Executor│
//          │      │            │        │
//          └──────┘            └─┬────┬─┘
//                                │    │
//                          ┌─────┘    └─────┐
//                          │                │
//                          │                │
//                      ┌───┴────┐       ┌───┴────┐
//                      │        │       │        │
//                      │RoCursor│       │RwProto │
//                      │        │       │::Cursor│
//                      └────┬───┘       │        │
//                           │           └───┬────┘
//                           │               │
//                           │               │
//                           │               │
//                      *********      ***************
//                      *       *      *             *
//                      * Patch *      * Incremental *
//                      *       *      *    state    *
//                      *********      *             *
//                                     ***************
//
//  ┌─┐
//  │ │  VM's component
//  └─┘
//
//  ***
//  * *  Data
//  ***
//
//
// Parser: Understands the instructions semantic and controls the program flow.
//         It delegates to the Executor operations like navigating through the
//         patch and incremental state data, reading values, and manipulating
//         fields.
//
//
// Executor: Thin glue layer that mainly forwards data back and forth between
//           the Parser and cursors. Mainly useful for testing, as it can
//           be easily mocked and allows to test the Parser in isolation.
//
//
// RoCursor: Provides read-only access to the incoming data (the patch) to be
//           applied. It allows to traverse the proto message structure of the
//           patch, iterating over fields and extracting field values.
//
//
// RwProto::Cursor: Provides read-write access to the incremental state. It
//                  allows traversing the proto message structure of the
//                  incremental state, as well as deleting/inserting/merging
//                  fields.
class Vm {
 public:
  Vm(protozero::ConstBytes program,
     size_t memory_limit_bytes,
     protozero::ConstBytes initial_incremental_state = {nullptr, 0});
  StatusOr<void> ApplyPatch(protozero::ConstBytes packet);
  void SerializeIncrementalState(protozero::Message*) const;
  std::string SerializeProgram() const;
  std::unique_ptr<Vm> CloneReadOnly() const;
  uint64_t GetMemoryUsageBytes() const;

 private:
  struct ReadWriteState {
    ReadWriteState(std::string& program,
                   size_t memory_limit_bytes,
                   protozero::ConstBytes initial_incremental_state)
        : executor{},
          parser{protozero::ConstBytes{
                     reinterpret_cast<const uint8_t*>(program.data()),
                     program.size()},
                 &executor},
          allocator{memory_limit_bytes},
          incremental_state{&allocator} {
      if (initial_incremental_state.data) {
        incremental_state.GetRoot().SetBytes(initial_incremental_state);
      }
    }

    Executor executor;
    Parser parser;
    Allocator allocator;
    RwProto incremental_state;
  };

  struct ReadOnlyState {
    explicit ReadOnlyState(std::string incremental_state)
        : serialized_incremental_state(std::move(incremental_state)) {}

    std::string serialized_incremental_state;
  };

  // Constructor used to produce read-only copies of the ProtoVm.
  // Private to avoid unintended copies. It should be used only for
  // CloneReadOnly().
  Vm(const Vm&);

  // Constructor used only for cloning
  explicit Vm(std::string incremental_state);

  std::string SerializeIncrementalStateAsString() const;

  std::string owned_program_;
  std::variant<ReadWriteState, ReadOnlyState> state_;
};

}  // namespace protovm
}  // namespace perfetto

#endif  // SRC_PROTOVM_VM_H_
