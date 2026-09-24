/*
 * Copyright (C) 2026 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_TYPED_PROTO_FIELD_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_TYPED_PROTO_FIELD_H_

#include <stdint.h>

#include <type_traits>

#include "perfetto/base/logging.h"
#include "perfetto/protozero/field.h"
#include "perfetto/protozero/proto_utils.h"

namespace perfetto::trace_processor {

// Wraps a single field of a protozero message (e.g. the TracePacket field a
// module registered for, or an out-of-tree extension field of any extended
// message). Values are accessed in a typed way via the generated field
// metadata constant, e.g.:
//
//   field.Cast<TracePacket::kProcessTree>()               // -> ConstBytes
//   field.Cast<FrameworksBaseTracePacket::kVideoFrame>()  // -> ConstBytes
//
// so the returned type always matches the field's schema and casting to the
// wrong field id is a programming error (DCHECK).
class TypedProtoField {
 public:
  explicit TypedProtoField(protozero::Field field) : field_(field) {}

  uint32_t id() const { return field_.id(); }
  bool valid() const { return field_.valid(); }

  template <const auto& kFieldMetadata>
  auto Cast() const {
    using FieldMetadata =
        std::remove_const_t<std::remove_reference_t<decltype(kFieldMetadata)>>;
    static_assert(std::is_base_of_v<protozero::proto_utils::FieldMetadataBase,
                                    FieldMetadata>,
                  "Cast() takes a generated field metadata constant, e.g. "
                  "TracePacket::kProcessTree");
    PERFETTO_DCHECK(field_.id() ==
                    static_cast<uint32_t>(FieldMetadata::kFieldId));
    using ST = protozero::proto_utils::ProtoSchemaType;
    constexpr ST kType = FieldMetadata::kProtoFieldType;
    if constexpr (kType == ST::kMessage || kType == ST::kBytes) {
      return field_.as_bytes();
    } else if constexpr (kType == ST::kString) {
      return field_.as_string();
    } else if constexpr (kType == ST::kBool) {
      return field_.as_bool();
    } else if constexpr (kType == ST::kInt32 || kType == ST::kSfixed32 ||
                         kType == ST::kEnum) {
      return field_.as_int32();
    } else if constexpr (kType == ST::kSint32) {
      return field_.as_sint32();
    } else if constexpr (kType == ST::kInt64 || kType == ST::kSfixed64) {
      return field_.as_int64();
    } else if constexpr (kType == ST::kSint64) {
      return field_.as_sint64();
    } else if constexpr (kType == ST::kUint32 || kType == ST::kFixed32) {
      return field_.as_uint32();
    } else if constexpr (kType == ST::kUint64 || kType == ST::kFixed64) {
      return field_.as_uint64();
    } else if constexpr (kType == ST::kFloat) {
      return field_.as_float();
    } else if constexpr (kType == ST::kDouble) {
      return field_.as_double();
    } else {
      static_assert(kType != kType, "Unsupported proto schema type");
    }
  }

 private:
  protozero::Field field_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_TYPED_PROTO_FIELD_H_
