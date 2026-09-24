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

#include "src/trace_processor/plugins/winscope_importer/protolog_message_decoder.h"

#include <cstdint>
#include <optional>
#include <vector>

#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/string_utils.h"
#include "src/trace_processor/importers/common/stats_tracker.h"

namespace perfetto::trace_processor::winscope {

ProtoLogMessageDecoder::ProtoLogMessageDecoder(TraceProcessorContext* context)
    : context_(context) {}

std::optional<DecodedMessage> ProtoLogMessageDecoder::Decode(
    uint64_t message_id,
    const std::vector<int64_t>& sint64_params,
    const std::vector<double>& double_params,
    const std::vector<bool>& boolean_params,
    const std::vector<std::string>& string_params) {
  auto* tracked_messages_vec_ptr = tracked_messages_.Find(message_id);
  if (tracked_messages_vec_ptr == nullptr) {
    return std::nullopt;
  }

  const base::SmallVector<TrackedMessage, 1>& messages =
      *tracked_messages_vec_ptr;

  if (messages.size() == 1) {
    const auto& tracked_message = messages[0];
    auto message = tracked_message.message;

    auto group = tracked_groups_.Find(tracked_message.group_id);
    std::string group_tag;
    if (group == nullptr) {
      group_tag = std::string(kUnknownGroupTag);
      context_->stats_tracker->IncrementStats(
          stats::winscope_protolog_group_tag_missing);
    } else {
      group_tag = group->tag;
    }

    auto formatted_message = FormatMessage(
        message, sint64_params, double_params, boolean_params, string_params);
    return DecodedMessage{tracked_message.level, group_tag, formatted_message,
                          tracked_message.location};
  } else {
    return DecodeCollidingMessageIds(messages, message_id, sint64_params,
                                     double_params, boolean_params,
                                     string_params);
  }
}

void ProtoLogMessageDecoder::TrackGroup(uint32_t id, std::string_view tag) {
  auto tracked_group = tracked_groups_.Find(id);
  if (tracked_group != nullptr) {
    if (tracked_group->tag != tag) {
      context_->stats_tracker->IncrementStats(
          stats::winscope_protolog_group_tag_collision);

      tracked_groups_[id] = TrackedGroup{std::string(kCollisionGroupTag)};
    }
  } else {
    tracked_groups_.Insert(id, TrackedGroup{std::string(tag)});
  }
}

void ProtoLogMessageDecoder::TrackMessage(
    uint64_t message_id,
    ProtoLogLevel level,
    uint32_t group_id,
    const std::string& message,
    const std::optional<std::string>& location) {
  auto* existing_messages_ptr = tracked_messages_.Find(message_id);

  if (existing_messages_ptr == nullptr) {
    base::SmallVector<TrackedMessage, 1> single_message_vector;
    single_message_vector.emplace_back(TrackedMessage{
        level, group_id, message, location, GetParameterSignature(message)});
    tracked_messages_.Insert(message_id, std::move(single_message_vector));
    return;
  }

  base::SmallVector<TrackedMessage, 1>& existing_messages =
      *existing_messages_ptr;

  auto it = std::find_if(existing_messages.begin(), existing_messages.end(),
                         [&](const TrackedMessage& existing_msg) {
                           return existing_msg.level == level &&
                                  existing_msg.group_id == group_id &&
                                  existing_msg.message == message &&
                                  existing_msg.location == location;
                         });

  if (it != existing_messages.end()) {
    return;
  }

  existing_messages.emplace_back(TrackedMessage{
      level, group_id, message, location, GetParameterSignature(message)});
}

std::string ProtoLogMessageDecoder::FormatMessage(
    const std::string& message,
    const std::vector<int64_t>& sint64_params,
    const std::vector<double>& double_params,
    const std::vector<bool>& boolean_params,
    const std::vector<std::string>& string_params) {
  std::string formatted_message;
  formatted_message.reserve(message.size());

  auto sint64_params_itr = sint64_params.begin();
  auto double_params_itr = double_params.begin();
  auto boolean_params_itr = boolean_params.begin();
  auto str_params_itr = string_params.begin();

  for (size_t i = 0; i < message.length();) {
    if (message[i] == '%' && i + 1 < message.length()) {
      switch (message[i + 1]) {
        case '%':
          formatted_message.push_back('%');
          break;
        case 'd': {
          if (sint64_params_itr == sint64_params.end()) {
            context_->stats_tracker->IncrementStats(
                stats::winscope_protolog_param_mismatch);
            formatted_message.append("[MISSING_PARAM]");
            break;
          }
          base::StackString<32> param("%" PRId64, *sint64_params_itr);
          formatted_message.append(param.c_str());
          ++sint64_params_itr;
          break;
        }
        case 'o': {
          if (sint64_params_itr == sint64_params.end()) {
            context_->stats_tracker->IncrementStats(
                stats::winscope_protolog_param_mismatch);
            formatted_message.append("[MISSING_PARAM]");
            break;
          }
          base::StackString<32> param(
              "%" PRIo64, static_cast<uint64_t>(*sint64_params_itr));
          formatted_message.append(param.c_str());
          ++sint64_params_itr;
          break;
        }
        case 'x': {
          if (sint64_params_itr == sint64_params.end()) {
            context_->stats_tracker->IncrementStats(
                stats::winscope_protolog_param_mismatch);
            formatted_message.append("[MISSING_PARAM]");
            break;
          }
          base::StackString<32> param(
              "%" PRIx64, static_cast<uint64_t>(*sint64_params_itr));
          formatted_message.append(param.c_str());
          ++sint64_params_itr;
          break;
        }
        case 'f': {
          if (double_params_itr == double_params.end()) {
            context_->stats_tracker->IncrementStats(
                stats::winscope_protolog_param_mismatch);
            formatted_message.append("[MISSING_PARAM]");
            break;
          }
          base::StackString<32> param("%f", *double_params_itr);
          formatted_message.append(param.c_str());
          ++double_params_itr;
          break;
        }
        case 'e': {
          if (double_params_itr == double_params.end()) {
            context_->stats_tracker->IncrementStats(
                stats::winscope_protolog_param_mismatch);
            formatted_message.append("[MISSING_PARAM]");
            break;
          }
          base::StackString<32> param("%e", *double_params_itr);
          formatted_message.append(param.c_str());
          ++double_params_itr;
          break;
        }
        case 'g': {
          if (double_params_itr == double_params.end()) {
            context_->stats_tracker->IncrementStats(
                stats::winscope_protolog_param_mismatch);
            formatted_message.append("[MISSING_PARAM]");
            break;
          }
          base::StackString<32> param("%g", *double_params_itr);
          formatted_message.append(param.c_str());
          ++double_params_itr;
          break;
        }
        case 's': {
          if (str_params_itr == string_params.end()) {
            context_->stats_tracker->IncrementStats(
                stats::winscope_protolog_param_mismatch);
            formatted_message.append("[MISSING_PARAM]");
            break;
          }
          formatted_message.append(*str_params_itr);
          ++str_params_itr;
          break;
        }
        case 'b': {
          if (boolean_params_itr == boolean_params.end()) {
            context_->stats_tracker->IncrementStats(
                stats::winscope_protolog_param_mismatch);
            formatted_message.append("[MISSING_PARAM]");
            break;
          }
          formatted_message.append(*boolean_params_itr ? "true" : "false");
          ++boolean_params_itr;
          break;
        }
        default:
          formatted_message.push_back(message[i]);
          formatted_message.push_back(message[i + 1]);
      }

      i += 2;
    } else {
      formatted_message.push_back(message[i]);
      i += 1;
    }
  }

  if (sint64_params_itr != sint64_params.end() ||
      double_params_itr != double_params.end() ||
      boolean_params_itr != boolean_params.end() ||
      str_params_itr != string_params.end()) {
    context_->stats_tracker->IncrementStats(
        stats::winscope_protolog_param_mismatch);
  }

  return formatted_message;
}

base::SmallVector<char, 8> ProtoLogMessageDecoder::GetParameterSignature(
    const std::string& message) {
  base::SmallVector<char, 8> signature;
  for (size_t i = 0; i < message.length(); ++i) {
    if (message[i] == '%' && i + 1 < message.length()) {
      switch (message[i + 1]) {
        case 'd':
        case 'o':
        case 'x':
          signature.emplace_back('i');
          break;
        case 'f':
        case 'e':
        case 'g':
          signature.emplace_back('d');
          break;
        case 'b':
          signature.emplace_back('b');
          break;
        case 's':
          signature.emplace_back('s');
          break;
        default:
          break;
      }
      i++;  // Skip the format specifier character
    }
  }
  return signature;
}

bool ProtoLogMessageDecoder::MatchesParameterSequence(
    const TrackedMessage& tracked_message,
    const std::vector<int64_t>& sint64_params,
    const std::vector<double>& double_params,
    const std::vector<bool>& boolean_params,
    const std::vector<std::string>& string_params) {
  size_t sint64_idx = 0;
  size_t double_idx = 0;
  size_t bool_idx = 0;
  size_t string_idx = 0;

  for (char spec : tracked_message.param_signature) {
    switch (spec) {
      case 'i':
        if (sint64_idx >= sint64_params.size())
          return false;
        sint64_idx++;
        break;
      case 'd':
        if (double_idx >= double_params.size())
          return false;
        double_idx++;
        break;
      case 'b':
        if (bool_idx >= boolean_params.size())
          return false;
        bool_idx++;
        break;
      case 's':
        if (string_idx >= string_params.size())
          return false;
        string_idx++;
        break;
    }
  }

  return sint64_idx == sint64_params.size() &&
         double_idx == double_params.size() &&
         bool_idx == boolean_params.size() &&
         string_idx == string_params.size();
}

std::optional<DecodedMessage> ProtoLogMessageDecoder::DecodeCollidingMessageIds(
    const base::SmallVector<TrackedMessage, 1>& messages,
    uint64_t message_id,
    const std::vector<int64_t>& sint64_params,
    const std::vector<double>& double_params,
    const std::vector<bool>& boolean_params,
    const std::vector<std::string>& string_params) {
  base::SmallVector<TrackedMessage, 1> potential_matches;
  for (size_t i = 0; i < messages.size(); ++i) {
    if (MatchesParameterSequence(messages[i], sint64_params, double_params,
                                 boolean_params, string_params)) {
      potential_matches.emplace_back(messages[i]);
    }
  }

  if (potential_matches.size() == 1) {
    context_->stats_tracker->IncrementStats(
        stats::winscope_protolog_message_collision_resolved);

    auto formatted_message =
        FormatMessage(potential_matches[0].message, sint64_params,
                      double_params, boolean_params, string_params);

    auto group = tracked_groups_.Find(potential_matches[0].group_id);
    std::string group_tag;
    if (group == nullptr) {
      group_tag = std::string(kUnknownGroupTag);
      context_->stats_tracker->IncrementStats(
          stats::winscope_protolog_group_tag_missing);
    } else {
      group_tag = group->tag;
    }

    return DecodedMessage{potential_matches[0].level, group_tag,
                          formatted_message, potential_matches[0].location};
  } else {
    context_->stats_tracker->IncrementStats(
        stats::winscope_protolog_message_collision);
    std::string collision_message = "<PROTOLOG COLLISION (id=0x" +
                                    base::Uint64ToHexString(message_id) + ") ";

    if (potential_matches.empty()) {
      collision_message += "NO TYPE MATCH>";
    } else {
      collision_message += "MULTIPLE TYPE MATCHES : ";
      for (size_t i = 0; i < potential_matches.size(); ++i) {
        auto formatted_message =
            FormatMessage(potential_matches[i].message, sint64_params,
                          double_params, boolean_params, string_params);
        collision_message += "'" + formatted_message + "'";
        if (i < potential_matches.size() - 1) {
          collision_message += ",\n ";
        }
      }
      collision_message += ">";
    }
    return DecodedMessage{ProtoLogLevel::PROTOLOG_LEVEL_WARN,
                          std::string(kCollisionGroupTag), collision_message,
                          std::nullopt};
  }
}

}  // namespace perfetto::trace_processor::winscope
