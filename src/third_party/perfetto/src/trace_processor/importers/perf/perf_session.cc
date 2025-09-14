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

#include "src/trace_processor/importers/perf/perf_session.h"

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/trace_processor/ref_counted.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "protos/perfetto/common/builtin_clock.pbzero.h"
#include "src/trace_processor/importers/perf/perf_event.h"
#include "src/trace_processor/importers/perf/perf_event_attr.h"
#include "src/trace_processor/importers/perf/reader.h"
#include "src/trace_processor/storage/trace_storage.h"  // IWYU pragma: keep
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/build_id.h"

namespace perfetto::trace_processor::perf_importer {
namespace {
bool OffsetsMatch(const PerfEventAttr& attr, const PerfEventAttr& other) {
  return attr.id_offset_from_start() == other.id_offset_from_start() &&
         (!attr.sample_id_all() ||
          attr.id_offset_from_end() == other.id_offset_from_end());
}
}  // namespace

base::StatusOr<RefPtr<PerfSession>> PerfSession::Builder::Build() {
  if (attr_with_ids_.empty()) {
    return base::ErrStatus("No perf_event_attr");
  }

  auto perf_session_id =
      context_->storage->mutable_perf_session_table()->Insert({}).id;

  RefPtr<PerfEventAttr> first_attr;
  base::FlatHashMap<uint64_t, RefPtr<PerfEventAttr>> attrs_by_id;
  for (const auto& entry : attr_with_ids_) {
    RefPtr<PerfEventAttr> attr(
        new PerfEventAttr(context_, perf_session_id, entry.attr));
    if (!first_attr) {
      first_attr = attr;
    }
    if (first_attr->sample_id_all() != attr->sample_id_all()) {
      return base::ErrStatus(
          "perf_event_attr with different sample_id_all values");
    }

    if (!OffsetsMatch(*first_attr, *attr)) {
      return base::ErrStatus("perf_event_attr with different id offsets");
    }

    for (uint64_t id : entry.ids) {
      if (!attrs_by_id.Insert(id, attr).second) {
        return base::ErrStatus(
            "Same id maps to multiple perf_event_attr: %" PRIu64, id);
      }
    }
  }
  if (attr_with_ids_.size() > 1 &&
      (!first_attr->id_offset_from_start().has_value() ||
       (first_attr->sample_id_all() &&
        !first_attr->id_offset_from_end().has_value()))) {
    return base::ErrStatus("No id offsets for multiple perf_event_attr");
  }
  return RefPtr<PerfSession>(
      new PerfSession(context_, perf_session_id, std::move(first_attr),
                      std::move(attrs_by_id), attr_with_ids_.size() == 1));
}

base::StatusOr<RefPtr<PerfEventAttr>> PerfSession::FindAttrForRecord(
    const perf_event_header& header,
    const TraceBlobView& payload) const {
  if (header.type >= PERF_RECORD_USER_TYPE_START) {
    return RefPtr<PerfEventAttr>();
  }

  if (has_single_perf_event_attr_) {
    return first_attr_;
  }

  if (header.type != PERF_RECORD_SAMPLE && !first_attr_->sample_id_all()) {
    return first_attr_;
  }

  uint64_t id;
  if (!ReadEventId(header, payload, id)) {
    return base::ErrStatus("Failed to read record id");
  }

  if (id == 0) {
    return first_attr_;
  }

  auto it = FindAttrForEventId(id);
  if (!it) {
    return base::ErrStatus("No perf_event_attr for id %" PRIu64, id);
  }
  return it;
}

bool PerfSession::ReadEventId(const perf_event_header& header,
                              const TraceBlobView& payload,
                              uint64_t& id) const {
  const PerfEventAttr& first = *attrs_by_id_.GetIterator().value();
  Reader reader(payload.copy());

  if (header.type != PERF_RECORD_SAMPLE) {
    PERFETTO_CHECK(first.id_offset_from_end().has_value());
    if (reader.size_left() < *first.id_offset_from_end()) {
      return false;
    }
    const size_t off = reader.size_left() - *first.id_offset_from_end();
    return reader.Skip(off) && reader.Read(id);
  }
  PERFETTO_CHECK(first.id_offset_from_start().has_value());
  return reader.Skip(*first.id_offset_from_start()) && reader.Read(id);
}

RefPtr<PerfEventAttr> PerfSession::FindAttrForEventId(uint64_t id) const {
  auto* it = attrs_by_id_.Find(id);
  if (!it) {
    return {};
  }
  return RefPtr<PerfEventAttr>(it->get());
}

void PerfSession::SetEventName(uint64_t event_id, std::string name) {
  auto* it = attrs_by_id_.Find(event_id);
  if (!it) {
    return;
  }
  (*it)->set_event_name(std::move(name));
}

void PerfSession::SetEventName(uint32_t type,
                               uint64_t config,
                               const std::string& name) {
  for (auto it = attrs_by_id_.GetIterator(); it; ++it) {
    if (it.value()->type() == type && it.value()->config() == config) {
      it.value()->set_event_name(name);
    }
  }
}

void PerfSession::AddBuildId(int32_t pid,
                             std::string filename,
                             BuildId build_id) {
  build_ids_.Insert({pid, std::move(filename)}, std::move(build_id));
}

std::optional<BuildId> PerfSession::LookupBuildId(
    uint32_t pid,
    const std::string& filename) const {
  // -1 is used in BUILD_ID feature to match any pid.
  static constexpr int32_t kAnyPid = -1;
  auto* it = build_ids_.Find({static_cast<int32_t>(pid), filename});
  if (!it) {
    it = build_ids_.Find({kAnyPid, filename});
  }
  return it ? std::make_optional(*it) : std::nullopt;
}

void PerfSession::SetCmdline(const std::vector<std::string>& args) {
  context_->storage->mutable_perf_session_table()
      ->FindById(perf_session_id_)
      ->set_cmdline(context_->storage->InternString(
          base::StringView(base::Join(args, " "))));
}

bool PerfSession::HasPerfClock() const {
  for (auto it = attrs_by_id_.GetIterator(); it; ++it) {
    if (it.value()->clock_id() == protos::pbzero::BUILTIN_CLOCK_PERF) {
      return true;
    }
  }
  return false;
}

}  // namespace perfetto::trace_processor::perf_importer
