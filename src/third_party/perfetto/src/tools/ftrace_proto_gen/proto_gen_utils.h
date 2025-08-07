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

#ifndef SRC_TOOLS_FTRACE_PROTO_GEN_PROTO_GEN_UTILS_H_
#define SRC_TOOLS_FTRACE_PROTO_GEN_PROTO_GEN_UTILS_H_

#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <google/protobuf/descriptor.h>

#include "src/traced/probes/ftrace/format_parser/format_parser.h"

namespace perfetto {

class VerifyStream : public std::ostringstream {
 public:
  VerifyStream(std::string filename);
  ~VerifyStream() override;

 private:
  std::string filename_;
  std::string expected_;
};

class FtraceEventName {
 public:
  explicit FtraceEventName(const std::string& full_name);

  bool valid() const;
  const std::string& name() const;
  const std::string& group() const;

 private:
  bool valid_;
  std::string name_;
  std::string group_;
};

struct ProtoType {
  enum Type { INVALID, NUMERIC, STRING };
  Type type;
  uint16_t size;
  bool is_signed;
  bool is_repeated;

  ProtoType GetSigned() const;
  std::string ToString() const;

  static ProtoType Invalid();
  static ProtoType String(bool is_repeated = false);
  static ProtoType Numeric(uint16_t size,
                           bool is_signed,
                           bool is_repeated = false);
  static ProtoType FromDescriptor(google::protobuf::FieldDescriptor::Type type,
                                  bool is_repeated = false);
};

struct Proto {
  Proto() = default;
  Proto(std::string evt_name, const google::protobuf::Descriptor& desc);
  struct Field {
    ProtoType type;
    std::string name;
    uint32_t number;
  };
  std::string name;
  std::string event_name;
  std::map<std::string, Field> fields;

  std::string ToString();
  void UnionFields(const std::vector<Proto::Field>& candidate_fields);
  void AddField(Proto::Field field);
  std::vector<const Field*> SortedFields();
  uint32_t max_id = 0;
};

std::string ToCamelCase(const std::string& s);
ProtoType GetCommon(ProtoType one, ProtoType other);
ProtoType InferProtoType(const FtraceEvent::Field& field);

}  // namespace perfetto

#endif  // SRC_TOOLS_FTRACE_PROTO_GEN_PROTO_GEN_UTILS_H_
