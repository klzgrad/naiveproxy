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

#ifndef SRC_PROTOVM_SCALAR_H_
#define SRC_PROTOVM_SCALAR_H_

#include "perfetto/protozero/proto_utils.h"

namespace perfetto {
namespace protovm {

struct Scalar {
  static Scalar Fixed32(uint32_t value) {
    return Scalar{protozero::proto_utils::ProtoWireType::kFixed32, value};
  }

  static Scalar Fixed64(uint64_t value) {
    return Scalar{protozero::proto_utils::ProtoWireType::kFixed64, value};
  }

  static Scalar VarInt(uint64_t value) {
    return Scalar{protozero::proto_utils::ProtoWireType::kVarInt, value};
  }

  bool operator==(const Scalar& other) const {
    return wire_type == other.wire_type && value == other.value;
  }

  bool operator!=(const Scalar& other) const { return !(*this == other); }

  protozero::proto_utils::ProtoWireType wire_type;
  uint64_t value;
};

}  // namespace protovm
}  // namespace perfetto

#endif  // SRC_PROTOVM_SCALAR_H_
