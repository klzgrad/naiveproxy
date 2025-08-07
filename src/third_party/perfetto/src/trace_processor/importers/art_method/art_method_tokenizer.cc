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

#include "src/trace_processor/importers/art_method/art_method_tokenizer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/ext/base/variant.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/art_method/art_method_event.h"
#include "src/trace_processor/importers/common/stack_profile_tracker.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/trace_blob_view_reader.h"

#include "protos/perfetto/common/builtin_clock.pbzero.h"

namespace perfetto::trace_processor::art_method {
namespace {

constexpr uint32_t kTraceMagic = 0x574f4c53;  // 'SLOW'
constexpr uint32_t kStreamingVersionMask = 0xF0U;
constexpr uint32_t kTraceHeaderLength = 32;

constexpr uint32_t kMethodsCode = 1;
constexpr uint32_t kThreadsCode = 2;
constexpr uint32_t kSummaryCode = 3;

std::string_view ToStringView(const TraceBlobView& tbv) {
  return {reinterpret_cast<const char*>(tbv.data()), tbv.size()};
}

std::string ConstructPathname(const std::string& class_name,
                              const std::string& pathname) {
  size_t index = class_name.rfind('/');
  if (index != std::string::npos && base::EndsWith(pathname, ".java")) {
    return class_name.substr(0, index + 1) + pathname;
  }
  return pathname;
}

uint64_t ToLong(const TraceBlobView& tbv) {
  uint64_t x = 0;
  memcpy(base::AssumeLittleEndian(&x), tbv.data(), tbv.size());
  return x;
}

uint32_t ToInt(const TraceBlobView& tbv) {
  uint32_t x = 0;
  memcpy(base::AssumeLittleEndian(&x), tbv.data(), tbv.size());
  return x;
}

uint16_t ToShort(const TraceBlobView& tbv) {
  uint16_t x = 0;
  memcpy(base::AssumeLittleEndian(&x), tbv.data(), tbv.size());
  return x;
}

}  // namespace

ArtMethodTokenizer::ArtMethodTokenizer(TraceProcessorContext* ctx)
    : context_(ctx) {}
ArtMethodTokenizer::~ArtMethodTokenizer() = default;

base::Status ArtMethodTokenizer::Parse(TraceBlobView blob) {
  reader_.PushBack(std::move(blob));
  if (sub_parser_.index() == base::variant_index<SubParser, Detect>()) {
    auto smagic = reader_.SliceOff(reader_.start_offset(), 4);
    if (!smagic) {
      return base::OkStatus();
    }
    uint32_t magic = ToInt(*smagic);
    sub_parser_ = magic == kTraceMagic ? SubParser{Streaming{this}}
                                       : SubParser{NonStreaming{this}};
    context_->clock_tracker->SetTraceTimeClock(
        protos::pbzero::BUILTIN_CLOCK_MONOTONIC);
  }
  if (sub_parser_.index() == base::variant_index<SubParser, Streaming>()) {
    return std::get<Streaming>(sub_parser_).Parse();
  }
  return std::get<NonStreaming>(sub_parser_).Parse();
}

base::Status ArtMethodTokenizer::ParseMethodLine(std::string_view l) {
  auto tokens = base::SplitString(base::TrimWhitespace(std::string(l)), "\t");
  auto id = base::StringToUInt32(tokens[0], 16);
  if (!id) {
    return base::ErrStatus(
        "ART method trace: unable to parse method id as integer: %s",
        tokens[0].c_str());
  }

  std::string class_name = tokens[1];
  std::string method_name;
  std::string signature;
  std::optional<StringId> pathname;
  std::optional<uint32_t> line_number;
  // Below logic was taken from:
  // https://cs.android.com/android-studio/platform/tools/base/+/mirror-goog-studio-main:perflib/src/main/java/com/android/tools/perflib/vmtrace/VmTraceParser.java;l=251
  // It's not clear why this complexity is strictly needed (maybe backcompat
  // or certain configurations of method tracing) but it's best to stick
  // closely to the official parser implementation.
  if (tokens.size() == 6) {
    method_name = tokens[2];
    signature = tokens[3];
    pathname = context_->storage->InternString(
        base::StringView(ConstructPathname(class_name, tokens[4])));
    line_number = base::StringToUInt32(tokens[5]);
  } else if (tokens.size() > 2) {
    if (base::StartsWith(tokens[3], "(")) {
      method_name = tokens[2];
      signature = tokens[3];
      if (tokens.size() >= 5) {
        pathname = context_->storage->InternString(base::StringView(tokens[4]));
      }
    } else {
      pathname = context_->storage->InternString(base::StringView(tokens[2]));
      line_number = base::StringToUInt32(tokens[3]);
    }
  }
  base::StackString<2048> slice_name("%s.%s: %s", class_name.c_str(),
                                     method_name.c_str(), signature.c_str());
  method_map_[*id] = {
      context_->storage->InternString(slice_name.string_view()),
      pathname,
      line_number,
  };
  return base::OkStatus();
}

base::Status ArtMethodTokenizer::ParseOptionLine(std::string_view l) {
  std::string line(l);
  auto res = base::SplitString(line, "=");
  if (res.size() != 2) {
    return base::ErrStatus(
        "ART method tracing: unable to parse option (line %s)", line.c_str());
  }
  if (res[0] == "clock") {
    if (res[1] == "dual") {
      clock_ = kDual;
    } else if (res[1] == "wall") {
      clock_ = kWall;
    } else if (res[1] == "thread-cpu") {
      return base::ErrStatus(
          "ART method tracing: thread-cpu clock is *not* supported. Use wall "
          "or dual clocks");
    } else {
      return base::ErrStatus("ART method tracing: unknown clock %s",
                             res[1].c_str());
    }
  }
  return base::OkStatus();
}

base::Status ArtMethodTokenizer::ParseRecord(uint32_t tid,
                                             const TraceBlobView& record) {
  ArtMethodEvent evt{};
  evt.tid = tid;
  if (auto* it = thread_map_.Find(tid); it && !it->comm_used) {
    evt.comm = it->comm;
    it->comm_used = true;
  }

  uint32_t methodid_action = ToInt(record.slice_off(0, 4));
  uint32_t ts_delta = clock_ == kDual ? ToInt(record.slice_off(8, 4))
                                      : ToInt(record.slice_off(4, 4));

  uint32_t action = methodid_action & 0x03;
  uint32_t method_id = methodid_action & ~0x03u;

  const auto& m = method_map_[method_id];
  evt.method = m.name;
  evt.pathname = m.pathname;
  evt.line_number = m.line_number;
  switch (action) {
    case 0:
      evt.action = ArtMethodEvent::kEnter;
      break;
    case 1:
    case 2:
      evt.action = ArtMethodEvent::kExit;
      break;
  }
  ASSIGN_OR_RETURN(int64_t ts, context_->clock_tracker->ToTraceTime(
                                   protos::pbzero::BUILTIN_CLOCK_MONOTONIC,
                                   (ts_ + ts_delta) * 1000));
  context_->sorter->PushArtMethodEvent(ts, evt);
  return base::OkStatus();
}

base::Status ArtMethodTokenizer::ParseThread(uint32_t tid,
                                             const std::string& comm) {
  thread_map_.Insert(
      tid, {context_->storage->InternString(base::StringView(comm)), false});
  return base::OkStatus();
}

base::Status ArtMethodTokenizer::Streaming::Parse() {
  auto it = tokenizer_->reader_.GetIterator();
  PERFETTO_CHECK(it.MaybeAdvance(it_offset_));
  for (bool cnt = true; cnt;) {
    switch (mode_) {
      case kHeaderStart: {
        ASSIGN_OR_RETURN(cnt, ParseHeaderStart(it));
        break;
      }
      case kData: {
        ASSIGN_OR_RETURN(cnt, ParseData(it));
        break;
      }
      case kSummaryDone: {
        mode_ = kDone;
        cnt = false;
        break;
      }
      case kDone: {
        return base::ErrStatus(
            "ART method trace: unexpected data after eof marker");
      }
    }
    if (cnt) {
      it_offset_ = it.file_offset();
    }
  }
  return base::OkStatus();
}

base::StatusOr<bool> ArtMethodTokenizer::Streaming::ParseHeaderStart(
    Iterator& it) {
  auto header = it.MaybeRead(kTraceHeaderLength);
  if (!header) {
    return false;
  }
  uint32_t magic = ToInt(header->slice_off(0, 4));
  if (magic != kTraceMagic) {
    return base::ErrStatus("ART Method trace: expected start-header magic");
  }
  tokenizer_->version_ =
      ToShort(header->slice_off(4, 2)) ^ kStreamingVersionMask;
  tokenizer_->ts_ = static_cast<int64_t>(ToLong(header->slice_off(8, 8)));
  switch (tokenizer_->version_) {
    case 1:
      tokenizer_->record_size_ = 9;
      break;
    case 2:
      tokenizer_->record_size_ = 10;
      break;
    case 3:
      tokenizer_->record_size_ = ToShort(header->slice_off(16, 2));
      break;
    default:
      PERFETTO_FATAL("Illegal version %u", tokenizer_->version_);
  }
  mode_ = kData;
  return true;
}

base::StatusOr<bool> ArtMethodTokenizer::Streaming::ParseData(Iterator& it) {
  std::optional<TraceBlobView> op_tbv = it.MaybeRead(2);
  if (!op_tbv) {
    return false;
  }
  uint32_t op = ToShort(*op_tbv);
  if (op != 0) {
    // Just skip past the record: this will be handled later.
    // -2 because we already the tid above which forms part of the record.
    return it.MaybeAdvance(tokenizer_->record_size_ - 2);
  }
  std::optional<TraceBlobView> code_tbv = it.MaybeRead(1);
  if (!code_tbv) {
    return false;
  }
  uint8_t code = *code_tbv->data();
  switch (code) {
    case kSummaryCode: {
      std::optional<TraceBlobView> summary_len_tbv = it.MaybeRead(4);
      if (!summary_len_tbv) {
        return false;
      }
      uint32_t summary_len = ToInt(*summary_len_tbv);
      std::optional<TraceBlobView> summary_tbv = it.MaybeRead(summary_len);
      if (!summary_tbv) {
        return false;
      }
      RETURN_IF_ERROR(ParseSummary(ToStringView(*summary_tbv)));
      mode_ = kSummaryDone;
      return true;
    }
    case kMethodsCode: {
      std::optional<TraceBlobView> method_len_tbv = it.MaybeRead(2);
      if (!method_len_tbv) {
        return false;
      }
      uint32_t method_len = ToShort(*method_len_tbv);
      std::optional<TraceBlobView> method_tbv = it.MaybeRead(method_len);
      if (!method_tbv) {
        return false;
      }
      RETURN_IF_ERROR(tokenizer_->ParseMethodLine(ToStringView(*method_tbv)));
      return true;
    }
    case kThreadsCode: {
      std::optional<TraceBlobView> tid_tbv = it.MaybeRead(2);
      if (!tid_tbv) {
        return false;
      }
      std::optional<TraceBlobView> comm_len_tbv = it.MaybeRead(2);
      if (!comm_len_tbv) {
        return false;
      }
      uint32_t comm_len = ToShort(*comm_len_tbv);
      std::optional<TraceBlobView> comm_tbv = it.MaybeRead(comm_len);
      if (!comm_tbv) {
        return false;
      }
      RETURN_IF_ERROR(tokenizer_->ParseThread(
          ToShort(*tid_tbv), std::string(ToStringView(*comm_tbv))));
      return true;
    }
    default:
      return base::ErrStatus("ART method trace: unknown opcode encountered %d",
                             code);
  }
}

base::Status ArtMethodTokenizer::Streaming::ParseSummary(
    std::string_view summary) const {
  base::StringSplitter s(std::string(summary), '\n');

  // First two lines should be version and line number respectively.
  if (!s.Next() || !s.Next() || !s.Next()) {
    return base::ErrStatus(
        "ART method trace: unexpected format of summary section");
  }

  // Parse lines until we hit "*threads" as the line.
  for (;;) {
    std::string_view line(s.cur_token(), s.cur_token_size());
    if (line == "*threads") {
      return base::OkStatus();
    }
    RETURN_IF_ERROR(tokenizer_->ParseOptionLine(line));
    if (!s.Next()) {
      return base::ErrStatus(
          "ART method trace: reached end of file before EOF marker");
    }
  }
}

base::Status ArtMethodTokenizer::Streaming::NotifyEndOfFile() {
  if (mode_ != kDone) {
    return base::ErrStatus("ART Method trace: trace is incomplete");
  }

  auto it = tokenizer_->reader_.GetIterator();
  PERFETTO_CHECK(it.MaybeAdvance(kTraceHeaderLength));
  for (;;) {
    std::optional<TraceBlobView> tid_tbv = it.MaybeRead(2);
    uint32_t tid = ToShort(*tid_tbv);
    if (tid == 0) {
      uint8_t code = *it.MaybeRead(1)->data();
      switch (code) {
        case kSummaryCode:
          return base::OkStatus();
        case kMethodsCode: {
          PERFETTO_CHECK(it.MaybeAdvance(ToShort(*it.MaybeRead(2))));
          break;
        }
        case kThreadsCode: {
          // Advance past the tid.
          PERFETTO_CHECK(it.MaybeAdvance(2));
          PERFETTO_CHECK(it.MaybeAdvance(ToShort(*it.MaybeRead(2))));
          break;
        }
        default:
          PERFETTO_FATAL("Should not be reached");
      }
      continue;
    }
    RETURN_IF_ERROR(tokenizer_->ParseRecord(
        tid, *it.MaybeRead(tokenizer_->record_size_ - 2)));
  }
}

base::Status ArtMethodTokenizer::NonStreaming::Parse() {
  auto it = tokenizer_->reader_.GetIterator();
  for (bool cnt = true; cnt;) {
    switch (mode_) {
      case kHeaderStart: {
        ASSIGN_OR_RETURN(cnt, ParseHeaderStart(it));
        break;
      }
      case kHeaderVersion: {
        ASSIGN_OR_RETURN(cnt, ParseHeaderVersion(it));
        break;
      }
      case kHeaderOptions: {
        ASSIGN_OR_RETURN(cnt, ParseHeaderOptions(it));
        break;
      }
      case kHeaderThreads: {
        ASSIGN_OR_RETURN(cnt, ParseHeaderThreads(it));
        break;
      }
      case kHeaderMethods: {
        ASSIGN_OR_RETURN(cnt, ParseHeaderMethods(it));
        break;
      }
      case kDataHeader: {
        ASSIGN_OR_RETURN(cnt, ParseDataHeader(it));
        break;
      }
      case kData: {
        size_t s = it.file_offset();
        for (size_t i = s;; i += tokenizer_->record_size_) {
          auto record =
              tokenizer_->reader_.SliceOff(i, tokenizer_->record_size_);
          if (!record) {
            PERFETTO_CHECK(it.MaybeAdvance(i - s));
            cnt = false;
            break;
          }
          uint32_t tid = tokenizer_->version_ == 1
                             ? record->data()[0]
                             : ToShort(record->slice_off(0, 2));
          RETURN_IF_ERROR(tokenizer_->ParseRecord(
              tid, record->slice_off(2, record->size() - 2)));
        }
        break;
      }
    }
  }
  tokenizer_->reader_.PopFrontUntil(it.file_offset());
  return base::OkStatus();
}

base::Status ArtMethodTokenizer::NonStreaming::NotifyEndOfFile() const {
  if (mode_ == NonStreaming::kData && tokenizer_->reader_.empty()) {
    return base::OkStatus();
  }
  return base::ErrStatus("ART Method trace: trace is incomplete");
}

base::StatusOr<bool> ArtMethodTokenizer::NonStreaming::ParseHeaderStart(
    Iterator& it) {
  auto raw = it.MaybeFindAndRead('\n');
  if (!raw) {
    return false;
  }
  RETURN_IF_ERROR(ParseHeaderSectionLine(ToStringView(*raw)));
  return true;
}

base::StatusOr<bool> ArtMethodTokenizer::NonStreaming::ParseHeaderVersion(
    Iterator& it) {
  auto line = it.MaybeFindAndRead('\n');
  if (!line) {
    return false;
  }
  std::string version_str(ToStringView(*line));
  auto version = base::StringToInt32(version_str);
  if (!version || *version < 1 || *version > 3) {
    return base::ErrStatus("ART Method trace: trace version (%s) not supported",
                           version_str.c_str());
  }
  tokenizer_->version_ = static_cast<uint32_t>(*version);
  mode_ = kHeaderOptions;
  return true;
}

base::StatusOr<bool> ArtMethodTokenizer::NonStreaming::ParseHeaderOptions(
    Iterator& it) {
  for (auto r = it.MaybeFindAndRead('\n'); r; r = it.MaybeFindAndRead('\n')) {
    std::string_view l = ToStringView(*r);
    if (l[0] == '*') {
      RETURN_IF_ERROR(ParseHeaderSectionLine(l));
      return true;
    }
    RETURN_IF_ERROR(tokenizer_->ParseOptionLine(l));
  }
  return false;
}

base::StatusOr<bool> ArtMethodTokenizer::NonStreaming::ParseHeaderThreads(
    Iterator& it) {
  for (auto r = it.MaybeFindAndRead('\n'); r; r = it.MaybeFindAndRead('\n')) {
    std::string_view l = ToStringView(*r);
    if (l[0] == '*') {
      RETURN_IF_ERROR(ParseHeaderSectionLine(l));
      return true;
    }
    std::string line(l);
    auto tokens = base::SplitString(line, "\t");
    if (tokens.size() != 2) {
      return base::ErrStatus(
          "ART method tracing: expected only one tab in thread line (context: "
          "%s)",
          line.c_str());
    }
    std::optional<uint32_t> tid = base::StringToUInt32(tokens[0]);
    if (!tid) {
      return base::ErrStatus(
          "ART method tracing: failed parse tid in thread line (context: %s)",
          tokens[0].c_str());
    }
    RETURN_IF_ERROR(tokenizer_->ParseThread(*tid, tokens[1]));
  }
  return false;
}

base::StatusOr<bool> ArtMethodTokenizer::NonStreaming::ParseHeaderMethods(
    Iterator& it) {
  for (auto r = it.MaybeFindAndRead('\n'); r; r = it.MaybeFindAndRead('\n')) {
    std::string_view l = ToStringView(*r);
    if (l[0] == '*') {
      RETURN_IF_ERROR(ParseHeaderSectionLine(l));
      return true;
    }
    RETURN_IF_ERROR(tokenizer_->ParseMethodLine(l));
  }
  return false;
}

base::StatusOr<bool> ArtMethodTokenizer::NonStreaming::ParseDataHeader(
    Iterator& it) {
  auto header = it.MaybeRead(kTraceHeaderLength);
  if (!header) {
    return false;
  }
  uint32_t magic = ToInt(header->slice_off(0, 4));
  if (magic != kTraceMagic) {
    return base::ErrStatus("ART Method trace: expected start-header magic");
  }
  uint16_t version = ToShort(header->slice_off(4, 2));
  if (version != tokenizer_->version_) {
    return base::ErrStatus(
        "ART Method trace: trace version does not match data version");
  }
  tokenizer_->ts_ = static_cast<int64_t>(ToLong(header->slice_off(8, 8)));
  switch (tokenizer_->version_) {
    case 1:
      tokenizer_->record_size_ = 9;
      break;
    case 2:
      tokenizer_->record_size_ = 10;
      break;
    case 3:
      tokenizer_->record_size_ = ToShort(header->slice_off(16, 2));
      break;
    default:
      PERFETTO_FATAL("Illegal version %u", tokenizer_->version_);
  }
  mode_ = kData;
  return true;
}

base::Status ArtMethodTokenizer::NonStreaming::ParseHeaderSectionLine(
    std::string_view line) {
  if (line == "*version") {
    mode_ = kHeaderVersion;
    return base::OkStatus();
  }
  if (line == "*threads") {
    mode_ = kHeaderThreads;
    return base::OkStatus();
  }
  if (line == "*methods") {
    mode_ = kHeaderMethods;
    return base::OkStatus();
  }
  if (line == "*end") {
    mode_ = kDataHeader;
    return base::OkStatus();
  }
  return base::ErrStatus(
      "ART Method trace: unexpected line (%s) when expecting section header "
      "(line starting with *)",
      std::string(line).c_str());
}

base::Status ArtMethodTokenizer::NotifyEndOfFile() {
  switch (sub_parser_.index()) {
    case base::variant_index<SubParser, Detect>():
      return base::ErrStatus("ART Method trace: trace is incomplete");
    case base::variant_index<SubParser, Streaming>():
      return std::get<Streaming>(sub_parser_).NotifyEndOfFile();
    case base::variant_index<SubParser, NonStreaming>():
      return std::get<NonStreaming>(sub_parser_).NotifyEndOfFile();
  }
  PERFETTO_FATAL("For GCC");
}

}  // namespace perfetto::trace_processor::art_method
