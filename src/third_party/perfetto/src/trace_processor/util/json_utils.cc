/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "src/trace_processor/util/json_utils.h"

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/small_vector.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/ext/base/variant.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/util/json_parser.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/variadic.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#if PERFETTO_BUILDFLAG(PERFETTO_TP_JSON)
#include <json/reader.h>
#include <json/value.h>
#endif

namespace perfetto::trace_processor::json {

namespace {

void InsertLeaf(TraceStorage* storage,
                ArgsTracker::BoundInserter* inserter,
                const JsonValue& value,
                std::string_view flat_key,
                std::string_view key) {
  StringPool::Id flat_key_id = storage->InternString(flat_key);
  StringPool::Id key_id = storage->InternString(key);
  switch (value.index()) {
    case base::variant_index<json::JsonValue, double>():
      inserter->AddArg(flat_key_id, key_id,
                       Variadic::Real(base::unchecked_get<double>(value)));
      break;
    case base::variant_index<json::JsonValue, int64_t>():
      inserter->AddArg(flat_key_id, key_id,
                       Variadic::Integer(base::unchecked_get<int64_t>(value)));
      break;
    case base::variant_index<json::JsonValue, std::string_view>():
      inserter->AddArg(flat_key_id, key_id,
                       Variadic::String(storage->InternString(
                           base::unchecked_get<std::string_view>(value))));
      break;
    case base::variant_index<json::JsonValue, bool>():
      inserter->AddArg(flat_key_id, key_id,
                       Variadic::Boolean(base::unchecked_get<bool>(value)));
      break;
    default:
      PERFETTO_FATAL("Unreachable");
  }
}

}  // namespace

std::optional<Json::Value> ParseJsonString(base::StringView raw_string) {
#if PERFETTO_BUILDFLAG(PERFETTO_TP_JSON)
  Json::CharReaderBuilder b;
  auto reader = std::unique_ptr<Json::CharReader>(b.newCharReader());

  Json::Value value;
  const char* begin = raw_string.data();
  return reader->parse(begin, begin + raw_string.size(), &value, nullptr)
             ? std::make_optional(std::move(value))
             : std::nullopt;
#else
  perfetto::base::ignore_result(raw_string);
  return std::nullopt;
#endif
}

bool AddJsonValueToArgs(Iterator& it,
                        const char* start,
                        const char* end,
                        std::string_view flat_key,
                        std::string_view key,
                        TraceStorage* storage,
                        ArgsTracker::BoundInserter* inserter) {
  it.Reset(start, end);
  if (!it.ParseStart()) {
    JsonValue value;
    std::string unescaped_str;
    base::Status status;
    if (ParseValue(start, end, value, unescaped_str, status) !=
        ReturnCode::kOk) {
      return false;
    }
    // This should never happen: the iterator should have been able to parse
    // this if it was a valid JSON value.
    PERFETTO_CHECK(!std::holds_alternative<Object>(value) &&
                   !std::holds_alternative<Array>(value));
    InsertLeaf(storage, inserter, value, flat_key, key);
    return false;
  }
  struct Frame {
    uint32_t flat_key_size = 0;
    uint32_t key_size = 0;
    bool inserted = false;
  };
  base::SmallVector<Frame, 4> stack;
  std::string flat_key_str(flat_key);
  std::string key_str(key);

  stack.emplace_back(Frame{static_cast<uint32_t>(flat_key_str.size()),
                           static_cast<uint32_t>(key_str.size()), false});
  for (;;) {
    json::Iterator::ParseType parse_type = it.parse_stack().back();
    auto& frame = stack.back();
    switch (it.ParseAndRecurse()) {
      case json::ReturnCode::kOk:
        break;
      case json::ReturnCode::kEndOfScope: {
        PERFETTO_DCHECK(!stack.empty());
        bool inserted = frame.inserted;
        stack.pop_back();
        if (stack.empty()) {
          return inserted;
        }
        auto& new_frame = stack.back();
        new_frame.inserted |= inserted;
        continue;
      }
      case json::ReturnCode::kIncompleteInput:
      case json::ReturnCode::kError:
        return false;
    }

    // Skip null values.
    const auto& value = it.value();
    if (value.index() == base::variant_index<json::JsonValue, json::Null>()) {
      continue;
    }

    // Leaf value.
    flat_key_str.resize(frame.flat_key_size);
    key_str.resize(frame.key_size);

    if (parse_type == json::Iterator::ParseType::kArray) {
      StringPool::Id key_id = storage->InternString(key_str);
      size_t array_index = inserter->GetNextArrayEntryIndex(key_id);
      key_str += "[" + std::to_string(array_index) + "]";
      inserter->IncrementArrayEntryIndex(key_id);
    } else if (parse_type == json::Iterator::ParseType::kObject) {
      key_str += ".";
      key_str += it.key();
      flat_key_str += ".";
      flat_key_str += it.key();
    } else {
      PERFETTO_FATAL("Unexpected parse type %d", static_cast<int>(parse_type));
    }

    if (value.index() == base::variant_index<json::JsonValue, json::Object>() ||
        value.index() == base::variant_index<json::JsonValue, json::Array>()) {
      stack.emplace_back(Frame{static_cast<uint32_t>(flat_key_str.size()),
                               static_cast<uint32_t>(key_str.size()), false});
      continue;
    }

    // Only for leaf values we actually insert into the args table.
    frame.inserted = true;

    InsertLeaf(storage, inserter, it.value(), flat_key_str, key_str);
  }
}

}  // namespace perfetto::trace_processor::json
