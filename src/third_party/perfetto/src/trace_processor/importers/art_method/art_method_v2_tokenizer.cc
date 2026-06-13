/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "src/trace_processor/importers/art_method/art_method_v2_tokenizer.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/utils.h"
#include "protos/perfetto/common/builtin_clock.pbzero.h"
#include "src/trace_processor/importers/art_method/art_method_event.h"
#include "src/trace_processor/importers/art_method/art_method_parser.h"
#include "src/trace_processor/importers/common/clock_tracker.h"
#include "src/trace_processor/importers/common/metadata_tracker.h"

namespace perfetto::trace_processor::art_method {
namespace {

constexpr uint32_t kMagicValue = 0x574f4c53;
constexpr uint32_t kVersionDualClockStreaming = 0xf5;
constexpr uint32_t kVersionDualClock = 0x05;
constexpr uint8_t kThreadInfo = 0;
constexpr uint8_t kMethodInfo = 1;
constexpr uint8_t kTraceEntries = 2;
constexpr uint8_t kSummary = 3;

constexpr uint8_t kMethodEntry = 0;
constexpr uint8_t kMethodExitNormal = 1;
constexpr uint8_t kMethodExitError = 2;
constexpr uint8_t kTraceActionMask = 3;

// Decodes a signed LEB128 integer from the byte stream.
// Returns the advanced pointer, or the original `data` pointer if the
// end of the buffer was reached or if the data is malformed.
// Note: This looks similar to proto varint decoding but it's actually different
// because of how it handles negative numbers and sign extension.
const uint8_t* DecodeSignedLeb128(const uint8_t* data,
                                  const uint8_t* end,
                                  int64_t* value) {
  const uint8_t* ptr = data;
  uint64_t result = 0;
  int shift = 0;
  uint8_t byte;
  do {
    if (ptr >= end) {
      return data;
    }
    byte = *ptr++;
    if (shift < 64) {
      result |= (static_cast<uint64_t>(byte & 0x7f) << shift);
    }
    shift += 7;
  } while ((byte & 0x80) && shift < 70);

  if (byte & 0x80) {
    return data;
  }

  if ((shift < 64) && (byte & 0x40)) {
    result |= ~(static_cast<uint64_t>(0)) << shift;
  }

  *value = static_cast<int64_t>(result);
  return ptr;
}

// Reads a simple little-endian integer from the byte stream.
uint64_t ReadNumber(const uint8_t* ptr, size_t num_bytes) {
  static_assert(PERFETTO_IS_LITTLE_ENDIAN(),
                "ART Method traces are little-endian. Perfetto requires a "
                "little-endian host to parse these effectively.");
  PERFETTO_DCHECK(num_bytes <= sizeof(uint64_t));
  uint64_t number = 0;
  memcpy(&number, ptr, num_bytes);
  return number;
}

std::string ConstructPathname(base::StringView class_name,
                              base::StringView pathname) {
  size_t index = class_name.rfind('/');
  if (index != base::StringView::npos && pathname.EndsWith(".java")) {
    return class_name.substr(0, index + 1).ToStdString() +
           pathname.ToStdString();
  }
  return pathname.ToStdString();
}

}  // namespace

ArtMethodV2Tokenizer::ArtMethodV2Tokenizer(TraceProcessorContext* ctx)
    : context_(ctx),
      stream_(
          ctx->sorter->CreateStream(std::make_unique<ArtMethodParser>(ctx))) {}

ArtMethodV2Tokenizer::~ArtMethodV2Tokenizer() = default;

base::Status ArtMethodV2Tokenizer::Parse(TraceBlobView blob) {
  reader_.PushBack(std::move(blob));

  if (!header_parsed_) {
    ASSIGN_OR_RETURN(bool has_more, ParseHeader());
    if (!has_more) {
      return base::OkStatus();
    }
    header_parsed_ = true;
  }

  if (trace_complete_) {
    return base::ErrStatus("ART Method V2 trace: extra data after summary");
  }

  for (;;) {
    if (is_parsing_summary_) {
      auto it = reader_.GetIterator();
      size_t avail = reader_.avail();
      if (avail > 0) {
        auto summary_opt = it.MaybeRead(avail);
        if (summary_opt) {
          std::string_view new_data(
              reinterpret_cast<const char*>(summary_opt->data()), avail);

          // To optimize the search for "*end" across streaming chunks, we only
          // search the newly appended data. We back up by 3 characters into the
          // old data in case the "*end" string was split exactly across two
          // chunks.
          size_t search_start = summary_.size() >= 3 ? summary_.size() - 3 : 0;
          summary_.append(new_data);

          if (summary_.find("*end", search_start) != std::string::npos) {
            trace_complete_ = true;
            is_parsing_summary_ = false;
          }
          reader_.PopFrontUntil(it.file_offset());
        }
      }
      return base::OkStatus();
    }

    auto it = reader_.GetIterator();
    auto header_opt = it.MaybeRead(1);
    if (!header_opt) {
      return base::OkStatus();
    }
    uint8_t entry_header = header_opt->data()[0];

    bool has_more = false;
    switch (entry_header) {
      case kThreadInfo:
      case kMethodInfo: {
        ASSIGN_OR_RETURN(has_more,
                         ParseThreadOrMethodInfo(entry_header == kMethodInfo));
        break;
      }
      case kTraceEntries: {
        ASSIGN_OR_RETURN(has_more, ParseTraceEntries());
        break;
      }
      case kSummary: {
        is_parsing_summary_ = true;
        auto summary_opt = it.MaybeRead(1);
        reader_.PopFrontUntil(it.file_offset());
        has_more = true;
        break;
      }
      default: {
        return base::ErrStatus(
            "ART Method V2 trace: unknown opcode encountered %d", entry_header);
      }
    }

    if (!has_more) {
      return base::OkStatus();
    }
  }
}

base::StatusOr<bool> ArtMethodV2Tokenizer::ParseHeader() {
  auto it = reader_.GetIterator();
  auto header_opt = it.MaybeRead(32);
  if (!header_opt) {
    return false;
  }
  const uint8_t* header = header_opt->data();

  // Verify the magic value ('SLOW') matches.
  uint32_t magic_value = static_cast<uint32_t>(ReadNumber(header, 4));
  if (magic_value != kMagicValue) {
    return base::ErrStatus("ART Method V2 trace: expected start-header magic");
  }

  uint16_t version = static_cast<uint16_t>(ReadNumber(header + 4, 2));
  if (version != kVersionDualClock && version != kVersionDualClockStreaming &&
      version != 0x0004 && version != 0x00f4) {
    return base::ErrStatus("ART Method V2 trace: unsupported version %u",
                           version);
  }

  is_dual_clock_ =
      (version == kVersionDualClock) || (version == kVersionDualClockStreaming);
  ts_ = static_cast<int64_t>(ReadNumber(header + 6, 8));
  start_tsc_ = ReadNumber(header + 14, 8);
  tsc_frequency_ = ReadNumber(header + 22, 8);

  reader_.PopFrontUntil(it.file_offset());
  return true;
}

base::StatusOr<bool> ArtMethodV2Tokenizer::ParseThreadOrMethodInfo(
    bool is_method) {
  uint8_t header_length = is_method ? 10 : 6;
  auto it = reader_.GetIterator();
  auto header_opt = it.MaybeRead(1 + header_length);
  if (!header_opt) {
    return false;
  }
  const uint8_t* header = header_opt->data() + 1;

  uint8_t num_bytes_for_id = is_method ? 8 : 4;
  uint64_t id = ReadNumber(header, num_bytes_for_id);
  uint16_t length =
      static_cast<uint16_t>(ReadNumber(header + num_bytes_for_id, 2));

  auto name_opt = it.MaybeRead(length);
  if (!name_opt) {
    return false;
  }

  std::string str(reinterpret_cast<const char*>(name_opt->data()), length);
  if (!str.empty() && str.back() == '\n') {
    str.pop_back();
  }

  if (is_method) {
    ParseMethod(id, str);
  } else {
    StringId str_id = context_->storage->InternString(base::StringView(str));
    thread_map_.Insert(id, {str_id, false, {}});
  }

  reader_.PopFrontUntil(it.file_offset());
  return true;
}

void ArtMethodV2Tokenizer::ParseMethod(uint64_t id, const std::string& str) {
  base::StringSplitter s(str, '\t');
  std::vector<base::StringView> tokens;
  while (s.Next()) {
    tokens.emplace_back(s.cur_token(), s.cur_token_size());
  }

  base::StringView class_name = tokens.empty() ? base::StringView() : tokens[0];
  base::StringView method_name;
  base::StringView signature;
  std::optional<StringId> pathname;
  std::optional<uint32_t> line_number;

  if (tokens.size() == 5) {
    method_name = tokens[1];
    signature = tokens[2];
    std::string pathname_str = ConstructPathname(class_name, tokens[3]);
    pathname = context_->storage->InternString(base::StringView(pathname_str));
    line_number = base::StringToUInt32(tokens[4].ToStdString());
  } else if (tokens.size() > 2) {
    if (tokens[2].StartsWith("(")) {
      method_name = tokens[1];
      signature = tokens[2];
      if (tokens.size() >= 4) {
        pathname = context_->storage->InternString(tokens[3]);
      }
    } else {
      pathname = context_->storage->InternString(tokens[1]);
      line_number = base::StringToUInt32(tokens[2].ToStdString());
    }
  }

  base::StackString<2048> slice_name(
      "%.*s.%.*s: %.*s", static_cast<int>(class_name.size()), class_name.data(),
      static_cast<int>(method_name.size()), method_name.data(),
      static_cast<int>(signature.size()), signature.data());
  StringId str_id = context_->storage->InternString(slice_name.string_view());

  method_map_.Insert(id, {str_id, pathname, line_number});
}

base::StatusOr<bool> ArtMethodV2Tokenizer::ParseTraceEntries() {
  auto it = reader_.GetIterator();
  auto header_opt = it.MaybeRead(1 + 11);
  if (!header_opt) {
    return false;
  }
  const uint8_t* header = header_opt->data() + 1;

  uint32_t thread_id = static_cast<uint32_t>(ReadNumber(header, 4));
  int offset = 4;
  uint32_t num_records = static_cast<uint32_t>(ReadNumber(header + offset, 3));
  offset += 3;
  uint32_t total_size = static_cast<uint32_t>(ReadNumber(header + offset, 4));

  auto buffer_opt = it.MaybeRead(total_size);
  if (!buffer_opt) {
    return false;
  }
  const uint8_t* buffer = buffer_opt->data();

  const uint8_t* current_buffer_ptr = buffer;
  const uint8_t* const buffer_end = buffer + total_size;

  int64_t prev_method_value = 0;
  int64_t prev_timestamp_action_value = 0;

  auto thread_it = thread_map_.Find(thread_id);
  if (!thread_it) {
    return base::ErrStatus(
        "ART Method V2 trace: trace entries encountered for unknown thread %u",
        thread_id);
  }

  std::vector<uint64_t>& method_stack = thread_it->method_stack;

  for (uint32_t i = 0; i < num_records; i++) {
    if (current_buffer_ptr >= buffer_end) {
      break;
    }

    int64_t diff = 0;
    const uint8_t* next_ptr =
        DecodeSignedLeb128(current_buffer_ptr, buffer_end, &diff);
    if (next_ptr == current_buffer_ptr) {
      break;
    }
    current_buffer_ptr = next_ptr;

    uint64_t curr_timestamp_action_value =
        static_cast<uint64_t>(prev_timestamp_action_value) +
        static_cast<uint64_t>(diff);
    prev_timestamp_action_value =
        static_cast<int64_t>(curr_timestamp_action_value);

    uint8_t event_type = curr_timestamp_action_value & kTraceActionMask;

    if (is_dual_clock_) {
      // In dual clock mode, the second timestamp is Thread CPU time.
      // We just skip over it.
      int64_t dummy = 0;
      const uint8_t* next_ptr_dual =
          DecodeSignedLeb128(current_buffer_ptr, buffer_end, &dummy);
      if (next_ptr_dual == current_buffer_ptr) {
        break;
      }
      current_buffer_ptr = next_ptr_dual;
    }

    // The first timestamp is the Wall time.
    // Shift out action bits to get actual timestamp diff in nanoseconds.
    int64_t ts = static_cast<int64_t>(curr_timestamp_action_value >> 2);
    if (tsc_frequency_ > 0) {
      ts = ts * 1000000000LL / static_cast<int64_t>(tsc_frequency_);
    }

    int64_t method_id;
    if (event_type == kMethodEntry) {
      if (current_buffer_ptr >= buffer_end) {
        break;
      }

      const uint8_t* next_method_ptr =
          DecodeSignedLeb128(current_buffer_ptr, buffer_end, &diff);
      if (next_method_ptr == current_buffer_ptr) {
        break;
      }
      current_buffer_ptr = next_method_ptr;

      method_id =
          static_cast<int64_t>(static_cast<uint64_t>(prev_method_value) +
                               static_cast<uint64_t>(diff));
      prev_method_value = method_id;
      method_stack.push_back(static_cast<uint64_t>(method_id));
    } else {
      // If the action is a method exit, the method ID is omitted to save space.
      // We infer it by popping the last method ID from the thread's execution
      // stack.
      if (method_stack.empty()) {
        method_id = prev_method_value;
      } else {
        method_id = static_cast<int64_t>(method_stack.back());
        method_stack.pop_back();
      }
    }

    PushRecord(thread_id, event_type, static_cast<uint64_t>(method_id), ts);
  }

  reader_.PopFrontUntil(it.file_offset());
  return true;
}

void ArtMethodV2Tokenizer::PushRecord(uint32_t tid,
                                      uint32_t action,
                                      uint64_t method_id,
                                      int64_t ts) {
  ArtMethodEvent evt{};
  evt.tid = tid;

  auto thread_it = thread_map_.Find(tid);
  if (thread_it && !thread_it->comm_used) {
    evt.comm = thread_it->comm;
    thread_it->comm_used = true;
  }

  auto method_it = method_map_.Find(method_id);
  if (method_it) {
    evt.method = method_it->name;
    evt.pathname = method_it->pathname;
    evt.line_number = method_it->line_number;
  }

  switch (action) {
    case kMethodEntry:
      evt.action = ArtMethodEvent::kEnter;
      break;
    case kMethodExitNormal:
    case kMethodExitError:
      evt.action = ArtMethodEvent::kExit;
      break;
  }

  std::optional<int64_t> trace_ts = context_->clock_tracker->ToTraceTime(
      ClockTracker::ClockId::Machine(protos::pbzero::BUILTIN_CLOCK_MONOTONIC),
      ts_ + ts);
  if (trace_ts) {
    stream_->Push(*trace_ts, evt);
  }
}

base::Status ArtMethodV2Tokenizer::OnPushDataToSorter() {
  if (!trace_complete_) {
    return base::ErrStatus("ART Method trace: trace is incomplete");
  }

  for (const auto& line : base::SplitString(summary_, "\n")) {
    size_t pos = line.find('=');
    if (pos != std::string::npos) {
      std::string key = line.substr(0, pos);
      std::string value = line.substr(pos + 1);

      StringId key_id = context_->storage->InternString(base::StringView(key));
      auto int_val = base::StringToInt64(value);
      if (int_val) {
        context_->metadata_tracker->SetDynamicMetadata(
            key_id, Variadic::Integer(*int_val));
      } else {
        StringId value_id =
            context_->storage->InternString(base::StringView(value));
        context_->metadata_tracker->SetDynamicMetadata(
            key_id, Variadic::String(value_id));
      }
    }
  }
  return base::OkStatus();
}

void ArtMethodV2Tokenizer::OnEventsFullyExtracted() {}

}  // namespace perfetto::trace_processor::art_method
