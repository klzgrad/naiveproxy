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

#ifndef SRC_PROTOVM_RO_CURSOR_H_
#define SRC_PROTOVM_RO_CURSOR_H_

#include <cstdint>
#include <variant>

#include "perfetto/protozero/field.h"
#include "perfetto/protozero/proto_decoder.h"

#include "src/protovm/error_handling.h"
#include "src/protovm/scalar.h"

namespace perfetto {
namespace protovm {

class RoCursor {
 public:
  class RepeatedFieldIterator {
   public:
    RepeatedFieldIterator();
    explicit RepeatedFieldIterator(protozero::ProtoDecoder decoder,
                                   uint32_t field_id);
    RepeatedFieldIterator& operator++();
    RoCursor operator*();
    explicit operator bool() const;

   private:
    protozero::Field Advance();

    protozero::ProtoDecoder decoder_;
    uint32_t field_id_;
    protozero::Field field_;
  };

  RoCursor();
  explicit RoCursor(protozero::ConstBytes data);
  explicit RoCursor(protozero::Field data);
  StatusOr<void> EnterField(uint32_t field_id);
  StatusOr<void> EnterRepeatedFieldAt(uint32_t field_id, uint32_t index);
  StatusOr<RepeatedFieldIterator> IterateRepeatedField(uint32_t field_id) const;
  bool IsScalar() const;
  bool IsBytes() const;
  StatusOr<Scalar> GetScalar() const;
  StatusOr<protozero::ConstBytes> GetBytes() const;

 private:
  StatusOr<protozero::ConstBytes> GetLengthDelimitedData() const;

  std::variant<protozero::ConstBytes, protozero::Field> data_;
};

}  // namespace protovm
}  // namespace perfetto

#endif  // SRC_PROTOVM_RO_CURSOR_H_
