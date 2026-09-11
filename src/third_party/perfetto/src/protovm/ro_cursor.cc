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

#include "src/protovm/ro_cursor.h"

#include "perfetto/base/logging.h"
#include "perfetto/protozero/proto_utils.h"

namespace perfetto {
namespace protovm {

RoCursor::RepeatedFieldIterator::RepeatedFieldIterator()
    : decoder_{protozero::ConstBytes{}}, field_id_{0}, field_{Advance()} {
  PERFETTO_DCHECK(!field_.valid());
}

RoCursor::RepeatedFieldIterator::RepeatedFieldIterator(
    protozero::ProtoDecoder decoder,
    uint32_t field_id)
    : decoder_(decoder), field_id_(field_id), field_{Advance()} {}

RoCursor::RepeatedFieldIterator& RoCursor::RepeatedFieldIterator::operator++() {
  field_ = Advance();
  return *this;
}

RoCursor RoCursor::RepeatedFieldIterator::operator*() {
  return RoCursor(field_);
}

RoCursor::RepeatedFieldIterator::operator bool() const {
  return field_.valid();
}

protozero::Field RoCursor::RepeatedFieldIterator::Advance() {
  auto field = decoder_.ReadField();
  while (field) {
    if (field.id() == field_id_) {
      break;
    }
    field = decoder_.ReadField();
  }
  return field;
}

RoCursor::RoCursor() = default;

RoCursor::RoCursor(protozero::ConstBytes data) : data_{data} {}

RoCursor::RoCursor(protozero::Field data) : data_{data} {}

StatusOr<void> RoCursor::EnterField(uint32_t field_id) {
  auto status_or_data = GetLengthDelimitedData();
  PROTOVM_RETURN_IF_NOT_OK(status_or_data);

  protozero::ProtoDecoder decoder(*status_or_data);

  for (auto field = decoder.ReadField(); field.valid();
       field = decoder.ReadField()) {
    if (field.id() == field_id) {
      data_ = field;
      return StatusOr<void>::Ok();
    }
  }

  return StatusOr<void>::Error();
}

StatusOr<void> RoCursor::EnterRepeatedFieldAt(uint32_t field_id,
                                              uint32_t index) {
  auto status_or_data = GetLengthDelimitedData();
  PROTOVM_RETURN_IF_NOT_OK(status_or_data);

  protozero::ProtoDecoder decoder(*status_or_data);

  uint32_t current_index = 0;

  for (auto field = decoder.ReadField(); field.valid();
       field = decoder.ReadField()) {
    if (field.id() != field_id) {
      continue;
    }

    if (current_index == index) {
      data_ = field.as_bytes();
      return StatusOr<void>::Ok();
    }

    ++current_index;
  }

  return StatusOr<void>::Error();
}

StatusOr<RoCursor::RepeatedFieldIterator> RoCursor::IterateRepeatedField(
    uint32_t field_id) const {
  auto status_or_data = GetLengthDelimitedData();
  PROTOVM_RETURN_IF_NOT_OK(status_or_data);
  protozero::ProtoDecoder decoder(*status_or_data);
  return RepeatedFieldIterator(decoder, field_id);
}

bool RoCursor::IsScalar() const {
  auto* field = std::get_if<protozero::Field>(&data_);
  if (!field) {
    return false;
  }

  return field->type() == protozero::proto_utils::ProtoWireType::kVarInt ||
         field->type() == protozero::proto_utils::ProtoWireType::kFixed32 ||
         field->type() == protozero::proto_utils::ProtoWireType::kFixed64;
}

bool RoCursor::IsBytes() const {
  if (std::holds_alternative<protozero::ConstBytes>(data_)) {
    return true;
  }

  auto& field = std::get<protozero::Field>(data_);
  return field.type() ==
         protozero::proto_utils::ProtoWireType::kLengthDelimited;
}

StatusOr<Scalar> RoCursor::GetScalar() const {
  if (!IsScalar()) {
    PROTOVM_ABORT("Attempted to access length-delimited field as a scalar");
  }

  auto& field = std::get<protozero::Field>(data_);
  return Scalar{field.type(), field.as_uint64()};
}

StatusOr<protozero::ConstBytes> RoCursor::GetBytes() const {
  if (std::holds_alternative<protozero::ConstBytes>(data_)) {
    return std::get<protozero::ConstBytes>(data_);
  }

  auto& field = std::get<protozero::Field>(data_);
  if (field.type() != protozero::proto_utils::ProtoWireType::kLengthDelimited) {
    PROTOVM_ABORT(
        "Attempted to access field as length-delimited but actual wire "
        "type is "
        "%u",
        static_cast<uint32_t>(field.type()));
  }

  return field.as_bytes();
}

StatusOr<protozero::ConstBytes> RoCursor::GetLengthDelimitedData() const {
  if (std::holds_alternative<protozero::Field>(data_)) {
    auto& field = std::get<protozero::Field>(data_);
    if (field.type() !=
        protozero::proto_utils::ProtoWireType::kLengthDelimited) {
      PROTOVM_ABORT(
          "Attempted to access field as length-delimited, but actual "
          "wire type is %u",
          static_cast<uint32_t>(field.type()));
    }
    return field.as_bytes();
  }

  return std::get<protozero::ConstBytes>(data_);
}

}  // namespace protovm
}  // namespace perfetto
