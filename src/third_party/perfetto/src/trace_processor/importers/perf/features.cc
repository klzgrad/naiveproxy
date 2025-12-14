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

#include "src/trace_processor/importers/perf/features.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/perf/perf_event.h"
#include "src/trace_processor/importers/perf/reader.h"

namespace perfetto::trace_processor::perf_importer::feature {
namespace {

struct BuildIdRecord {
  static constexpr uint8_t kMaxSize = 20;
  char data[kMaxSize];
  uint8_t size;
  uint8_t reserved[3];
};

uint8_t CountTrailingZeros(const BuildIdRecord& build_id) {
  for (uint8_t i = 0; i < BuildIdRecord::kMaxSize; ++i) {
    if (build_id.data[BuildIdRecord::kMaxSize - i - 1] != 0) {
      return i;
    }
  }
  return sizeof(build_id.data);
}

// BuildIds are usually SHA-1 hashes (20 bytes), sometimes MD5 (16 bytes),
// sometimes 8 bytes long. Simpleperf adds trailing zeros up to 20. Do a best
// guess based on the number of trailing zeros.
uint8_t GuessBuildIdSize(const BuildIdRecord& build_id) {
  static_assert(BuildIdRecord::kMaxSize == 20);
  uint8_t len = BuildIdRecord::kMaxSize - CountTrailingZeros(build_id);
  if (len > 16) {
    return BuildIdRecord::kMaxSize;
  }
  if (len > 8) {
    return 16;
  }
  return 8;
}

bool ParseString(Reader& reader, std::string& out) {
  uint32_t len;
  base::StringView str;
  if (!reader.Read(len) || len == 0 || !reader.ReadStringView(str, len)) {
    return false;
  }

  if (str.at(len - 1) != '\0') {
    return false;
  }

  // Strings are padded with null values, stop at first null
  out = std::string(str.data());
  return true;
}

bool ParseBuildId(const perf_event_header& header,
                  TraceBlobView blob,
                  BuildId& out) {
  Reader reader(std::move(blob));

  BuildIdRecord build_id;

  if (!reader.Read(out.pid) || !reader.Read(build_id) ||
      !reader.ReadStringUntilEndOrNull(out.filename)) {
    return false;
  }

  if (header.misc & PERF_RECORD_MISC_EXT_RESERVED) {
    if (build_id.size > BuildIdRecord::kMaxSize) {
      return false;
    }
  } else {
    // Probably a simpleperf trace. Simpleperf fills build_ids with zeros up
    // to a length of 20 and leaves the rest uninitialized :( so we can not read
    // build_id.size or build_id.reserved to do any checks.
    // TODO(b/334978369): We should be able to tell for sure whether this is
    // simpleperf or not by checking the existence of SimpleperfMetaInfo.
    build_id.size = GuessBuildIdSize(build_id);
  }
  out.build_id = std::string(build_id.data, build_id.size);
  return true;
}

base::Status ParseEventTypeInfo(std::string value, SimpleperfMetaInfo& out) {
  for (const auto& line : base::SplitString(value, "\n")) {
    auto tokens = base::SplitString(line, ",");
    if (tokens.size() != 3) {
      return base::ErrStatus("Invalid event_type_info: '%s'", line.c_str());
    }

    auto type = base::StringToUInt32(tokens[1]);
    if (!type) {
      return base::ErrStatus("Could not parse type in event_type_info: '%s'",
                             tokens[1].c_str());
    }
    auto config = base::StringToUInt64(tokens[2]);
    if (!config) {
      return base::ErrStatus("Could not parse config in event_type_info: '%s'",
                             tokens[2].c_str());
    }

    out.event_type_info.Insert({*type, *config}, std::move(tokens[0]));
  }

  return base::OkStatus();
}

base::Status ParseSimpleperfMetaInfoEntry(
    std::pair<std::string, std::string> entry,
    SimpleperfMetaInfo& out) {
  static constexpr char kEventTypeInfoKey[] = "event_type_info";
  if (entry.first == kEventTypeInfoKey) {
    return ParseEventTypeInfo(std::move(entry.second), out);
  }

  PERFETTO_CHECK(
      out.entries.Insert(std::move(entry.first), std::move(entry.second))
          .second);
  return base::OkStatus();
}

}  // namespace

// static
base::Status BuildId::Parse(TraceBlobView bytes,
                            std::function<base::Status(BuildId)> cb) {
  Reader reader(std::move(bytes));
  while (reader.size_left() != 0) {
    perf_event_header header;
    TraceBlobView payload;
    if (!reader.Read(header)) {
      return base::ErrStatus(
          "Failed to parse feature BuildId. Could not read header.");
    }
    if (header.size < sizeof(header)) {
      return base::ErrStatus(
          "Failed to parse feature BuildId. Invalid size in header.");
    }
    if (!reader.ReadBlob(payload, header.size - sizeof(header))) {
      return base::ErrStatus(
          "Failed to parse feature BuildId. Could not read payload.");
    }

    BuildId build_id;
    if (!ParseBuildId(header, std::move(payload), build_id)) {
      return base::ErrStatus(
          "Failed to parse feature BuildId. Could not read entry.");
    }

    RETURN_IF_ERROR(cb(std::move(build_id)));
  }
  return base::OkStatus();
}

// static
base::Status SimpleperfMetaInfo::Parse(const TraceBlobView& bytes,
                                       SimpleperfMetaInfo& out) {
  auto* it_end = reinterpret_cast<const char*>(bytes.data() + bytes.size());
  for (auto* it = reinterpret_cast<const char*>(bytes.data()); it != it_end;) {
    auto end = std::find(it, it_end, '\0');
    if (end == it_end) {
      return base::ErrStatus("Failed to read key from Simpleperf MetaInfo");
    }
    std::string key(it, end);
    it = end;
    ++it;
    if (it == it_end) {
      return base::ErrStatus("Missing value in Simpleperf MetaInfo");
    }
    end = std::find(it, it_end, '\0');
    if (end == it_end) {
      return base::ErrStatus("Failed to read value from Simpleperf MetaInfo");
    }
    std::string value(it, end);
    it = end;
    ++it;

    RETURN_IF_ERROR(ParseSimpleperfMetaInfoEntry(
        std::make_pair(std::move(key), std::move(value)), out));
  }
  return base::OkStatus();
}

// static
base::Status EventDescription::Parse(
    TraceBlobView bytes,
    std::function<base::Status(EventDescription)> cb) {
  Reader reader(std::move(bytes));
  uint32_t nr;
  uint32_t attr_size;
  if (!reader.Read(nr) || !reader.Read(attr_size)) {
    return base::ErrStatus("Failed to parse header for PERF_EVENT_DESC");
  }

  for (; nr != 0; --nr) {
    EventDescription desc;
    uint32_t nr_ids;
    if (!reader.ReadPerfEventAttr(desc.attr, attr_size) ||
        !reader.Read(nr_ids) || !ParseString(reader, desc.event_string)) {
      return base::ErrStatus("Failed to parse record for PERF_EVENT_DESC");
    }

    desc.ids.resize(nr_ids);
    for (uint64_t& id : desc.ids) {
      if (!reader.Read(id)) {
        return base::ErrStatus("Failed to parse ids for PERF_EVENT_DESC");
      }
    }
    RETURN_IF_ERROR(cb(std::move(desc)));
  }
  return base::OkStatus();
}

base::Status ParseSimpleperfFile2(TraceBlobView bytes,
                                  std::function<void(TraceBlobView)> cb) {
  Reader reader(std::move(bytes));
  while (reader.size_left() != 0) {
    uint32_t len;
    if (!reader.Read(len)) {
      return base::ErrStatus("Failed to parse len in FEATURE_SIMPLEPERF_FILE2");
    }
    TraceBlobView payload;
    if (!reader.ReadBlob(payload, len)) {
      return base::ErrStatus(
          "Failed to parse payload in FEATURE_SIMPLEPERF_FILE2");
    }
    cb(std::move(payload));
  }
  return base::OkStatus();
}

// static
base::Status HeaderGroupDesc::Parse(TraceBlobView bytes, HeaderGroupDesc& out) {
  Reader reader(std::move(bytes));
  uint32_t nr;
  if (!reader.Read(nr)) {
    return base::ErrStatus("Failed to parse header for HEADER_GROUP_DESC");
  }

  HeaderGroupDesc group_desc;
  group_desc.entries.resize(nr);
  for (auto& e : group_desc.entries) {
    if (!ParseString(reader, e.string) || !reader.Read(e.leader_idx) ||
        !reader.Read(e.nr_members)) {
      return base::ErrStatus("Failed to parse HEADER_GROUP_DESC entry");
    }
  }
  out = std::move(group_desc);
  return base::OkStatus();
}

base::StatusOr<std::vector<std::string>> ParseCmdline(TraceBlobView bytes) {
  Reader reader(std::move(bytes));
  uint32_t nr;
  if (!reader.Read(nr)) {
    return base::ErrStatus("Failed to parse nr for CMDLINE");
  }

  std::vector<std::string> args;
  args.reserve(nr);
  for (; nr != 0; --nr) {
    args.emplace_back();
    if (!ParseString(reader, args.back())) {
      return base::ErrStatus("Failed to parse string for CMDLINE");
    }
  }
  return args;
}

base::StatusOr<std::string> ParseOsRelease(TraceBlobView bytes) {
  Reader reader(std::move(bytes));
  std::string str;
  if (!ParseString(reader, str)) {
    return base::ErrStatus("Failed to parse string for OS_RELEASE");
  }
  return str;
}

}  // namespace perfetto::trace_processor::perf_importer::feature
