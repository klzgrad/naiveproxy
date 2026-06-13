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

#include "src/trace_processor/importers/json/json_trace_tokenizer.h"

#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/murmur_hash.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/ext/base/variant.h"
#include "perfetto/public/compiler.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/importers/common/clock_tracker.h"
#include "src/trace_processor/importers/common/legacy_v8_cpu_profile_tracker.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/common/v8_profile_parser.h"
#include "src/trace_processor/importers/json/json_trace_parser.h"
#include "src/trace_processor/importers/systrace/systrace_line.h"
#include "src/trace_processor/sorter/trace_sorter.h"  // IWYU pragma: keep
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/util/clock_synchronizer.h"
#include "src/trace_processor/util/json_parser.h"

namespace perfetto::trace_processor {
namespace {

using State = json::ReturnCode;

base::Status AppendUnescapedCharacter(char c,
                                      bool is_escaping,
                                      std::string* key) {
  if (is_escaping) {
    switch (c) {
      case '"':
      case '\\':
      case '/':
        key->push_back(c);
        break;
      case 'b':
        key->push_back('\b');
        break;
      case 'f':
        key->push_back('\f');
        break;
      case 'n':
        key->push_back('\n');
        break;
      case 'r':
        key->push_back('\r');
        break;
      case 't':
        key->push_back('\t');
        break;
      case 'u':
        // Just pass through \uxxxx escape sequences which JSON supports but is
        // not worth the effort to parse as we never use them here.
        key->append("\\u");
        break;
      default:
        return base::ErrStatus("Illegal character in JSON %c", c);
    }
  } else if (c != '\\') {
    key->push_back(c);
  }
  return base::OkStatus();
}

enum class ReadStringRes {
  kEndOfString,
  kNeedsMoreData,
  kFatalError,
};
ReadStringRes ReadOneJsonString(const char* start,
                                const char* end,
                                std::string* key,
                                const char** next) {
  if (start == end) {
    return ReadStringRes::kNeedsMoreData;
  }
  if (*start != '"') {
    return ReadStringRes::kFatalError;
  }

  bool is_escaping = false;
  for (const char* s = start + 1; s < end; s++) {
    // Control characters are not allowed in JSON strings.
    if (iscntrl(*s))
      return ReadStringRes::kFatalError;

    // If we get a quote character end of the string.
    if (*s == '"' && !is_escaping) {
      *next = s + 1;
      return ReadStringRes::kEndOfString;
    }

    base::Status status = AppendUnescapedCharacter(*s, is_escaping, key);
    if (!status.ok())
      return ReadStringRes::kFatalError;

    // If we're in a string and we see a backslash and the last character was
    // not a backslash the next character is escaped:
    is_escaping = *s == '\\' && !is_escaping;
  }
  return ReadStringRes::kNeedsMoreData;
}

enum class SkipValueRes {
  kEndOfValue,
  kNeedsMoreData,
  kFatalError,
};
SkipValueRes SkipOneJsonValue(const char* start,
                              const char* end,
                              const char** next) {
  uint32_t brace_count = 0;
  uint32_t bracket_count = 0;
  for (const char* s = start; s < end; s++) {
    if (*s == '"') {
      // Because strings can contain {}[] characters, handle them separately
      // before anything else.
      std::string ignored;
      const char* str_next = nullptr;
      switch (ReadOneJsonString(s, end, &ignored, &str_next)) {
        case ReadStringRes::kFatalError:
          return SkipValueRes::kFatalError;
        case ReadStringRes::kNeedsMoreData:
          return SkipValueRes::kNeedsMoreData;
        case ReadStringRes::kEndOfString:
          // -1 as the loop body will +1 getting to the correct place.
          s = str_next - 1;
          break;
      }
      continue;
    }
    if (brace_count == 0 && bracket_count == 0 && (*s == ',' || *s == '}')) {
      // Regardless of a comma or brace, this will be skipped by the caller so
      // just set it to this character.
      *next = s;
      return SkipValueRes::kEndOfValue;
    }
    if (*s == '[') {
      ++bracket_count;
      continue;
    }
    if (*s == ']') {
      if (bracket_count == 0) {
        return SkipValueRes::kFatalError;
      }
      --bracket_count;
      continue;
    }
    if (*s == '{') {
      ++brace_count;
      continue;
    }
    if (*s == '}') {
      if (brace_count == 0) {
        return SkipValueRes::kFatalError;
      }
      --brace_count;
      continue;
    }
  }
  return SkipValueRes::kNeedsMoreData;
}

base::Status SetOutAndReturn(const char* ptr, const char** out) {
  *out = ptr;
  return base::OkStatus();
}

uint32_t CoerceToUint32(int64_t n) {
  if (n < 0 || n > std::numeric_limits<uint32_t>::max()) {
    return 0;
  }
  return static_cast<uint32_t>(n);
}

uint32_t CoerceToUint32(double n) {
  return CoerceToUint32(static_cast<int64_t>(n));
}

inline bool CoerceToTs(const json::JsonValue& value,
                       int64_t& ts,
                       base::Status& status) {
  switch (value.index()) {
    case base::variant_index<json::JsonValue, double>(): {
      double value_dbl = base::unchecked_get<double>(value);
      ts = value_dbl == std::trunc(value_dbl)
               ? static_cast<int64_t>(value_dbl) * 1000
               : static_cast<int64_t>(std::llround(value_dbl * 1000.0));
      return true;
    }
    case base::variant_index<json::JsonValue, int64_t>():
      ts = base::unchecked_get<int64_t>(value) * 1000;
      return true;
    case base::variant_index<json::JsonValue, std::string_view>(): {
      std::string_view value_str = base::unchecked_get<std::string_view>(value);
      json::JsonValue str_parsed;
      std::string temp_str;
      const char* start = value_str.data();
      const char* end = value_str.data() + value_str.size();

      // The ParseValue function expects to see the next character after any
      // number so we need to add one to the end of the string contents (which
      // should be the quote character) to ensure we don't get
      // `kIncompleteInput`.
      const char* quote_end = value_str.data() + value_str.size() + 1;
      auto ret =
          json::ParseValue(start, quote_end, str_parsed, temp_str, status);
      switch (ret) {
        case json::ReturnCode::kOk:
          if (start != end) {
            status = base::ErrStatus(
                "Unexpected trailing characters when parsing string timestamp "
                "'%.*s'",
                static_cast<int>(value_str.size()), value_str.data());
            return false;
          }
          break;
        case json::ReturnCode::kError:
          // status is already set by ParseValue.
          return false;
        case json::ReturnCode::kIncompleteInput:
          status = base::ErrStatus(
              "Unexpected incomplete input when parsing string timestamp "
              "'%.*s'",
              static_cast<int>(value_str.size()), value_str.data());
          return false;
        case json::ReturnCode::kEndOfScope:
          status = base::ErrStatus(
              "Unexpected end of scope when parsing string timestamp '%.*s'",
              static_cast<int>(value_str.size()), value_str.data());
          return false;
      }
      if (!std::holds_alternative<double>(str_parsed) &&
          !std::holds_alternative<int64_t>(str_parsed)) {
        status = base::ErrStatus("Expected a number in string timestamp '%.*s'",
                                 static_cast<int>(value_str.size()),
                                 value_str.data());
        return false;
      }
      return CoerceToTs(str_parsed, ts, status);
    }
    default:
      status = base::ErrStatus("Expected a number in timestamp, got %zu",
                               value.index());
      return false;
  }
}

inline std::string_view GetStringValue(const json::JsonValue& value) {
  if (const auto* str = std::get_if<std::string_view>(&value)) {
    return *str;
  }
  return {};
}

inline std::string_view GetObjectValue(const json::JsonValue& value) {
  if (const auto* o = std::get_if<json::Object>(&value)) {
    return o->contents;
  }
  return {};
}

struct IdResult {
  JsonEvent::IdStrOrUint64 id;
  JsonEvent::IdType type;
};

std::optional<IdResult> ExtractId(StringPool* pool,
                                  const json::JsonValue& value) {
  switch (value.index()) {
    case base::variant_index<json::JsonValue, std::string_view>(): {
      IdResult res;
      auto str_view = base::unchecked_get<std::string_view>(value);
      res.id.id_str = pool->InternString(
          base::StringView(str_view.data(), str_view.size()));
      res.type = JsonEvent::IdType::kString;
      return res;
    }
    case base::variant_index<json::JsonValue, int64_t>(): {
      IdResult res;
      res.id.id_uint64 =
          static_cast<uint64_t>(base::unchecked_get<int64_t>(value));
      res.type = JsonEvent::IdType::kUint64;
      return res;
    }
    default:
      return std::nullopt;
  }
}

void ParseId2(json::Iterator& inner_it,
              TraceProcessorContext* context,
              std::string_view id2,
              std::optional<IdResult>& id2_local,
              std::optional<IdResult>& id2_global) {
  inner_it.Reset(id2.data(), id2.data() + id2.size());
  if (!inner_it.ParseStart()) {
    context->storage->IncrementStats(stats::json_tokenizer_failure);
    return;
  }
  for (;;) {
    switch (inner_it.ParseObjectFieldWithoutRecursing()) {
      case State::kOk:
      case State::kEndOfScope:
        break;
      case State::kError:
        context->storage->IncrementStats(stats::json_tokenizer_failure);
        return;
      case State::kIncompleteInput:
        PERFETTO_FATAL("Unexpected incomplete input in JSON object for id2");
    }
    if (inner_it.eof()) {
      break;
    }
    if (inner_it.key() == "local") {
      id2_local =
          ExtractId(context->storage->mutable_string_pool(), inner_it.value());
    } else if (inner_it.key() == "global") {
      id2_global =
          ExtractId(context->storage->mutable_string_pool(), inner_it.value());
    }
  }
}

class JsonSink : public TraceSorter::Sink<JsonEvent, JsonSink> {
 public:
  explicit JsonSink(JsonTraceParser* parser) : parser_(parser) {}
  void Parse(int64_t ts, JsonEvent data) {
    parser_->ParseJsonPacket(ts, std::move(data));
  }

 private:
  JsonTraceParser* parser_;
};
class SystraceSink : public TraceSorter::Sink<SystraceLine, SystraceSink> {
 public:
  explicit SystraceSink(JsonTraceParser* parser) : parser_(parser) {}
  void Parse(int64_t ts, SystraceLine data) {
    parser_->ParseSystraceLine(ts, std::move(data));
  }

 private:
  JsonTraceParser* parser_;
};
class V8Sink : public TraceSorter::Sink<LegacyV8CpuProfileEvent, V8Sink> {
 public:
  explicit V8Sink(LegacyV8CpuProfileTracker* tracker) : tracker_(tracker) {}
  void Parse(int64_t ts, LegacyV8CpuProfileEvent data) {
    tracker_->Parse(ts, data);
  }

 private:
  LegacyV8CpuProfileTracker* tracker_;
};

}  // namespace

ReadKeyRes ReadOneJsonKey(const char* start,
                          const char* end,
                          std::string* key,
                          const char** next) {
  enum class NextToken {
    kStringOrEndOfDict,
    kColon,
    kValue,
  };

  NextToken next_token = NextToken::kStringOrEndOfDict;
  for (const char* s = start; s < end; s++) {
    // Whitespace characters anywhere can be skipped.
    if (isspace(*s))
      continue;

    switch (next_token) {
      case NextToken::kStringOrEndOfDict: {
        // If we see a closing brace, that means we've reached the end of the
        // wrapping dictionary.
        if (*s == '}') {
          *next = s + 1;
          return ReadKeyRes::kEndOfDictionary;
        }

        // If we see a comma separator, just ignore it_.
        if (*s == ',')
          continue;

        auto res = ReadOneJsonString(s, end, key, &s);
        if (res == ReadStringRes::kFatalError)
          return ReadKeyRes::kFatalError;
        if (res == ReadStringRes::kNeedsMoreData)
          return ReadKeyRes::kNeedsMoreData;

        // We need to decrement from the pointer as the loop will increment
        // it back up.
        s--;
        next_token = NextToken::kColon;
        break;
      }
      case NextToken::kColon:
        if (*s != ':')
          return ReadKeyRes::kFatalError;
        next_token = NextToken::kValue;
        break;
      case NextToken::kValue:
        // Allowed value starting chars: [ { digit - "
        // Also allowed: true, false, null. For simplicities sake, we only check
        // against the first character as we're not trying to be super accurate.
        if (*s == '[' || *s == '{' || isdigit(*s) || *s == '-' || *s == '"' ||
            *s == 't' || *s == 'f' || *s == 'n') {
          *next = s;
          return ReadKeyRes::kFoundKey;
        }
        return ReadKeyRes::kFatalError;
    }
  }
  return ReadKeyRes::kNeedsMoreData;
}

ReadSystemLineRes ReadOneSystemTraceLine(const char* start,
                                         const char* end,
                                         std::string* line,
                                         const char** next) {
  bool is_escaping = false;
  for (const char* s = start; s < end; s++) {
    // If we get a quote character and we're not escaping, we are done with the
    // system trace string.
    if (*s == '"' && !is_escaping) {
      *next = s + 1;
      return ReadSystemLineRes::kEndOfSystemTrace;
    }

    // If we are escaping n, that means this is a new line which is a delimiter
    // for a system trace line.
    if (*s == 'n' && is_escaping) {
      *next = s + 1;
      return ReadSystemLineRes::kFoundLine;
    }

    base::Status status = AppendUnescapedCharacter(*s, is_escaping, line);
    if (!status.ok())
      return ReadSystemLineRes::kFatalError;

    // If we're in a string and we see a backslash and the last character was
    // not a backslash the next character is escaped:
    is_escaping = *s == '\\' && !is_escaping;
  }
  return ReadSystemLineRes::kNeedsMoreData;
}

JsonTraceTokenizer::JsonTraceTokenizer(TraceProcessorContext* ctx)
    : context_(ctx),
      parser_(ctx),
      v8_tracker_(std::make_unique<LegacyV8CpuProfileTracker>(ctx)),
      json_stream_(
          context_->sorter->CreateStream(std::make_unique<JsonSink>(&parser_))),
      systrace_stream_(context_->sorter->CreateStream(
          std::make_unique<SystraceSink>(&parser_))),
      v8_stream_(context_->sorter->CreateStream(
          std::make_unique<V8Sink>(v8_tracker_.get()))),
      trace_file_clock_(ClockId::TraceFile(ctx->trace_id().value)) {}
JsonTraceTokenizer::~JsonTraceTokenizer() = default;

base::Status JsonTraceTokenizer::Parse(TraceBlobView blob) {
  buffer_.insert(buffer_.end(), blob.data(), blob.data() + blob.size());
  const char* buf = buffer_.data();
  const char* next = buf;
  const char* end = buf + buffer_.size();

  if (offset_ == 0) {
    // Strip leading whitespace.
    while (next != end && isspace(*next)) {
      next++;
    }
    if (next == end) {
      return base::ErrStatus(
          "Failure parsing JSON: first chunk has only whitespace");
    }

    // Trace could begin in any of these ways:
    // {"traceEvents":[{
    // { "traceEvents": [{
    // [{
    if (*next != '{' && *next != '[') {
      return base::ErrStatus(
          "Failure parsing JSON: first non-whitespace character is not [ or {");
    }

    // Figure out the format of the JSON file based on the first non-whitespace
    // character.
    format_ = *next == '{' ? TraceFormat::kOuterDictionary
                           : TraceFormat::kOnlyTraceEvents;

    // Skip the '[' or '{' character.
    next++;

    // Set our current position based on the format of the trace.
    position_ = format_ == TraceFormat::kOuterDictionary
                    ? TracePosition::kDictionaryKey
                    : TracePosition::kInsideTraceEventsArray;
  }
  RETURN_IF_ERROR(ParseInternal(next, end, &next));

  offset_ += static_cast<uint64_t>(next - buf);
  buffer_.erase(buffer_.begin(), buffer_.begin() + (next - buf));
  return base::OkStatus();
}

base::Status JsonTraceTokenizer::ParseInternal(const char* start,
                                               const char* end,
                                               const char** out) {
  switch (position_) {
    case TracePosition::kDictionaryKey:
      return HandleDictionaryKey(start, end, out);
    case TracePosition::kInsideSystemTraceEventsString:
      return HandleSystemTraceEvent(start, end, out);
    case TracePosition::kInsideTraceEventsArray:
      return HandleTraceEvent(start, end, out);
    case TracePosition::kEof: {
      return start == end
                 ? base::OkStatus()
                 : base::ErrStatus(
                       "Failure parsing JSON: tried to parse data after EOF");
    }
  }
  PERFETTO_FATAL("For GCC");
}

base::Status JsonTraceTokenizer::HandleTraceEvent(const char* start,
                                                  const char* end,
                                                  const char** out) {
  const char* global_cur = start;
  for (;;) {
    const char* cur = global_cur;
    if (PERFETTO_UNLIKELY(!json::internal::SkipWhitespace(cur, end))) {
      return SetOutAndReturn(global_cur, out);
    }
    // Warning: the order of these checks is important. Due to bugs like
    // https://github.com/google/perfetto/issues/1822, we allow for trailing
    // commas in the trace events array, so we need to check for that first
    // before checking for the end of the array.
    if (PERFETTO_UNLIKELY(*cur == ',')) {
      if (PERFETTO_UNLIKELY(!json::internal::SkipWhitespace(++cur, end))) {
        return SetOutAndReturn(global_cur, out);
      }
    }
    if (PERFETTO_UNLIKELY(*cur == ']')) {
      if (format_ == TraceFormat::kOnlyTraceEvents) {
        position_ = TracePosition::kEof;
        return SetOutAndReturn(cur + 1, out);
      }
      position_ = TracePosition::kDictionaryKey;
      return ParseInternal(cur + 1, end, out);
    }
    it_.Reset(cur, end);
    if (!it_.ParseStart() || !ParseTraceEventContents()) {
      if (!it_.status().ok()) {
        return base::ErrStatus("Failure parsing JSON: %s",
                               it_.status().c_message());
      }
      return SetOutAndReturn(global_cur, out);
    }
    global_cur = it_.cur();
  }
}

bool JsonTraceTokenizer::ParseTraceEventContents() {
  JsonEvent event;
  base::Status status;
  int64_t ts = std::numeric_limits<int64_t>::max();
  std::optional<IdResult> id2_local;
  std::optional<IdResult> id2_global;
  for (;;) {
    switch (it_.ParseObjectFieldWithoutRecursing()) {
      case State::kOk:
      case State::kEndOfScope:
        break;
      case State::kIncompleteInput:
      case State::kError:
        return false;
    }
    if (it_.eof()) {
      break;
    }
    if (it_.key() == "ph") {
      std::string_view ph = GetStringValue(it_.value());
      event.phase = ph.size() >= 1 ? ph[0] : '\0';
    } else if (it_.key() == "ts") {
      // On failure, ts remains at max() which will be handled below.
      CoerceToTs(it_.value(), ts, status);
    } else if (it_.key() == "dur") {
      if (!CoerceToTs(it_.value(), event.dur, status)) {
        PERFETTO_DLOG("%s", status.c_message());
        context_->storage->IncrementStats(stats::json_tokenizer_failure);
        return false;
      }
    } else if (it_.key() == "pid") {
      switch (it_.value().index()) {
        case base::variant_index<json::JsonValue, std::string_view>(): {
          // If the pid is a string, treat raw id of the interned string as
          // the pid. This "hack" which allows emitting "quick-and-dirty"
          // compact JSON traces: relying on these traces for production is
          // necessarily brittle as it is not a part of the actual spec.
          std::string_view proc_name =
              base::unchecked_get<std::string_view>(it_.value());
          event.pid = context_->storage->InternString(proc_name).raw_id();
          event.pid_is_string_id = true;
          event.pid_exists = true;
          break;
        }
        case base::variant_index<json::JsonValue, int64_t>():
          event.pid = CoerceToUint32(base::unchecked_get<int64_t>(it_.value()));
          event.pid_exists = true;
          break;
        case base::variant_index<json::JsonValue, double>():
          event.pid = CoerceToUint32(base::unchecked_get<double>(it_.value()));
          event.pid_exists = true;
          break;
        default:
          break;
      }
    } else if (it_.key() == "tid") {
      switch (it_.value().index()) {
        case base::variant_index<json::JsonValue, std::string_view>(): {
          // See the comment for |pid| string handling above: the same applies
          // here.
          std::string_view thread_name =
              base::unchecked_get<std::string_view>(it_.value());
          event.tid = context_->storage->InternString(thread_name).raw_id();
          event.tid_is_string_id = true;
          event.tid_exists = true;
          break;
        }
        case base::variant_index<json::JsonValue, int64_t>():
          event.tid = CoerceToUint32(base::unchecked_get<int64_t>(it_.value()));
          event.tid_exists = true;
          break;
        case base::variant_index<json::JsonValue, double>():
          event.tid = CoerceToUint32(base::unchecked_get<double>(it_.value()));
          event.tid_exists = true;
          break;
        default:
          break;
      }
    } else if (it_.key() == "id") {
      if (auto id = ExtractId(context_->storage->mutable_string_pool(),
                              it_.value())) {
        event.id = id->id;
        event.id_type = id->type;
      }
    } else if (it_.key() == "bind_id") {
      if (auto id = ExtractId(context_->storage->mutable_string_pool(),
                              it_.value())) {
        event.bind_id = id->id;
        event.bind_id_type = id->type;
      }
    } else if (it_.key() == "cat") {
      std::string_view cat = GetStringValue(it_.value());
      event.cat =
          cat.empty() ? kNullStringId : context_->storage->InternString(cat);
    } else if (it_.key() == "name") {
      std::string_view name = GetStringValue(it_.value());
      event.name =
          name.empty() ? kNullStringId : context_->storage->InternString(name);
    } else if (it_.key() == "flow_in") {
      switch (it_.value().index()) {
        case base::variant_index<json::JsonValue, bool>():
          event.flow_in = base::unchecked_get<bool>(it_.value());
          break;
        default:
          break;
      }
    } else if (it_.key() == "flow_out") {
      switch (it_.value().index()) {
        case base::variant_index<json::JsonValue, bool>():
          event.flow_out = base::unchecked_get<bool>(it_.value());
          break;
        default:
          break;
      }
    } else if (it_.key() == "s") {
      auto value = GetStringValue(it_.value());
      if (value == "p") {
        event.scope = JsonEvent::Scope::kProcess;
      } else if (value == "t") {
        event.scope = JsonEvent::Scope::kThread;
      } else if (value == "g") {
        event.scope = JsonEvent::Scope::kGlobal;
      } else if (value.data() == nullptr) {
        event.scope = JsonEvent::Scope::kNone;
      }
    } else if (it_.key() == "bp") {
      event.bind_enclosing_slice = GetStringValue(it_.value()) == "e";
    } else if (it_.key() == "tts") {
      if (!CoerceToTs(it_.value(), event.tts, status)) {
        PERFETTO_DLOG("%s", status.c_message());
        context_->storage->IncrementStats(stats::json_tokenizer_failure);
        return false;
      }
    } else if (it_.key() == "tdur") {
      if (!CoerceToTs(it_.value(), event.tdur, status)) {
        PERFETTO_DLOG("%s", status.c_message());
        context_->storage->IncrementStats(stats::json_tokenizer_failure);
        return false;
      }
    } else if (it_.key() == "args") {
      std::string_view args = GetObjectValue(it_.value());
      if (!args.empty()) {
        event.args = std::make_unique<char[]>(args.size());
        memcpy(event.args.get(), args.data(), args.size());
        event.args_size = args.size();
      }
    } else if (it_.key() == "id2") {
      std::string_view id2 = GetObjectValue(it_.value());
      if (!id2.empty()) {
        ParseId2(inner_it_, context_, id2, id2_local, id2_global);
      }
    }
  }
  if (!event.phase) {
    context_->storage->IncrementStats(stats::json_tokenizer_failure);
    return true;
  }
  // Don't check ts for metadata events. In all other cases error.
  if (ts == std::numeric_limits<int64_t>::max()) {
    if (event.phase != 'M') {
      PERFETTO_DLOG("%s", status.c_message());
      context_->storage->IncrementStats(stats::json_tokenizer_failure);
      return true;
    }
    // If the event is a metadata event, we can set ts to 0.
    ts = 0;
  }

  // Make the tid equal to the pid if tid is not set.
  if (event.tid == 0 && event.pid != 0 && !event.tid_is_string_id) {
    event.tid = event.pid;
  }

  if (PERFETTO_LIKELY(event.id_type == JsonEvent::IdType::kNone)) {
    if (PERFETTO_UNLIKELY(id2_global)) {
      event.async_cookie_type = JsonEvent::AsyncCookieType::kId2Global;
      event.async_cookie = static_cast<int64_t>(base::MurmurHashCombine(
          event.cat.raw_id(),
          id2_global->type == JsonEvent::IdType::kString
              ? static_cast<uint64_t>(id2_global->id.id_str.raw_id())
              : id2_global->id.id_uint64));
    } else if (PERFETTO_UNLIKELY(id2_local)) {
      event.async_cookie_type = JsonEvent::AsyncCookieType::kId2Local;
      event.async_cookie = static_cast<int64_t>(base::MurmurHashCombine(
          event.cat.raw_id(),
          id2_local->type == JsonEvent::IdType::kString
              ? static_cast<uint64_t>(id2_local->id.id_str.raw_id())
              : id2_local->id.id_uint64));
    }
  } else if (event.id_type == JsonEvent::IdType::kString) {
    event.async_cookie_type = JsonEvent::AsyncCookieType::kId;
    event.async_cookie = static_cast<int64_t>(
        base::MurmurHashCombine(event.cat.raw_id(), event.id.id_str.raw_id()));
  } else if (event.id_type == JsonEvent::IdType::kUint64) {
    event.async_cookie_type = JsonEvent::AsyncCookieType::kId;
    event.async_cookie = static_cast<int64_t>(
        base::MurmurHashCombine(event.cat.raw_id(), event.id.id_uint64));
  }
  if (PERFETTO_UNLIKELY(event.phase == 'P')) {
    if (status = ParseV8SampleEvent(event); !status.ok()) {
      context_->storage->IncrementStats(stats::json_tokenizer_failure);
      return true;
    }
    return true;
  }
  auto trace_ts = context_->clock_tracker->ToTraceTime(trace_file_clock_, ts);
  if (trace_ts) {
    json_stream_->Push(*trace_ts, std::move(event));
  }
  return true;
}

base::Status JsonTraceTokenizer::ParseV8SampleEvent(const JsonEvent& event) {
  uint64_t id;
  if (event.id_type == JsonEvent::IdType::kString) {
    std::optional<uint64_t> id_opt = base::StringToUInt64(
        context_->storage->GetString(event.id.id_str).c_str(), 16);
    if (!id_opt) {
      context_->storage->IncrementStats(stats::json_tokenizer_failure);
      return base::OkStatus();
    }
    id = *id_opt;
  } else if (event.id_type == JsonEvent::IdType::kUint64) {
    id = event.id.id_uint64;
  } else {
    return base::OkStatus();
  }

  auto profile_or =
      ParseV8ProfileArgs(std::string_view(event.args.get(), event.args_size));
  if (!profile_or.ok()) {
    return base::OkStatus();
  }
  const V8Profile& profile = *profile_or;

  if (profile.start_time) {
    v8_tracker_->SetStartTsForSessionAndPid(id, event.pid,
                                            *profile.start_time * 1000);
    return base::OkStatus();
  }

  for (const auto& node : profile.nodes) {
    base::StringView url = node.call_frame.url
                               ? base::StringView(*node.call_frame.url)
                               : base::StringView();
    base::StringView function_name =
        base::StringView(node.call_frame.function_name);
    base::Status status = v8_tracker_->AddCallsite(
        id, event.pid, node.id, node.parent, url, function_name, node.children);
    if (!status.ok()) {
      context_->storage->IncrementStats(
          stats::legacy_v8_cpu_profile_invalid_callsite);
      continue;
    }
  }

  if (profile.samples.size() != profile.time_deltas.size()) {
    return base::ErrStatus(
        "v8 legacy profile: samples and timestamps do not have same size");
  }
  for (uint32_t i = 0; i < profile.samples.size(); ++i) {
    ASSIGN_OR_RETURN(int64_t ts,
                     v8_tracker_->AddDeltaAndGetTs(
                         id, event.pid, profile.time_deltas[i] * 1000));
    auto trace_ts = context_->clock_tracker->ToTraceTime(trace_file_clock_, ts);
    if (trace_ts) {
      v8_stream_->Push(*trace_ts,
                       LegacyV8CpuProfileEvent{id, event.pid, event.tid,
                                               profile.samples[i]});
    }
  }
  return base::OkStatus();
}

base::Status JsonTraceTokenizer::HandleDictionaryKey(const char* start,
                                                     const char* end,
                                                     const char** out) {
  if (format_ != TraceFormat::kOuterDictionary) {
    return base::ErrStatus(
        "Failure parsing JSON: illegal format when parsing dictionary key");
  }

  const char* next = start;
  std::string key;
  switch (ReadOneJsonKey(start, end, &key, &next)) {
    case ReadKeyRes::kFatalError:
      return base::ErrStatus(
          "Failure parsing JSON: encountered fatal error while parsing key");
    case ReadKeyRes::kEndOfDictionary:
      position_ = TracePosition::kEof;
      return SetOutAndReturn(next, out);
    case ReadKeyRes::kNeedsMoreData:
      // If we didn't manage to read the key we need to set |out| to |start|
      // (*not* |next|) to keep the state machine happy.
      return SetOutAndReturn(start, out);
    case ReadKeyRes::kFoundKey:
      break;
  }

  // ReadOneJsonKey should ensure that the first character of the value is
  // available.
  PERFETTO_CHECK(next < end);

  if (key == "traceEvents") {
    // Skip the [ character opening the array.
    if (*next != '[') {
      return base::ErrStatus(
          "Failure parsing JSON: traceEvents is not an array.");
    }
    next++;

    position_ = TracePosition::kInsideTraceEventsArray;
    return ParseInternal(next, end, out);
  }

  if (key == "systemTraceEvents") {
    // Skip the " character opening the string.
    if (*next != '"') {
      return base::ErrStatus(
          "Failure parsing JSON: systemTraceEvents is not an string.");
    }
    next++;

    position_ = TracePosition::kInsideSystemTraceEventsString;
    return ParseInternal(next, end, out);
  }

  if (key == "displayTimeUnit") {
    std::string time_unit;
    auto result = ReadOneJsonString(next, end, &time_unit, &next);
    if (result == ReadStringRes::kFatalError)
      return base::ErrStatus("Could not parse displayTimeUnit");
    context_->storage->IncrementStats(stats::json_display_time_unit);
    return ParseInternal(next, end, out);
  }

  // If we don't know the key for this JSON value just skip it_.
  switch (SkipOneJsonValue(next, end, &next)) {
    case SkipValueRes::kFatalError:
      return base::ErrStatus(
          "Failure parsing JSON: error while parsing value for key %s",
          key.c_str());
    case SkipValueRes::kNeedsMoreData:
      // If we didn't manage to read the key *and* the value, we need to set
      // |out| to |start| (*not* |next|) to keep the state machine happy (as
      // we expect to always see a key before the value).
      return SetOutAndReturn(start, out);
    case SkipValueRes::kEndOfValue:
      return ParseInternal(next, end, out);
  }
  PERFETTO_FATAL("For GCC");
}

base::Status JsonTraceTokenizer::HandleSystemTraceEvent(const char* start,
                                                        const char* end,
                                                        const char** out) {
  if (format_ != TraceFormat::kOuterDictionary) {
    return base::ErrStatus(
        "Failure parsing JSON: illegal format when parsing system events");
  }

  const char* next = start;
  while (next < end) {
    std::string raw_line;
    switch (ReadOneSystemTraceLine(next, end, &raw_line, &next)) {
      case ReadSystemLineRes::kFatalError:
        return base::ErrStatus(
            "Failure parsing JSON: encountered fatal error while parsing "
            "event inside trace event string");
      case ReadSystemLineRes::kNeedsMoreData:
        return SetOutAndReturn(next, out);
      case ReadSystemLineRes::kEndOfSystemTrace:
        position_ = TracePosition::kDictionaryKey;
        return ParseInternal(next, end, out);
      case ReadSystemLineRes::kFoundLine:
        break;
    }

    if (base::StartsWith(raw_line, "#") || raw_line.empty())
      continue;

    SystraceLine line;
    RETURN_IF_ERROR(systrace_line_tokenizer_.Tokenize(raw_line, &line));
    auto trace_ts =
        context_->clock_tracker->ToTraceTime(trace_file_clock_, line.ts);
    if (trace_ts) {
      systrace_stream_->Push(*trace_ts, std::move(line));
    }
  }
  return SetOutAndReturn(next, out);
}

base::Status JsonTraceTokenizer::OnPushDataToSorter() {
  // Phase 1: Validate trace is complete
  return position_ == TracePosition::kEof ||
                 (position_ == TracePosition::kInsideTraceEventsArray &&
                  format_ == TraceFormat::kOnlyTraceEvents)
             ? base::OkStatus()
             : base::ErrStatus("JSON trace file is incomplete");
}

}  // namespace perfetto::trace_processor
