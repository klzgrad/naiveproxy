// Protocol Buffers - Google's data interchange format
// Copyright 2023 Google LLC.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "google/protobuf/compiler/rust/accessors/accessors.h"

#include <memory>

#include "absl/log/absl_log.h"
#include "google/protobuf/compiler/rust/accessors/accessor_case.h"
#include "google/protobuf/compiler/rust/accessors/generator.h"
#include "google/protobuf/compiler/rust/context.h"
#include "google/protobuf/compiler/rust/rust_field_type.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/port.h"

namespace google {
namespace protobuf {
namespace compiler {
namespace rust {

namespace {

std::unique_ptr<AccessorGenerator> AccessorGeneratorFor(
    Context& ctx, const FieldDescriptor& field) {
  // TODO: We do not support ctype=CORD fields or repeated
  // ctype=STRING_PIECE fields on cpp kernel yet (upb doesn't care about ctype).
  auto ctype = field.options().ctype();
  if (ctx.is_cpp() &&
      (ctype == FieldOptions::CORD || ctype == FieldOptions::STRING_PIECE) &&
      field.is_repeated()) {
    return std::make_unique<UnsupportedField>(
        "fields has an unsupported ctype");
  }

  if (field.is_map()) {
    return std::make_unique<Map>();
  }

  if (field.is_repeated()) {
    return std::make_unique<RepeatedField>();
  }

  switch (GetRustFieldType(field)) {
    case RustFieldType::INT32:
    case RustFieldType::INT64:
    case RustFieldType::UINT32:
    case RustFieldType::UINT64:
    case RustFieldType::FLOAT:
    case RustFieldType::DOUBLE:
    case RustFieldType::BOOL:
    case RustFieldType::ENUM:
      return std::make_unique<SingularScalar>();
    case RustFieldType::BYTES:
    case RustFieldType::STRING:
      if (ctype == FieldOptions::CORD) {
        return std::make_unique<SingularCord>();
      }
      return std::make_unique<SingularString>();
    case RustFieldType::MESSAGE:
      return std::make_unique<SingularMessage>();
  }

  ABSL_LOG(ERROR) << "Unknown field type: " << field.type();
  internal::Unreachable();
}

}  // namespace

void GenerateAccessorMsgImpl(Context& ctx, const FieldDescriptor& field,
                             AccessorCase accessor_case) {
  AccessorGeneratorFor(ctx, field)->GenerateMsgImpl(ctx, field, accessor_case);
}

void GenerateAccessorExternC(Context& ctx, const FieldDescriptor& field) {
  AccessorGeneratorFor(ctx, field)->GenerateExternC(ctx, field);
}

void GenerateAccessorThunkCc(Context& ctx, const FieldDescriptor& field) {
  AccessorGeneratorFor(ctx, field)->GenerateThunkCc(ctx, field);
}

}  // namespace rust
}  // namespace compiler
}  // namespace protobuf
}  // namespace google
