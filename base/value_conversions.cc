// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/value_conversions.h"

#include <stdint.h>

#include <algorithm>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/memory/ptr_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "base/unguessable_token.h"
#include "base/values.h"

namespace base {
namespace {
// Helper for serialize/deserialize UnguessableToken.
union UnguessableTokenRepresentation {
  struct Field {
    uint64_t high;
    uint64_t low;
  } field;

  uint8_t buffer[sizeof(Field)];
};
}  // namespace

// |Value| internally stores strings in UTF-8, so we have to convert from the
// system native code to UTF-8 and back.
std::unique_ptr<Value> CreateFilePathValue(const FilePath& in_value) {
  return std::make_unique<Value>(in_value.AsUTF8Unsafe());
}

bool GetValueAsFilePath(const Value& value, FilePath* file_path) {
  std::string str;
  if (!value.GetAsString(&str))
    return false;
  if (file_path)
    *file_path = FilePath::FromUTF8Unsafe(str);
  return true;
}

// |Value| does not support 64-bit integers, and doubles do not have enough
// precision, so we store the 64-bit time value as a string instead.
std::unique_ptr<Value> CreateTimeDeltaValue(const TimeDelta& time) {
  std::string string_value = base::Int64ToString(time.ToInternalValue());
  return std::make_unique<Value>(string_value);
}

bool GetValueAsTimeDelta(const Value& value, TimeDelta* time) {
  std::string str;
  int64_t int_value;
  if (!value.GetAsString(&str) || !base::StringToInt64(str, &int_value))
    return false;
  if (time)
    *time = TimeDelta::FromInternalValue(int_value);
  return true;
}

std::unique_ptr<Value> CreateUnguessableTokenValue(
    const UnguessableToken& token) {
  UnguessableTokenRepresentation representation;
  representation.field.high = token.GetHighForSerialization();
  representation.field.low = token.GetLowForSerialization();

  return std::make_unique<Value>(
      HexEncode(representation.buffer, sizeof(representation.buffer)));
}

bool GetValueAsUnguessableToken(const Value& value, UnguessableToken* token) {
  if (!value.is_string()) {
    return false;
  }

  // TODO(dcheng|yucliu): Make a function that accepts non vector variant and
  // reads a fixed number of bytes.
  std::vector<uint8_t> high_low_bytes;
  if (!HexStringToBytes(value.GetString(), &high_low_bytes)) {
    return false;
  }

  UnguessableTokenRepresentation representation;
  if (high_low_bytes.size() != sizeof(representation.buffer)) {
    return false;
  }

  std::copy(high_low_bytes.begin(), high_low_bytes.end(),
            std::begin(representation.buffer));
  *token = UnguessableToken::Deserialize(representation.field.high,
                                         representation.field.low);
  return true;
}

}  // namespace base
