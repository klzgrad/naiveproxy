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
#include <memory>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/no_destructor.h"
#include "perfetto/protozero/proto_utils.h"

#include "protos/perfetto/trace/trace.pbzero.h"

namespace perfetto::trace_processor {
namespace {

constexpr uint8_t kTracePacketTag =
    protozero::proto_utils::MakeTagLengthDelimited(
        protos::pbzero::Trace::kPacketFieldNumber);

}  // namespace

TraceImporterBase::~TraceImporterBase() = default;

TraceImporterId TraceImporterRegistry::Register(
    std::unique_ptr<TraceImporterBase> importer) {
  TraceImporterId id = importer->id();
  PERFETTO_CHECK(importers_.Insert(id, std::move(importer)).second);
  return id;
}

const TraceTypeDescriptor* TraceImporterRegistry::Find(
    TraceImporterId id) const {
  if (const TraceImporterBase* importer = FindImporter(id)) {
    return &importer->descriptor();
  }
  // Unregistered ids (the "no match" sentinel) are described as unknown so
  // callers never see nullptr.
  static base::NoDestructor<TraceTypeDescriptor> unknown([] {
    TraceTypeDescriptor d;
    d.name = "unknown";
    return d;
  }());
  return &unknown.ref();
}

const TraceImporterBase* TraceImporterRegistry::FindImporter(
    TraceImporterId id) const {
  auto* it = importers_.Find(id);
  return it ? it->get() : nullptr;
}

TraceImporterId TraceImporterRegistry::Guess(const uint8_t* data,
                                             size_t size) const {
  // Sniff every importer in detection_priority order, lowest first. Priorities
  // are globally unique so the order is total.
  struct Entry {
    TraceImporterId id;
    const TraceImporterBase* importer;
  };
  std::vector<Entry> entries;
  for (auto it = importers_.GetIterator(); it; ++it) {
    entries.push_back({it.key(), it.value().get()});
  }
  std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
    return a.importer->descriptor().detection_priority <
           b.importer->descriptor().detection_priority;
  });
  for (const Entry& e : entries) {
    if (e.importer->Sniff(data, size)) {
      return e.id;
    }
  }
  return TraceImporterId();
}

const char* TraceImporterRegistry::ToString(TraceImporterId id) const {
  return Find(id)->name.c_str();
}

bool TraceImporterRegistry::IsContainer(TraceImporterId id) const {
  return Find(id)->is_container;
}

CompressedTraceType SniffCompressedTraceType(const uint8_t* data, size_t size) {
  if (size >= 2 && data[0] == 0x1f && data[1] == 0x8b) {
    return CompressedTraceType::kGzip;
  }
  if (size >= 4 && data[0] == 0x28 && data[1] == 0xb5 && data[2] == 0x2f &&
      data[3] == 0xfd) {
    return CompressedTraceType::kZstd;
  }
  // A raw proto trace starts with the length-delimited Trace.packet field tag.
  if (size > 0 && data[0] == kTracePacketTag) {
    return CompressedTraceType::kProto;
  }
  return CompressedTraceType::kOther;
}

}  // namespace perfetto::trace_processor
