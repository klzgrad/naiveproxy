/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "src/trace_redaction/proto_util.h"

#include "perfetto/protozero/field.h"
#include "perfetto/protozero/message.h"

namespace perfetto::trace_redaction {
namespace proto_util {

// This is copied from "src/protozero/field.cc", but was modified to use the
// serialization methods provided in "perfetto/protozero/message.h".
void AppendField(const protozero::Field& field, protozero::Message* message) {
  auto id = field.id();
  auto type = field.type();

  switch (type) {
    case protozero::proto_utils::ProtoWireType::kVarInt: {
      message->AppendVarInt(id, field.raw_int_value());
      return;
    }

    case protozero::proto_utils::ProtoWireType::kFixed32: {
      message->AppendFixed(id, field.as_uint32());
      return;
    }

    case protozero::proto_utils::ProtoWireType::kFixed64: {
      message->AppendFixed(id, field.as_uint64());
      return;
    }

    case protozero::proto_utils::ProtoWireType::kLengthDelimited: {
      message->AppendBytes(id, field.data(), field.size());
      return;
    }
  }

  // A switch-statement would be preferred, but when using a switch statement,
  // it complains that about case coverage.
  PERFETTO_FATAL("Unknown field type %u", static_cast<uint8_t>(type));
}

}  // namespace proto_util
}  // namespace perfetto::trace_redaction
