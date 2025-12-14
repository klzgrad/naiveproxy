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

#include "src/trace_processor/importers/proto/winscope/viewcapture_args_parser.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "protos/perfetto/trace/interned_data/interned_data.pbzero.h"
#include "protos/perfetto/trace/profiling/profile_common.pbzero.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"

namespace perfetto::trace_processor::winscope {

namespace {
using InternedString = protos::pbzero::InternedString;
using ViewCaptureRow = tables::ViewCaptureTable::RowReference;
using ViewRow = tables::ViewCaptureViewTable::RowReference;
using ConstChars = protozero::ConstChars;
}  // namespace

ViewCaptureArgsParser::ViewCaptureArgsParser(
    int64_t packet_timestamp,
    ArgsTracker::BoundInserter& inserter,
    TraceStorage& storage,
    PacketSequenceStateGeneration* sequence_state,
    ViewCaptureRow* snapshot_row,
    ViewRow* view_row)
    : ArgsParser(packet_timestamp, inserter, storage, sequence_state),
      storage_{storage},
      snapshot_row_(snapshot_row),
      view_row_(view_row) {}

void ViewCaptureArgsParser::AddInteger(const Key& key, int64_t value) {
  if (TryAddDeinternedString(key, static_cast<uint64_t>(value))) {
    return;
  }
  ArgsParser::AddInteger(key, value);
}

void ViewCaptureArgsParser::AddUnsignedInteger(const Key& key, uint64_t value) {
  if (TryAddDeinternedString(key, value)) {
    return;
  }
  ArgsParser::AddUnsignedInteger(key, value);
}

bool ViewCaptureArgsParser::TryAddDeinternedString(const Key& key,
                                                   uint64_t iid) {
  bool is_interned_field = base::EndsWith(key.key, "_iid");
  if (!is_interned_field) {
    return false;
  }

  const auto deintern_key = key.key.substr(0, key.key.size() - 4);
  const auto deintern_flat_key =
      key.flat_key.substr(0, key.flat_key.size() - 4);
  const auto deintern_key_combined = Key{deintern_flat_key, deintern_key};
  const auto deintern_val = TryDeinternString(key, iid);

  if (!deintern_val) {
    ArgsParser::AddString(deintern_key_combined,
                          ConstChars{ERROR_MSG.data(), ERROR_MSG.size()});
    storage_.IncrementStats(
        stats::winscope_viewcapture_missing_interned_string_parse_errors);
    return false;
  }

  ArgsParser::AddString(deintern_key_combined, *deintern_val);

  IidToStringMap& iid_args =
      flat_key_to_iid_args[storage_.InternString(key.flat_key)];
  iid_args.Insert(iid, storage_.InternString(*deintern_val));

  return true;
}

std::optional<ConstChars> ViewCaptureArgsParser::TryDeinternString(
    const Key& key,
    uint64_t iid) {
  using protos::pbzero::InternedData;
  if (base::EndsWith(key.key, "class_name_iid")) {
    return DeinternString<InternedData::kViewcaptureClassNameFieldNumber>(
        iid, view_row_, &ViewRow::set_class_name);
  }
  if (base::EndsWith(key.key, "package_name_iid")) {
    return DeinternString<InternedData::kViewcapturePackageNameFieldNumber>(
        iid, snapshot_row_, &ViewCaptureRow::set_package_name);
  }
  if (base::EndsWith(key.key, "view_id_iid")) {
    return DeinternString<InternedData::kViewcaptureViewIdFieldNumber>(
        iid, view_row_, &ViewRow::set_view_id);
  }
  if (base::EndsWith(key.key, "window_name_iid")) {
    return DeinternString<InternedData::kViewcaptureWindowNameFieldNumber>(
        iid, snapshot_row_, &ViewCaptureRow::set_window_name);
  }
  return std::nullopt;
}

template <uint32_t FieldNumber, typename RowRef>
std::optional<ConstChars> ViewCaptureArgsParser::DeinternString(
    uint64_t iid,
    RowRef* row,
    void (RowRef::*setter)(StringPool::Id)) {
  auto* decoder =
      seq_state()->LookupInternedMessage<FieldNumber, InternedString>(iid);
  if (!decoder) {
    return std::nullopt;
  }
  auto blob = decoder->str();
  auto chars = ConstChars{reinterpret_cast<const char*>(blob.data), blob.size};
  if (row) {
    base::StringView sv(chars);
    (row->*setter)(storage_.mutable_string_pool()->InternString(sv));
  }
  return chars;
}

}  // namespace perfetto::trace_processor::winscope
