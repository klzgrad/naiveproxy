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

#ifndef SRC_PROTOVM_TEST_SAMPLE_PACKETS_H_
#define SRC_PROTOVM_TEST_SAMPLE_PACKETS_H_

#include "perfetto/protozero/message.h"
#include "perfetto/protozero/scattered_heap_buffer.h"

#include "src/protovm/test/protos/incremental_trace.pb.h"

namespace perfetto {
namespace protovm {
namespace test {

class SamplePackets {
 public:
  static protos::TraceEntry TraceEntryWithOneElement() {
    protos::TraceEntry entry;

    auto* element0 = entry.add_elements();
    element0->set_id(0);
    element0->set_value(10);
    element0->set_value_fixed32(32);
    element0->set_value_fixed64(64);

    return entry;
  }

  static protos::TraceEntry TraceEntryWithTwoElements() {
    protos::TraceEntry entry;

    auto* element0 = entry.add_elements();
    *element0 = TraceEntryWithOneElement().elements(0);

    auto* element1 = entry.add_elements();
    element1->set_id(1);
    element1->set_value(11);

    return entry;
  }

  static protos::Patch PatchWithInitialState() {
    protos::Patch patch;

    auto* element10 = patch.add_elements_to_set();
    element10->set_id(0);
    element10->set_value(10);

    auto* element11 = patch.add_elements_to_set();
    element11->set_id(1);
    element11->set_value(11);

    return patch;
  }

  static protos::Patch PatchWithDelOperation() {
    protos::Patch patch;
    patch.add_elements_to_delete(0);
    return patch;
  }

  static protos::Patch PatchWithMergeOperation1() {
    protos::Patch patch;

    auto* element0 = patch.add_elements_to_merge();
    element0->set_id(0);
    element0->set_value(10);

    return patch;
  }

  static protos::Patch PatchWithMergeOperation2() {
    protos::Patch patch;

    auto* element0 = patch.add_elements_to_merge();
    element0->set_id(0);
    element0->set_value(100);

    auto* element1 = patch.add_elements_to_merge();
    element1->set_id(1);
    element1->set_value(101);

    return patch;
  }

  static protos::Patch PatchWithSetOperation() {
    protos::Patch patch;

    auto* element0 = patch.add_elements_to_set();
    element0->set_id(0);

    auto* element1 = patch.add_elements_to_set();
    element1->set_id(1);
    element1->set_value(101);

    return patch;
  }

  static std::string PatchInconsistentWithIncrementalTraceProgram() {
    protozero::HeapBuffered<protozero::Message> proto;

    auto* element_to_set = proto.get()->BeginNestedMessage<protozero::Message>(
        protos::Patch::kElementsToSetFieldNumber);

    // The VM program will access the field elements_to_set[0].id expecting to
    // find a scalar but here we are setting it as a length-delimited field
    // (incompatible wire type), thus causing an abort.
    std::string_view random_invalid_data = "random invalid data";
    element_to_set->AppendBytes(protos::Element::kIdFieldNumber,
                                random_invalid_data.data(),
                                random_invalid_data.size());

    return proto.SerializeAsString();
  }
};

}  // namespace test
}  // namespace protovm
}  // namespace perfetto

#endif  // SRC_PROTOVM_TEST_SAMPLE_PACKETS_H_
