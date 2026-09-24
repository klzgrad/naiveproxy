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

#ifndef SRC_TRACE_PROCESSOR_PLUGINS_WINSCOPE_IMPORTER_PROTOLOG_MESSAGE_DECODER_H_
#define SRC_TRACE_PROCESSOR_PLUGINS_WINSCOPE_IMPORTER_PROTOLOG_MESSAGE_DECODER_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "perfetto/ext/base/flat_hash_map.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor::winscope {

inline constexpr std::string_view kCollisionGroupTag = "COLLISION_GROUP";
inline constexpr std::string_view kUnknownGroupTag = "UNKNOWN_GROUP";

enum ProtoLogLevel : int32_t {
  PROTOLOG_LEVEL_DEBUG = 1,
  PROTOLOG_LEVEL_VERBOSE = 2,
  PROTOLOG_LEVEL_INFO = 3,
  PROTOLOG_LEVEL_WARN = 4,
  PROTOLOG_LEVEL_ERROR = 5,
  PROTOLOG_LEVEL_WTF = 6,
};

struct DecodedMessage {
  ProtoLogLevel log_level;
  std::string group_tag;
  std::string message;
  std::optional<std::string> location;
};

struct TrackedGroup {
  std::string tag;
};

struct TrackedMessage {
  ProtoLogLevel level;
  uint32_t group_id;
  std::string message;
  std::optional<std::string> location;
  base::SmallVector<char, 8> param_signature;
};

class ProtoLogMessageDecoder {
 public:
  explicit ProtoLogMessageDecoder(TraceProcessorContext*);

  std::optional<DecodedMessage> Decode(
      uint64_t message_id,
      const std::vector<int64_t>& sint64_params,
      const std::vector<double>& double_params,
      const std::vector<bool>& boolean_params,
      const std::vector<std::string>& string_params);

  void TrackGroup(uint32_t id, std::string_view tag);

  void TrackMessage(uint64_t message_id,
                    ProtoLogLevel level,
                    uint32_t group_id,
                    const std::string& message,
                    const std::optional<std::string>& location);

 private:
  std::string FormatMessage(const std::string& message,
                            const std::vector<int64_t>& sint64_params,
                            const std::vector<double>& double_params,
                            const std::vector<bool>& boolean_params,
                            const std::vector<std::string>& string_params);

  base::SmallVector<char, 8> GetParameterSignature(const std::string& message);

  bool MatchesParameterSequence(const TrackedMessage& tracked_message,
                                const std::vector<int64_t>& sint64_params,
                                const std::vector<double>& double_params,
                                const std::vector<bool>& boolean_params,
                                const std::vector<std::string>& string_params);

  std::optional<DecodedMessage> DecodeCollidingMessageIds(
      const base::SmallVector<TrackedMessage, 1>& messages,
      uint64_t message_id,
      const std::vector<int64_t>& sint64_params,
      const std::vector<double>& double_params,
      const std::vector<bool>& boolean_params,
      const std::vector<std::string>& string_params);

 private:
  TraceProcessorContext* const context_;
  base::FlatHashMap<uint64_t, TrackedGroup> tracked_groups_;
  base::FlatHashMap<uint64_t, base::SmallVector<TrackedMessage, 1>>
      tracked_messages_;
};

}  // namespace perfetto::trace_processor::winscope

#endif  // SRC_TRACE_PROCESSOR_PLUGINS_WINSCOPE_IMPORTER_PROTOLOG_MESSAGE_DECODER_H_
