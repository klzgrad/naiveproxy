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

#include "src/trace_processor/util/trace_type.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/protozero/proto_decoder.h"
#include "perfetto/protozero/proto_utils.h"
#include "src/trace_processor/importers/android_bugreport/android_log_event.h"
#include "src/trace_processor/importers/perf_text/perf_text_sample_line_parser.h"

#include "protos/perfetto/trace/trace.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "protos/third_party/pprof/profile.pbzero.h"

namespace perfetto::trace_processor {
namespace {
// Fuchsia traces have a magic number as documented here:
// https://fuchsia.googlesource.com/fuchsia/+/HEAD/docs/development/tracing/trace-format/README.md#magic-number-record-trace-info-type-0
constexpr char kFuchsiaMagic[] = {'\x10', '\x00', '\x04', '\x46',
                                  '\x78', '\x54', '\x16', '\x00'};
constexpr char kPerfMagic[] = {'P', 'E', 'R', 'F', 'I', 'L', 'E', '2'};
constexpr char kZipMagic[] = {'P', 'K', '\x03', '\x04'};
constexpr char kGzipMagic[] = {'\x1f', '\x8b'};
constexpr char kArtMethodStreamingMagic[] = {'S', 'L', 'O', 'W'};
constexpr char kArtHprofStreamingMagic[] = {'J', 'A', 'V', 'A', ' ', 'P',
                                            'R', 'O', 'F', 'I', 'L', 'E'};
constexpr char kTarPosixMagic[] = {'u', 's', 't', 'a', 'r', '\0'};
constexpr char kTarGnuMagic[] = {'u', 's', 't', 'a', 'r', ' ', ' ', '\0'};
constexpr size_t kTarMagicOffset = 257;
constexpr char kSimpleperfMagic[] = {'S', 'I', 'M', 'P', 'L',
                                     'E', 'P', 'E', 'R', 'F'};

constexpr uint8_t kTracePacketTag =
    protozero::proto_utils::MakeTagLengthDelimited(
        protos::pbzero::Trace::kPacketFieldNumber);
constexpr uint16_t kModuleSymbolsTag =
    protozero::proto_utils::MakeTagLengthDelimited(
        protos::pbzero::TracePacket::kModuleSymbolsFieldNumber);

std::string RemoveWhitespace(std::string str) {
  str.erase(std::remove_if(str.begin(), str.end(), base::IsSpace), str.end());
  return str;
}

template <size_t N>
bool MatchesMagic(const uint8_t* data,
                  size_t size,
                  const char (&magic)[N],
                  size_t offset = 0) {
  if (size < N + offset) {
    return false;
  }

  return memcmp(data + offset, magic, N) == 0;
}

bool IsProtoTraceWithSymbols(const uint8_t* ptr, size_t size) {
  const uint8_t* const end = ptr + size;

  uint64_t tag;
  const uint8_t* next = protozero::proto_utils::ParseVarInt(ptr, end, &tag);

  if (next == ptr || tag != kTracePacketTag) {
    return false;
  }

  ptr = next;
  uint64_t field_length;
  next = protozero::proto_utils::ParseVarInt(ptr, end, &field_length);
  if (next == ptr) {
    return false;
  }
  ptr = next;

  if (field_length == 0) {
    return false;
  }

  next = protozero::proto_utils::ParseVarInt(ptr, end, &tag);
  if (next == ptr) {
    return false;
  }

  return tag == kModuleSymbolsTag;
}

bool IsPprofProfile(const uint8_t* data, size_t size) {
  using perfetto::third_party::perftools::profiles::pbzero::Profile;
  using protozero::proto_utils::ProtoWireType;
  // Minimum size to parse a protobuf tag and small varint
  constexpr size_t kMinPprofSize = 10;
  if (size < kMinPprofSize) {
    return false;
  }

  bool has_core_pprof_field = false;
  protozero::ProtoDecoder decoder(data, size);
  for (auto fld = decoder.ReadField(); fld.valid(); fld = decoder.ReadField()) {
    switch (fld.id()) {
      case Profile::kSampleFieldNumber:
      case Profile::kMappingFieldNumber:
      case Profile::kLocationFieldNumber:
      case Profile::kFunctionFieldNumber:
      case Profile::kStringTableFieldNumber:
        has_core_pprof_field = true;
        [[fallthrough]];
      case Profile::kSampleTypeFieldNumber:
      case Profile::kPeriodTypeFieldNumber:
        if (fld.type() != ProtoWireType::kLengthDelimited) {
          return false;
        }
        break;
      case Profile::kCommentFieldNumber:
        if (fld.type() != ProtoWireType::kLengthDelimited &&
            fld.type() != ProtoWireType::kVarInt) {
          return false;
        }
        break;
      case Profile::kDropFramesFieldNumber:
      case Profile::kKeepFramesFieldNumber:
        has_core_pprof_field = true;
        [[fallthrough]];
      case Profile::kTimeNanosFieldNumber:
      case Profile::kDurationNanosFieldNumber:
      case Profile::kPeriodFieldNumber:
      case Profile::kDefaultSampleTypeFieldNumber:
        if (fld.type() != ProtoWireType::kVarInt) {
          return false;
        }
        break;
      default:
        return false;
    }
  }
  return has_core_pprof_field;
}

// Checks if a line looks like a valid collapsed stack line:
// frame1;frame2;frame3 count
bool IsCollapsedStackLine(const char* line_start, size_t line_len) {
  // Skip leading whitespace
  size_t start = 0;
  while (start < line_len && base::IsSpace(line_start[start])) {
    ++start;
  }
  if (start >= line_len || line_start[start] == '#') {
    return false;  // Empty or comment - not definitive
  }

  // Trim trailing whitespace
  size_t end = line_len;
  while (end > start && base::IsSpace(line_start[end - 1])) {
    --end;
  }

  size_t len = end - start;
  if (len == 0) {
    return false;
  }

  const char* line = line_start + start;

  // Find the last space - count should be after it
  size_t last_space = len;
  for (size_t i = len; i > 0; --i) {
    if (line[i - 1] == ' ') {
      last_space = i - 1;
      break;
    }
  }

  if (last_space == len || last_space == 0) {
    return false;  // No space found, or space at start
  }

  // Check that everything after the last space is a digit
  for (size_t i = last_space + 1; i < len; ++i) {
    if (!std::isdigit(static_cast<unsigned char>(line[i]))) {
      return false;
    }
  }

  // Check that the stack part contains at least one semicolon
  bool has_semicolon = false;
  for (size_t i = 0; i < last_space; ++i) {
    if (line[i] == ';') {
      has_semicolon = true;
      break;
    }
  }
  return has_semicolon;
}

bool IsCollapsedStackFormat(const uint8_t* data, size_t size) {
  const char* str = reinterpret_cast<const char*>(data);

  // Look at first few non-empty, non-comment lines
  size_t valid_lines = 0;
  size_t pos = 0;

  while (pos < size && valid_lines < 3) {
    // Find end of line
    size_t nl = pos;
    while (nl < size && str[nl] != '\n') {
      ++nl;
    }

    size_t line_len = nl - pos;

    // Skip empty/whitespace-only lines and comments for counting
    size_t start = pos;
    while (start < nl && base::IsSpace(str[start])) {
      ++start;
    }

    if (start < nl && str[start] != '#') {
      if (!IsCollapsedStackLine(str + pos, line_len)) {
        return false;
      }
      ++valid_lines;
    }

    pos = (nl < size) ? nl + 1 : size;
  }

  return valid_lines > 0;
}

}  // namespace

const char* TraceTypeToString(TraceType trace_type) {
  switch (trace_type) {
    case kJsonTraceType:
      return "json";
    case kProtoTraceType:
      return "proto";
    case kSymbolsTraceType:
      return "symbols";
    case kNinjaLogTraceType:
      return "ninja_log";
    case kFuchsiaTraceType:
      return "fuchsia";
    case kSystraceTraceType:
      return "systrace";
    case kGzipTraceType:
      return "gzip";
    case kCtraceTraceType:
      return "ctrace";
    case kZipFile:
      return "zip";
    case kPerfDataTraceType:
      return "perf";
    case kPprofTraceType:
      return "pprof";
    case kCollapsedStackTraceType:
      return "collapsed_stack";
    case kInstrumentsXmlTraceType:
      return "instruments_xml";
    case kAndroidLogcatTraceType:
      return "android_logcat";
    case kAndroidDumpstateTraceType:
      return "android_dumpstate";
    case kAndroidBugreportTraceType:
      return "android_bugreport";
    case kGeckoTraceType:
      return "gecko";
    case kArtMethodTraceType:
      return "art_method";
    case kArtHprofTraceType:
      return "art_hprof";
    case kPerfTextTraceType:
      return "perf_text";
    case kPrimesTraceType:
      return "primes";
    case kSimpleperfProtoTraceType:
      return "simpleperf_proto";
    case kUnknownTraceType:
      return "unknown";
    case kTarTraceType:
      return "tar";
  }
  PERFETTO_FATAL("For GCC");
}

bool IsContainerTraceType(TraceType trace_type) {
  switch (trace_type) {
    case kGzipTraceType:
    case kCtraceTraceType:
    case kZipFile:
    case kAndroidBugreportTraceType:
    case kTarTraceType:
      return true;
    case kJsonTraceType:
    case kPrimesTraceType:
    case kProtoTraceType:
    case kSymbolsTraceType:
    case kNinjaLogTraceType:
    case kFuchsiaTraceType:
    case kSystraceTraceType:
    case kPerfDataTraceType:
    case kPprofTraceType:
    case kCollapsedStackTraceType:
    case kInstrumentsXmlTraceType:
    case kAndroidLogcatTraceType:
    case kAndroidDumpstateTraceType:
    case kGeckoTraceType:
    case kArtMethodTraceType:
    case kArtHprofTraceType:
    case kPerfTextTraceType:
    case kSimpleperfProtoTraceType:
    case kUnknownTraceType:
      return false;
  }
  PERFETTO_FATAL("For GCC");
}

TraceType GuessTraceType(const uint8_t* data, size_t size) {
  if (size == 0) {
    return kUnknownTraceType;
  }

  if (MatchesMagic(data, size, kTarPosixMagic, kTarMagicOffset)) {
    return kTarTraceType;
  }

  if (MatchesMagic(data, size, kTarGnuMagic, kTarMagicOffset)) {
    return kTarTraceType;
  }

  if (MatchesMagic(data, size, kFuchsiaMagic)) {
    return kFuchsiaTraceType;
  }

  if (MatchesMagic(data, size, kPerfMagic)) {
    return kPerfDataTraceType;
  }
  if (MatchesMagic(data, size, kSimpleperfMagic)) {
    return kSimpleperfProtoTraceType;
  }

  if (MatchesMagic(data, size, kZipMagic)) {
    return kZipFile;
  }

  if (MatchesMagic(data, size, kGzipMagic)) {
    return kGzipTraceType;
  }

  if (MatchesMagic(data, size, kArtMethodStreamingMagic)) {
    return kArtMethodTraceType;
  }

  if (MatchesMagic(data, size, kArtHprofStreamingMagic)) {
    return kArtHprofTraceType;
  }

  std::string start(reinterpret_cast<const char*>(data),
                    std::min<size_t>(size, kGuessTraceMaxLookahead));

  std::string start_minus_white_space = RemoveWhitespace(start);
  // Generated by the Gecko conversion script built into perf.
  if (base::StartsWith(start_minus_white_space, "{\"meta\""))
    return kGeckoTraceType;
  // Generated by the simpleperf conversion script.
  if (base::StartsWith(start_minus_white_space, "{\"libs\""))
    return kGeckoTraceType;
  if (base::StartsWith(start_minus_white_space, "{\""))
    return kJsonTraceType;
  if (base::StartsWith(start_minus_white_space, "[{\""))
    return kJsonTraceType;

  // ART method traces (non-streaming).
  if (base::StartsWith(start, "*version\n"))
    return kArtMethodTraceType;

  // Systrace with header but no leading HTML.
  if (base::Contains(start, "# tracer"))
    return kSystraceTraceType;

  // Systrace with leading HTML.
  // Both: <!DOCTYPE html> and <!DOCTYPE HTML> have been observed.
  std::string lower_start = base::ToLower(start);
  if (base::StartsWith(lower_start, "<!doctype html>") ||
      base::StartsWith(lower_start, "<html>"))
    return kSystraceTraceType;

  // MacOS Instruments XML export.
  if (base::StartsWith(start, "<?xml version=\"1.0\"?>\n<trace-query-result>"))
    return kInstrumentsXmlTraceType;

  // Traces obtained from atrace -z (compress).
  // They all have the string "TRACE:" followed by 78 9C which is a zlib header
  // for "deflate, default compression, window size=32K" (see b/208691037)
  if (base::Contains(start, "TRACE:\n\x78\x9c"))
    return kCtraceTraceType;

  // Traces obtained from atrace without -z (no compression).
  if (base::Contains(start, "TRACE:\n"))
    return kSystraceTraceType;

  // Traces obtained from trace-cmd report.
  if (base::StartsWith(start, "cpus="))
    return kSystraceTraceType;

  // Ninja's build log (.ninja_log).
  if (base::StartsWith(start, "# ninja log"))
    return kNinjaLogTraceType;

  if (AndroidLogEvent::IsAndroidLogcat(data, size)) {
    return kAndroidLogcatTraceType;
  }

  // Collapsed stack format (flamegraph input format).
  if (IsCollapsedStackFormat(data, size))
    return kCollapsedStackTraceType;

  // Perf text format.
  if (perf_text_importer::IsPerfTextFormatTrace(data, size))
    return kPerfTextTraceType;

  // Systrace with no header or leading HTML.
  if (base::StartsWith(start, " "))
    return kSystraceTraceType;

  if (IsProtoTraceWithSymbols(data, size))
    return kSymbolsTraceType;

  if (IsPprofProfile(data, size))
    return kPprofTraceType;

  if (base::StartsWith(start, "\x0a"))
    return kProtoTraceType;

  // TODO(leemh): This is not robust enough. Chat elkurdi@/lalitm@ to determine
  // better way.
  if (base::StartsWith(start, "\x09"))
    return kPrimesTraceType;

  if (base::StartsWith(start, "9,0,i,vers,")) {
    return kAndroidDumpstateTraceType;  // BatteryStats Checkin format.
  }

  if (base::StartsWith(start,
                       "======================================================="
                       "=\n== dumpstate: ")) {
    return kAndroidDumpstateTraceType;
  }

  return kUnknownTraceType;
}

}  // namespace perfetto::trace_processor
