/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "src/tools/ftrace_proto_gen/proto_gen_utils.h"

#include <algorithm>
#include <regex>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/subprocess.h"

namespace perfetto {

namespace {

std::string RunClangFmt(const std::string& input) {
  base::Subprocess clang_fmt({"third_party/clang-format/clang-format"});
  clang_fmt.args.stdout_mode = base::Subprocess::OutputMode::kBuffer;
  clang_fmt.args.stderr_mode = base::Subprocess::OutputMode::kInherit;
  clang_fmt.args.input = input;
  PERFETTO_CHECK(clang_fmt.Call());
  return std::move(clang_fmt.output());
}

}  // namespace

using base::Contains;
using base::EndsWith;
using base::StartsWith;
using base::Uppercase;

VerifyStream::VerifyStream(std::string filename)
    : filename_(std::move(filename)) {
  PERFETTO_CHECK(base::ReadFile(filename_, &expected_));
}

VerifyStream::~VerifyStream() {
  std::string tidied = str();
  if (EndsWith(filename_, "cc") || EndsWith(filename_, "proto"))
    tidied = RunClangFmt(str());
  if (expected_ != tidied) {
    PERFETTO_FATAL("%s is out of date. Please run tools/run_ftrace_proto_gen.",
                   filename_.c_str());
  }
}

FtraceEventName::FtraceEventName(const std::string& full_name) {
  if (full_name.rfind("removed", 0) != std::string::npos) {
    valid_ = false;
    return;
  }
  name_ = full_name.substr(full_name.find('/') + 1, std::string::npos);
  group_ = full_name.substr(0, full_name.find('/'));
  valid_ = true;
}

bool FtraceEventName::valid() const {
  return valid_;
}

const std::string& FtraceEventName::name() const {
  PERFETTO_CHECK(valid_);
  return name_;
}

const std::string& FtraceEventName::group() const {
  PERFETTO_CHECK(valid_);
  return group_;
}

std::string ToCamelCase(const std::string& s) {
  std::string result;
  result.reserve(s.size());
  bool upperCaseNextChar = true;
  for (size_t i = 0; i < s.size(); i++) {
    char c = s[i];
    if (c == '_') {
      upperCaseNextChar = true;
      continue;
    }
    if (upperCaseNextChar) {
      upperCaseNextChar = false;
      c = Uppercase(c);
    }
    result.push_back(c);
  }
  return result;
}

ProtoType ProtoType::GetSigned() const {
  PERFETTO_CHECK(type == NUMERIC);
  if (is_signed)
    return *this;

  if (size == 64) {
    return Numeric(64, true);
  }

  return Numeric(2 * size, true);
}

std::string ProtoType::ToString() const {
  switch (type) {
    case INVALID:
      PERFETTO_CHECK(false);
    case STRING:
      return "string";
    case NUMERIC: {
      std::string s;
      if (!is_signed)
        s += "u";
      s += "int";
      s += std::to_string(size);
      return s;
    }
  }
  PERFETTO_CHECK(false);  // for GCC.
}

// static
ProtoType ProtoType::String(bool is_repeated) {
  return {STRING, 0, false, is_repeated};
}

// static
ProtoType ProtoType::Invalid() {
  return {INVALID, 0, false, false};
}

// static
ProtoType ProtoType::Numeric(uint16_t size, bool is_signed, bool is_repeated) {
  PERFETTO_CHECK(size == 32 || size == 64);
  return {NUMERIC, size, is_signed, is_repeated};
}

// static
ProtoType ProtoType::FromDescriptor(
    google::protobuf::FieldDescriptor::Type type,
    bool is_repeated) {
  if (type == google::protobuf::FieldDescriptor::Type::TYPE_UINT64)
    return Numeric(64, false, is_repeated);

  if (type == google::protobuf::FieldDescriptor::Type::TYPE_INT64)
    return Numeric(64, true, is_repeated);

  if (type == google::protobuf::FieldDescriptor::Type::TYPE_UINT32)
    return Numeric(32, false, is_repeated);

  if (type == google::protobuf::FieldDescriptor::Type::TYPE_INT32)
    return Numeric(32, true, is_repeated);

  if (type == google::protobuf::FieldDescriptor::Type::TYPE_STRING)
    return String(is_repeated);

  if (type == google::protobuf::FieldDescriptor::Type::TYPE_ENUM)
    return Numeric(32, true, is_repeated);

  return Invalid();
}

ProtoType GetCommon(ProtoType one, ProtoType other) {
  // Always need to prefer the LHS as it is the one already present
  // in the proto.
  if (one.type == ProtoType::STRING)
    return ProtoType::String(one.is_repeated);

  if (one.is_signed || other.is_signed) {
    one = one.GetSigned();
    other = other.GetSigned();
  }

  return ProtoType::Numeric(std::max(one.size, other.size), one.is_signed,
                            one.is_repeated || other.is_repeated);
}

ProtoType InferProtoType(const FtraceEvent::Field& field) {
  // Fixed length strings: "char foo[16]"
  if (std::regex_match(field.type_and_name, std::regex(R"(char \w+\[\d+\])")))
    return ProtoType::String();

  // String pointers: "__data_loc char[] foo" (as in
  // 'cpufreq_interactive_boost').
  if (Contains(field.type_and_name, "char[] "))
    return ProtoType::String();
  if (Contains(field.type_and_name, "char * "))
    return ProtoType::String();

  // Variable length strings: "char* foo"
  if (StartsWith(field.type_and_name, "char *"))
    return ProtoType::String();

  // Variable length strings: "char foo" + size: 0 (as in 'print').
  if (StartsWith(field.type_and_name, "char ") && field.size == 0)
    return ProtoType::String();

  // ino_t, i_ino and dev_t are 32bit on some devices 64bit on others. For the
  // protos we need to choose the largest possible size.
  if (StartsWith(field.type_and_name, "ino_t ") ||
      StartsWith(field.type_and_name, "i_ino ") ||
      StartsWith(field.type_and_name, "dev_t ")) {
    return ProtoType::Numeric(64, /* is_signed= */ false);
  }

  // Bools should always uint32 even if they are signed.
  if (StartsWith(field.type_and_name, "bool ")) {
    return ProtoType::Numeric(32, /* is_signed= */ false);
  }

  // Fixed size array for syscall args. Similar to ino_t choose the largest
  // possible size to cover 32bit and 64bit.
  if (StartsWith(field.type_and_name, "unsigned long args[6]")) {
    return ProtoType::Numeric(64, /* is_signed= */ false,
                              /* is_repeated= */ true);
  }

  // Ints of various sizes:
  if (field.size <= 4)
    return ProtoType::Numeric(32, field.is_signed);
  if (field.size <= 8)
    return ProtoType::Numeric(64, field.is_signed);
  return ProtoType::Invalid();
}

Proto::Proto(std::string evt_name, const google::protobuf::Descriptor& desc)
    : name(desc.name()), event_name(evt_name) {
  for (int i = 0; i < desc.field_count(); ++i) {
    const google::protobuf::FieldDescriptor* field = desc.field(i);
    PERFETTO_CHECK(field);
    AddField(Field{ProtoType::FromDescriptor(field->type()), field->name(),
                   uint32_t(field->number())});
  }
}

std::vector<const Proto::Field*> Proto::SortedFields() {
  std::vector<const Proto::Field*> sorted_fields;

  for (const auto& p : fields) {
    sorted_fields.emplace_back(&p.second);
  }
  std::sort(sorted_fields.begin(), sorted_fields.end(),
            [](const Proto::Field* a, const Proto::Field* b) {
              return a->number < b->number;
            });
  return sorted_fields;
}

std::string Proto::ToString() {
  std::string s;
  s += "message " + name + " {\n";
  for (const auto field : SortedFields()) {
    if (field->type.is_repeated)
      s += "  repeated ";
    else
      s += "  optional ";
    s += field->type.ToString() + " " + field->name + " = " +
         std::to_string(field->number) + ";\n";
  }
  s += "}\n";
  return s;
}

void Proto::UnionFields(const std::vector<Proto::Field>& candidate_fields) {
  for (const auto& candidate : candidate_fields) {
    auto it = fields.find(candidate.name);
    if (it != fields.end()) {
      // potentially expand proto type to cover both cases
      it->second.type = GetCommon(it->second.type, candidate.type);
      continue;
    }
    Proto::Field new_field = candidate;
    new_field.number = ++max_id;
    AddField(std::move(new_field));
  }
}

void Proto::AddField(Proto::Field other) {
  max_id = std::max(max_id, other.number);
  fields.emplace(other.name, std::move(other));
}

}  // namespace perfetto
