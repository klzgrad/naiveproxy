/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "src/trace_processor/metrics/metrics.h"

#include <algorithm>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/protozero/field.h"
#include "perfetto/protozero/proto_utils.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_engine.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_type.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"
#include "src/trace_processor/sqlite/sql_source.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"
#include "src/trace_processor/tp_metatrace.h"
#include "src/trace_processor/util/descriptors.h"

#include "protos/perfetto/common/descriptor.pbzero.h"
#include "protos/perfetto/trace_processor/metatrace_categories.pbzero.h"
#include "protos/perfetto/trace_processor/metrics_impl.pbzero.h"

namespace perfetto::trace_processor::metrics {

namespace {

base::StatusOr<protozero::ConstBytes> ValidateSingleNonEmptyMessage(
    const uint8_t* ptr,
    size_t size,
    uint32_t schema_type,
    const std::string& message_type) {
  PERFETTO_DCHECK(size > 0);

  if (size > protozero::proto_utils::kMaxMessageLength) {
    return base::ErrStatus(
        "Message has size %zu which is larger than the maximum allowed message "
        "size %zu",
        size, protozero::proto_utils::kMaxMessageLength);
  }

  protos::pbzero::ProtoBuilderResult::Decoder decoder(ptr, size);
  if (decoder.is_repeated()) {
    return base::ErrStatus("Cannot handle nested repeated messages");
  }

  const auto& single_field = decoder.single();
  protos::pbzero::SingleBuilderResult::Decoder single(single_field.data,
                                                      single_field.size);

  if (single.type() != schema_type) {
    return base::ErrStatus("Message field has wrong wire type %u",
                           single.type());
  }

  base::StringView actual_type(single.type_name());
  if (actual_type != base::StringView(message_type)) {
    return base::ErrStatus("Field has wrong type (expected %s, was %s)",
                           message_type.c_str(),
                           actual_type.ToStdString().c_str());
  }

  if (!single.has_protobuf()) {
    return base::ErrStatus("Message has no proto bytes");
  }

  // We disallow 0 size fields here as they should have been reported as null
  // one layer down.
  if (single.protobuf().size == 0) {
    return base::ErrStatus("Field has zero size");
  }
  return single.protobuf();
}

}  // namespace

ProtoBuilder::ProtoBuilder(const DescriptorPool* pool,
                           const ProtoDescriptor* descriptor)
    : pool_(pool), descriptor_(descriptor) {}

base::Status ProtoBuilder::AppendSqlValue(const std::string& field_name,
                                          const SqlValue& value) {
  base::StatusOr<const FieldDescriptor*> desc = FindFieldByName(field_name);
  RETURN_IF_ERROR(desc.status());
  switch (value.type) {
    case SqlValue::kLong:
      if (desc.value()->is_repeated()) {
        return base::ErrStatus(
            "Unexpected long value for repeated field %s in proto type %s",
            field_name.c_str(), descriptor_->full_name().c_str());
      }
      return AppendSingleLong(**desc, value.long_value);
    case SqlValue::kDouble:
      if (desc.value()->is_repeated()) {
        return base::ErrStatus(
            "Unexpected double value for repeated field %s in proto type %s",
            field_name.c_str(), descriptor_->full_name().c_str());
      }
      return AppendSingleDouble(**desc, value.double_value);
    case SqlValue::kString:
      if (desc.value()->is_repeated()) {
        return base::ErrStatus(
            "Unexpected string value for repeated field %s in proto type %s",
            field_name.c_str(), descriptor_->full_name().c_str());
      }
      return AppendSingleString(**desc, value.string_value);
    case SqlValue::kBytes: {
      const auto* ptr = static_cast<const uint8_t*>(value.bytes_value);
      size_t size = value.bytes_count;
      if (desc.value()->is_repeated()) {
        return AppendRepeated(**desc, ptr, size);
      }
      return AppendSingleBytes(**desc, ptr, size);
    }
    case SqlValue::kNull:
      // If the value is null, it's treated as the field being absent so we
      // don't append anything.
      return base::OkStatus();
  }
  PERFETTO_FATAL("For GCC");
}

base::Status ProtoBuilder::AppendSingleLong(const FieldDescriptor& field,
                                            int64_t value) {
  using FieldDescriptorProto = protos::pbzero::FieldDescriptorProto;
  switch (field.type()) {
    case FieldDescriptorProto::TYPE_INT32:
    case FieldDescriptorProto::TYPE_INT64:
    case FieldDescriptorProto::TYPE_UINT32:
    case FieldDescriptorProto::TYPE_BOOL:
      message_->AppendVarInt(field.number(), value);
      break;
    case FieldDescriptorProto::TYPE_ENUM: {
      auto opt_enum_descriptor_idx =
          pool_->FindDescriptorIdx(field.resolved_type_name());
      if (!opt_enum_descriptor_idx) {
        return base::ErrStatus(
            "Unable to find enum type %s to fill field %s (in proto message "
            "%s)",
            field.resolved_type_name().c_str(), field.name().c_str(),
            descriptor_->full_name().c_str());
      }
      const auto& enum_desc = pool_->descriptors()[*opt_enum_descriptor_idx];
      auto opt_enum_str = enum_desc.FindEnumString(static_cast<int32_t>(value));
      if (!opt_enum_str) {
        return base::ErrStatus("Invalid enum value %" PRId64
                               " "
                               "in enum type %s; encountered while filling "
                               "field %s (in proto message %s)",
                               value, field.resolved_type_name().c_str(),
                               field.name().c_str(),
                               descriptor_->full_name().c_str());
      }
      message_->AppendVarInt(field.number(), value);
      break;
    }
    case FieldDescriptorProto::TYPE_SINT32:
    case FieldDescriptorProto::TYPE_SINT64:
      message_->AppendSignedVarInt(field.number(), value);
      break;
    case FieldDescriptorProto::TYPE_FIXED32:
    case FieldDescriptorProto::TYPE_SFIXED32:
    case FieldDescriptorProto::TYPE_FIXED64:
    case FieldDescriptorProto::TYPE_SFIXED64:
      message_->AppendFixed(field.number(), value);
      break;
    case FieldDescriptorProto::TYPE_UINT64:
      return base::ErrStatus(
          "Field %s (in proto message %s) is using a uint64 type. uint64 in "
          "metric messages is not supported by trace processor; use an int64 "
          "field instead.",
          field.name().c_str(), descriptor_->full_name().c_str());
    default: {
      return base::ErrStatus(
          "Tried to write value of type long into field %s (in proto type %s) "
          "which has type %u",
          field.name().c_str(), descriptor_->full_name().c_str(), field.type());
    }
  }
  return base::OkStatus();
}

base::Status ProtoBuilder::AppendSingleDouble(const FieldDescriptor& field,
                                              double value) {
  using FieldDescriptorProto = protos::pbzero::FieldDescriptorProto;
  switch (field.type()) {
    case FieldDescriptorProto::TYPE_FLOAT:
    case FieldDescriptorProto::TYPE_DOUBLE: {
      if (field.type() == FieldDescriptorProto::TYPE_FLOAT) {
        message_->AppendFixed(field.number(), static_cast<float>(value));
      } else {
        message_->AppendFixed(field.number(), value);
      }
      break;
    }
    default: {
      return base::ErrStatus(
          "Tried to write value of type double into field %s (in proto type "
          "%s) which has type %u",
          field.name().c_str(), descriptor_->full_name().c_str(), field.type());
    }
  }
  return base::OkStatus();
}

base::Status ProtoBuilder::AppendSingleString(const FieldDescriptor& field,
                                              base::StringView data) {
  using FieldDescriptorProto = protos::pbzero::FieldDescriptorProto;
  switch (field.type()) {
    case FieldDescriptorProto::TYPE_STRING: {
      message_->AppendBytes(field.number(), data.data(), data.size());
      break;
    }
    case FieldDescriptorProto::TYPE_ENUM: {
      auto opt_enum_descriptor_idx =
          pool_->FindDescriptorIdx(field.resolved_type_name());
      if (!opt_enum_descriptor_idx) {
        return base::ErrStatus(
            "Unable to find enum type %s to fill field %s (in proto message "
            "%s)",
            field.resolved_type_name().c_str(), field.name().c_str(),
            descriptor_->full_name().c_str());
      }
      const auto& enum_desc = pool_->descriptors()[*opt_enum_descriptor_idx];
      std::string enum_str = data.ToStdString();
      auto opt_enum_value = enum_desc.FindEnumValue(enum_str);
      if (!opt_enum_value) {
        return base::ErrStatus(
            "Invalid enum string %s "
            "in enum type %s; encountered while filling "
            "field %s (in proto message %s)",
            enum_str.c_str(), field.resolved_type_name().c_str(),
            field.name().c_str(), descriptor_->full_name().c_str());
      }
      message_->AppendVarInt(field.number(), *opt_enum_value);
      break;
    }
    default: {
      return base::ErrStatus(
          "Tried to write value of type string into field %s (in proto type "
          "%s) which has type %u",
          field.name().c_str(), descriptor_->full_name().c_str(), field.type());
    }
  }
  return base::OkStatus();
}

base::Status ProtoBuilder::AppendSingleBytes(const FieldDescriptor& field,
                                             const uint8_t* ptr,
                                             size_t size) {
  using FieldDescriptorProto = protos::pbzero::FieldDescriptorProto;
  if (field.type() == FieldDescriptorProto::TYPE_MESSAGE) {
    // If we have an zero sized bytes, we still want to propagate that the field
    // message was set but empty.
    if (size == 0) {
      // ptr can be null and passing nullptr to AppendBytes feels dangerous so
      // just pass an empty string (which will have a valid pointer always) and
      // zero as the size.
      message_->AppendBytes(field.number(), "", 0);
      return base::OkStatus();
    }

    base::StatusOr<protozero::ConstBytes> bytes = ValidateSingleNonEmptyMessage(
        ptr, size, field.type(), field.resolved_type_name());
    if (!bytes.ok()) {
      return base::ErrStatus(
          "[Field %s in message %s]: %s", field.name().c_str(),
          descriptor_->full_name().c_str(), bytes.status().c_message());
    }
    message_->AppendBytes(field.number(), bytes->data, bytes->size);
    return base::OkStatus();
  }

  if (size == 0) {
    return base::ErrStatus(
        "Tried to write zero-sized value into field %s (in proto type "
        "%s). Nulls are only supported for message protos; all other types "
        "should ensure that nulls are not passed to proto builder functions by "
        "using the SQLite IFNULL/COALESCE functions.",
        field.name().c_str(), descriptor_->full_name().c_str());
  }

  return base::ErrStatus(
      "Tried to write value of type bytes into field %s (in proto type %s) "
      "which has type %u",
      field.name().c_str(), descriptor_->full_name().c_str(), field.type());
}

base::Status ProtoBuilder::AppendRepeated(const FieldDescriptor& field,
                                          const uint8_t* ptr,
                                          size_t size) {
  PERFETTO_DCHECK(field.is_repeated());

  if (size > protozero::proto_utils::kMaxMessageLength) {
    return base::ErrStatus(
        "Message passed to field %s in proto message %s has size %zu which is "
        "larger than the maximum allowed message size %zu",
        field.name().c_str(), descriptor_->full_name().c_str(), size,
        protozero::proto_utils::kMaxMessageLength);
  }

  protos::pbzero::ProtoBuilderResult::Decoder decoder(ptr, size);
  if (!decoder.is_repeated()) {
    return base::ErrStatus(
        "Unexpected message value for repeated field %s in proto type %s",
        field.name().c_str(), descriptor_->full_name().c_str());
  }

  protos::pbzero::RepeatedBuilderResult::Decoder repeated(decoder.repeated());
  bool parse_error = false;
  if (repeated.has_int_values()) {
    for (auto it = repeated.int_values(&parse_error); it; ++it) {
      RETURN_IF_ERROR(AppendSingleLong(field, *it));
    }
  } else if (repeated.has_double_values()) {
    for (auto it = repeated.double_values(&parse_error); it; ++it) {
      RETURN_IF_ERROR(AppendSingleDouble(field, *it));
    }
  } else if (repeated.has_string_values()) {
    for (auto it = repeated.string_values(); it; ++it) {
      RETURN_IF_ERROR(AppendSingleString(field, *it));
    }
  } else if (repeated.has_byte_values()) {
    for (auto it = repeated.byte_values(); it; ++it) {
      RETURN_IF_ERROR(AppendSingleBytes(field, (*it).data, (*it).size));
    }
  } else {
    return base::ErrStatus("Unknown type in repeated field");
  }
  return parse_error
             ? base::ErrStatus("Failed to parse repeated field internal proto")
             : base::OkStatus();
}

std::vector<uint8_t> ProtoBuilder::SerializeToProtoBuilderResult() {
  std::vector<uint8_t> serialized = SerializeRaw();
  if (serialized.empty()) {
    return serialized;
  }

  const auto& type_name = descriptor_->full_name();

  protozero::HeapBuffered<protos::pbzero::ProtoBuilderResult> result;
  result->set_is_repeated(false);

  auto* single = result->set_single();
  single->set_type(protos::pbzero::FieldDescriptorProto::Type::TYPE_MESSAGE);
  single->set_type_name(type_name.c_str(), type_name.size());
  single->set_protobuf(serialized.data(), serialized.size());
  return result.SerializeAsArray();
}

std::vector<uint8_t> ProtoBuilder::SerializeRaw() {
  return message_.SerializeAsArray();
}

base::StatusOr<const FieldDescriptor*> ProtoBuilder::FindFieldByName(
    const std::string& field_name) {
  const auto* field = descriptor_->FindFieldByName(field_name);
  if (!field) {
    return base::ErrStatus("Field with name %s not found in proto type %s",
                           field_name.c_str(),
                           descriptor_->full_name().c_str());
  }
  return field;
}

RepeatedFieldBuilder::RepeatedFieldBuilder() {
  repeated_ = message_->set_repeated();
}

base::Status RepeatedFieldBuilder::AddSqlValue(SqlValue value) {
  switch (value.type) {
    case SqlValue::kLong:
      return AddLong(value.long_value);
    case SqlValue::kDouble:
      return AddDouble(value.double_value);
    case SqlValue::kString:
      return AddString(value.string_value);
    case SqlValue::kBytes:
      return AddBytes(static_cast<const uint8_t*>(value.bytes_value),
                      value.bytes_count);
    case SqlValue::kNull:
      return AddBytes(nullptr, 0);
  }
  PERFETTO_FATAL("Unknown SqlValue type");
}

std::vector<uint8_t> RepeatedFieldBuilder::SerializeToProtoBuilderResult() {
  if (!repeated_field_type_) {
    return {};
  }
  {
    if (repeated_field_type_ == SqlValue::Type::kDouble) {
      repeated_->set_double_values(double_packed_repeated_);
    } else if (repeated_field_type_ == SqlValue::Type::kLong) {
      repeated_->set_int_values(int64_packed_repeated_);
    }
    repeated_->Finalize();
    repeated_ = nullptr;
  }
  message_->set_is_repeated(true);
  return message_.SerializeAsArray();
}

base::Status RepeatedFieldBuilder::AddLong(int64_t value) {
  RETURN_IF_ERROR(EnsureType(SqlValue::Type::kLong));
  int64_packed_repeated_.Append(value);
  return base::OkStatus();
}

base::Status RepeatedFieldBuilder::AddDouble(double value) {
  RETURN_IF_ERROR(EnsureType(SqlValue::Type::kDouble));
  double_packed_repeated_.Append(value);
  return base::OkStatus();
}

base::Status RepeatedFieldBuilder::AddString(base::StringView value) {
  RETURN_IF_ERROR(EnsureType(SqlValue::Type::kString));
  repeated_->add_string_values(value.data(), value.size());
  return base::OkStatus();
}

base::Status RepeatedFieldBuilder::AddBytes(const uint8_t* data, size_t size) {
  RETURN_IF_ERROR(EnsureType(SqlValue::Type::kBytes));
  repeated_->add_byte_values(data, size);
  return base::OkStatus();
}

base::Status RepeatedFieldBuilder::EnsureType(SqlValue::Type type) {
  if (repeated_field_type_ && repeated_field_type_ != type) {
    return base::ErrStatus(
        "Inconsistent type in RepeatedField: was %s but now seen value %s",
        sqlite::utils::SqliteTypeToFriendlyString(*repeated_field_type_),
        sqlite::utils::SqliteTypeToFriendlyString(type));
  }
  repeated_field_type_ = type;
  return base::OkStatus();
}

int TemplateReplace(
    const std::string& raw_text,
    const std::unordered_map<std::string, std::string>& substitutions,
    std::string* out) {
  std::regex re(R"(\{\{\s*(\w*)\s*\}\})", std::regex_constants::ECMAScript);

  auto it = std::sregex_iterator(raw_text.begin(), raw_text.end(), re);
  auto regex_end = std::sregex_iterator();
  auto start = raw_text.begin();
  for (; it != regex_end; ++it) {
    out->insert(out->end(), start, raw_text.begin() + it->position(0));

    auto value_it = substitutions.find(it->str(1));
    if (value_it == substitutions.end()) {
      return 1;
    }

    const auto& value = value_it->second;
    std::copy(value.begin(), value.end(), std::back_inserter(*out));
    start = raw_text.begin() + it->position(0) + it->length(0);
  }
  out->insert(out->end(), start, raw_text.end());
  return 0;
}

void NullIfEmpty::Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
  PERFETTO_DCHECK(argc == 1);

  switch (sqlite::value::Type(argv[0])) {
    case sqlite::Type::kNull:
      return sqlite::utils::ReturnNullFromFunction(ctx);
    case sqlite::Type::kBlob: {
      if (sqlite::value::Bytes(argv[0]) == 0) {
        return sqlite::utils::ReturnNullFromFunction(ctx);
      }
      return sqlite::result::TransientBytes(ctx, sqlite::value::Blob(argv[0]),
                                            sqlite::value::Bytes(argv[0]));
    }
    case sqlite::Type::kInteger:
    case sqlite::Type::kFloat:
    case sqlite::Type::kText:
      return sqlite::utils::SetError(
          ctx, "NULL_IF_EMPTY: should only be called with bytes argument");
  }
}

void RepeatedField::Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
  if (argc != 1) {
    sqlite::result::Error(ctx, "RepeatedField: only expected one arg");
    return;
  }

  // We use a double indirection here so we can use new and delete without
  // needing to do dangerous dances with placement new and checking
  // initialization.
  auto** builder_ptr_ptr = static_cast<RepeatedFieldBuilder**>(
      sqlite3_aggregate_context(ctx, sizeof(RepeatedFieldBuilder*)));

  // The memory returned from sqlite3_aggregate_context is zeroed on its first
  // invocation so *builder_ptr_ptr will be nullptr on the first invocation of
  // RepeatedFieldStep.
  bool needs_init = *builder_ptr_ptr == nullptr;
  if (needs_init) {
    *builder_ptr_ptr = new RepeatedFieldBuilder();
  }

  auto value = sqlite::utils::SqliteValueToSqlValue(argv[0]);
  RepeatedFieldBuilder* builder = *builder_ptr_ptr;
  auto status = builder->AddSqlValue(value);
  if (!status.ok()) {
    sqlite::result::Error(ctx, status.c_message());
  }
}

void RepeatedField::Final(sqlite3_context* ctx) {
  // Note: we choose the size intentionally to be zero because we don't want to
  // allocate if the Step has never been called.
  auto** builder_ptr_ptr =
      static_cast<RepeatedFieldBuilder**>(sqlite3_aggregate_context(ctx, 0));

  // If Step has never been called, |builder_ptr_ptr| will be null.
  if (builder_ptr_ptr == nullptr) {
    sqlite::result::Null(ctx);
    return;
  }

  // Capture the context pointer so that it will be freed at the end of this
  // function.
  std::unique_ptr<RepeatedFieldBuilder> builder(*builder_ptr_ptr);
  std::vector<uint8_t> raw = builder->SerializeToProtoBuilderResult();
  if (raw.empty()) {
    sqlite::result::Null(ctx);
    return;
  }

  return sqlite::result::TransientBytes(ctx, raw.data(),
                                        static_cast<int>(raw.size()));
}

// SQLite function implementation used to build a proto directly in SQL. The
// proto to be built is given by the descriptor which is given as a context
// parameter to this function and chosen when this function is first registed
// with SQLite. The args of this function are key value pairs specifying the
// name of the field and its value. Nested messages are expected to be passed
// as byte blobs (as they were built recursively using this function).
// The return value is the built proto or an error about why the proto could
// not be built.
void BuildProto::Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
  PERFETTO_DCHECK(argc >= 0);

  auto* user_ctx = GetUserData(ctx);
  const ProtoDescriptor& desc =
      user_ctx->pool->descriptors()[user_ctx->descriptor_idx];

  if (argc % 2 != 0) {
    return sqlite::utils::SetError(
        ctx, base::ErrStatus("Invalid number of args to %s BuildProto (got %d)",
                             desc.full_name().c_str(), argc));
  }

  ProtoBuilder builder(user_ctx->pool, &desc);
  for (int i = 0; i < argc; i += 2) {
    if (sqlite::value::Type(argv[i]) != sqlite::Type::kText) {
      return sqlite::utils::SetError(ctx, "BuildProto: Invalid args");
    }

    const char* key = sqlite::value::Text(argv[i]);
    SqlValue value = sqlite::utils::SqliteValueToSqlValue(argv[i + 1]);
    auto status = builder.AppendSqlValue(key, value);
    if (!status.ok()) {
      return sqlite::utils::SetError(ctx, status);
    }
  }

  // Even if the message is empty, we don't return null here as we want the
  // existence of the message to be respected.
  std::vector<uint8_t> raw = builder.SerializeToProtoBuilderResult();
  if (raw.empty()) {
    // Passing nullptr to SQLite feels dangerous so just pass an empty string
    // and zero as the size so we don't deref nullptr accidentally somewhere.
    return sqlite::result::StaticBytes(ctx, "", 0);
  }

  return sqlite::result::TransientBytes(ctx, raw.data(),
                                        static_cast<int>(raw.size()));
}

void RunMetric::Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
  PERFETTO_DCHECK(argc >= 0);

  if (argc == 0 || sqlite::value::Type(argv[0]) != sqlite::Type::kText) {
    return sqlite::utils::SetError(ctx, "RUN_METRIC: Invalid arguments");
  }

  auto* user_ctx = GetUserData(ctx);
  const char* path = sqlite::value::Text(argv[0]);
  auto metric_it = std::find_if(
      user_ctx->metrics->begin(), user_ctx->metrics->end(),
      [path](const SqlMetricFile& metric) { return metric.path == path; });
  if (metric_it == user_ctx->metrics->end()) {
    return sqlite::utils::SetError(
        ctx, base::ErrStatus("RUN_METRIC: Unknown filename provided %s", path));
  }

  std::unordered_map<std::string, std::string> substitutions;
  for (int i = 1; i < argc; i += 2) {
    if (sqlite::value::Type(argv[i]) != sqlite::Type::kText) {
      return sqlite::utils::SetError(ctx,
                                     "RUN_METRIC: all keys must be strings");
    }

    std::string key_str = sqlite::value::Text(argv[i]);
    if (i + 1 >= argc) {
      return sqlite::utils::SetError(ctx, "RUN_METRIC: missing value for key");
    }

    std::optional<std::string> value_str;
    switch (sqlite::value::Type(argv[i + 1])) {
      case sqlite::Type::kText:
        value_str = sqlite::value::Text(argv[i + 1]);
        break;
      case sqlite::Type::kInteger:
        value_str = std::to_string(sqlite::value::Int64(argv[i + 1]));
        break;
      case sqlite::Type::kFloat:
        value_str = std::to_string(sqlite::value::Double(argv[i + 1]));
        break;
      case sqlite::Type::kNull:
      case sqlite::Type::kBlob:
        value_str = std::nullopt;
        break;
    }

    if (!value_str) {
      return sqlite::utils::SetError(
          ctx, "RUN_METRIC: all values must be convertible to strings");
    }
    substitutions[key_str] = *value_str;
  }

  std::string subbed_sql;
  int ret = TemplateReplace(metric_it->sql, substitutions, &subbed_sql);
  if (ret) {
    return sqlite::utils::SetError(
        ctx,
        base::ErrStatus("RUN_METRIC: Error when performing substitutions: %s",
                        metric_it->sql.c_str()));
  }

  auto res =
      user_ctx->engine->Execute(SqlSource::FromMetricFile(subbed_sql, path));
  if (!res.status().ok()) {
    return sqlite::utils::SetError(ctx, res.status());
  }

  // RUN_METRIC returns no value (void function)
  return sqlite::utils::ReturnVoidFromFunction(ctx);
}

void UnwrapMetricProto::Step(sqlite3_context* ctx,
                             int argc,
                             sqlite3_value** argv) {
  PERFETTO_DCHECK(argc == 2);

  if (sqlite::value::Type(argv[0]) != sqlite::Type::kBlob) {
    return sqlite::utils::SetError(ctx,
                                   "UNWRAP_METRIC_PROTO: proto is not a blob");
  }

  if (sqlite::value::Type(argv[1]) != sqlite::Type::kText) {
    return sqlite::utils::SetError(
        ctx, "UNWRAP_METRIC_PROTO: message type is not string");
  }

  const uint8_t* ptr =
      static_cast<const uint8_t*>(sqlite::value::Blob(argv[0]));
  size_t size = static_cast<size_t>(sqlite::value::Bytes(argv[0]));
  if (size == 0) {
    return sqlite::result::StaticBytes(ctx, "", 0);
  }

  const char* message_type = sqlite::value::Text(argv[1]);
  static constexpr uint32_t kMessageType =
      static_cast<uint32_t>(protozero::proto_utils::ProtoSchemaType::kMessage);
  base::StatusOr<protozero::ConstBytes> bytes =
      ValidateSingleNonEmptyMessage(ptr, size, kMessageType, message_type);
  if (!bytes.ok()) {
    return sqlite::utils::SetError(ctx,
                                   base::ErrStatus("UNWRAP_METRICS_PROTO: %s",
                                                   bytes.status().c_message()));
  }

  return sqlite::result::TransientBytes(ctx, bytes->data,
                                        static_cast<int>(bytes->size));
}

base::Status ComputeMetrics(PerfettoSqlEngine* engine,
                            const std::vector<std::string>& metrics_to_compute,
                            const std::vector<SqlMetricFile>& sql_metrics,
                            const DescriptorPool& pool,
                            const ProtoDescriptor& root_descriptor,
                            std::vector<uint8_t>* metrics_proto) {
  ProtoBuilder metric_builder(&pool, &root_descriptor);
  for (const auto& name : metrics_to_compute) {
    auto metric_it =
        std::find_if(sql_metrics.begin(), sql_metrics.end(),
                     [&name](const SqlMetricFile& metric) {
                       return metric.proto_field_name.has_value() &&
                              name == metric.proto_field_name.value();
                     });
    if (metric_it == sql_metrics.end()) {
      return base::ErrStatus("Unknown metric %s", name.c_str());
    }

    const SqlMetricFile& sql_metric = *metric_it;
    auto prep_it =
        engine->Execute(SqlSource::FromMetric(sql_metric.sql, metric_it->path));
    RETURN_IF_ERROR(prep_it.status());

    auto output_query =
        "SELECT * FROM " + sql_metric.output_table_name.value() + ";";
    PERFETTO_TP_TRACE(
        metatrace::Category::QUERY_TIMELINE, "COMPUTE_METRIC_QUERY",
        [&](metatrace::Record* r) { r->AddArg("SQL", output_query); });

    auto it = engine->ExecuteUntilLastStatement(
        SqlSource::FromTraceProcessorImplementation(std::move(output_query)));
    RETURN_IF_ERROR(it.status());

    // Allow the query to return no rows. This has the same semantic as an
    // empty proto being returned.
    const auto& field_name = sql_metric.proto_field_name.value();
    if (it->stmt.IsDone()) {
      metric_builder.AppendSqlValue(field_name, SqlValue::Bytes(nullptr, 0));
      continue;
    }

    if (it->stats.column_count != 1) {
      return base::ErrStatus("Output table %s should have exactly one column",
                             sql_metric.output_table_name.value().c_str());
    }

    SqlValue col = sqlite::utils::SqliteValueToSqlValue(
        sqlite3_column_value(it->stmt.sqlite_stmt(), 0));
    if (col.type != SqlValue::kBytes) {
      return base::ErrStatus("Output table %s column has invalid type",
                             sql_metric.output_table_name.value().c_str());
    }
    RETURN_IF_ERROR(metric_builder.AppendSqlValue(field_name, col));

    bool has_next = it->stmt.Step();
    if (has_next) {
      return base::ErrStatus("Output table %s should have at most one row",
                             sql_metric.output_table_name.value().c_str());
    }
    RETURN_IF_ERROR(it->stmt.status());
  }
  *metrics_proto = metric_builder.SerializeRaw();
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor::metrics
