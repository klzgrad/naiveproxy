/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "src/trace_processor/plugins/winscope_importer/surfaceflinger_transactions_parser.h"
#include <optional>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/base64.h"
#include "perfetto/ext/base/string_view.h"
#include "protos/third_party/android/frameworks/native/tracing/winscope/surfaceflinger_transactions.pbzero.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/stats_tracker.h"
#include "src/trace_processor/importers/proto/args_parser.h"
#include "src/trace_processor/plugins/winscope_importer/winscope_proto_mapping.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto {
namespace trace_processor {

SurfaceFlingerTransactionsParser::SurfaceFlingerTransactionsParser(
    TraceProcessorContext* context)
    : context_{context},
      args_parser_{*context->descriptor_pool_,
                   *context->storage->mutable_string_pool()} {}

void SurfaceFlingerTransactionsParser::Parse(int64_t timestamp,
                                             protozero::ConstBytes blob) {
  tables::SurfaceFlingerTransactionsTable::Row row;
  row.ts = timestamp;
  row.base64_proto_id = context_->storage->mutable_string_pool()
                            ->InternString(base::StringView(
                                base::Base64Encode(blob.data, blob.size)))
                            .raw_id();

  com::android::internal::pbzero::TransactionTraceEntry::Decoder
      snapshot_decoder(blob);
  row.vsync_id = snapshot_decoder.vsync_id();

  auto snapshot_id =
      context_->storage->mutable_surfaceflinger_transactions_table()
          ->Insert(row)
          .id;

  ArgsTracker args_tracker(context_);
  auto inserter = args_tracker.AddArgsTo(snapshot_id);
  ArgsParser writer(timestamp, inserter, *context_->storage,
                    *context_->process_tracker);
  base::Status status = args_parser_.ParseMessage(
      blob,
      *util::winscope_proto_mapping::GetProtoName(
          tables::SurfaceFlingerTransactionsTable::Name()),
      nullptr /* parse all fields */, writer);
  if (!status.ok()) {
    context_->stats_tracker->IncrementStats(
        stats::winscope_sf_transactions_parse_errors);
  }

  for (auto it = snapshot_decoder.transactions(); it; ++it) {
    ParseTransaction(timestamp, *it, snapshot_id);
  }

  for (auto it = snapshot_decoder.added_layers(); it; ++it) {
    ParseAddedLayer(timestamp, *it, snapshot_id);
  }

  for (auto it = snapshot_decoder.destroyed_layers(); it; ++it) {
    tables::SurfaceFlingerTransactionTable::Row transaction;
    transaction.snapshot_id = snapshot_id;
    transaction.layer_id = *it;
    transaction.transaction_type =
        context_->storage->mutable_string_pool()->InternString(
            "LAYER_DESTROYED");
    context_->storage->mutable_surfaceflinger_transaction_table()->Insert(
        transaction);
  }

  for (auto it = snapshot_decoder.added_displays(); it; ++it) {
    ParseDisplayState(
        timestamp, *it, snapshot_id,
        context_->storage->mutable_string_pool()->InternString("DISPLAY_ADDED"),
        std::nullopt, std::nullopt, std::nullopt, std::nullopt);
  }

  for (auto it = snapshot_decoder.removed_displays(); it; ++it) {
    tables::SurfaceFlingerTransactionTable::Row transaction;
    transaction.snapshot_id = snapshot_id;
    transaction.display_id = *it;
    transaction.transaction_type =
        context_->storage->mutable_string_pool()->InternString(
            "DISPLAY_REMOVED");
    context_->storage->mutable_surfaceflinger_transaction_table()->Insert(
        transaction);
  }

  for (auto it = snapshot_decoder.destroyed_layer_handles(); it; ++it) {
    tables::SurfaceFlingerTransactionTable::Row transaction;
    transaction.snapshot_id = snapshot_id;
    transaction.layer_id = *it;
    transaction.transaction_type =
        context_->storage->mutable_string_pool()->InternString(
            "LAYER_HANDLE_DESTROYED");
    context_->storage->mutable_surfaceflinger_transaction_table()->Insert(
        transaction);
  }
}

void SurfaceFlingerTransactionsParser::ParseTransaction(
    int64_t timestamp,
    protozero::ConstBytes transaction,
    tables::SurfaceFlingerTransactionsTable::Id snapshot_id) {
  com::android::internal::pbzero::TransactionState::Decoder transaction_decoder(
      transaction);

  auto transaction_id = transaction_decoder.transaction_id();
  auto pid = transaction_decoder.pid();
  auto uid = transaction_decoder.uid();

  auto has_layer_changes = transaction_decoder.has_layer_changes() &&
                           transaction_decoder.layer_changes()->valid();
  auto has_display_changes = transaction_decoder.has_display_changes() &&
                             transaction_decoder.display_changes()->valid();

  if (!has_layer_changes && !has_display_changes) {
    AddNoopRow(snapshot_id, transaction_id, pid, uid);
    return;
  }

  if (has_layer_changes) {
    for (auto it = transaction_decoder.layer_changes(); it; ++it) {
      AddLayerChangedRow(timestamp, *it, snapshot_id, transaction_id, pid, uid,
                         transaction);
    }
  }

  if (has_display_changes) {
    for (auto it = transaction_decoder.display_changes(); it; ++it) {
      ParseDisplayState(timestamp, *it, snapshot_id,
                        context_->storage->mutable_string_pool()->InternString(
                            "DISPLAY_CHANGED"),
                        transaction_id, pid, uid, transaction);
    }
  }
}

void SurfaceFlingerTransactionsParser::ParseAddedLayer(
    int64_t timestamp,
    protozero::ConstBytes layer_creation_args,
    tables::SurfaceFlingerTransactionsTable::Id snapshot_id) {
  com::android::internal::pbzero::LayerCreationArgs::Decoder decoder(
      layer_creation_args);

  tables::SurfaceFlingerTransactionTable::Row transaction;
  transaction.snapshot_id = snapshot_id;
  transaction.layer_id = decoder.layer_id();
  transaction.transaction_type =
      context_->storage->mutable_string_pool()->InternString("LAYER_ADDED");

  transaction.base64_proto_id =
      context_->storage->mutable_string_pool()
          ->InternString(base::StringView(base::Base64Encode(
              layer_creation_args.data, layer_creation_args.size)))
          .raw_id();
  auto row_id = context_->storage->mutable_surfaceflinger_transaction_table()
                    ->Insert(transaction)
                    .id;

  AddArgs(timestamp, layer_creation_args, row_id,
          ".com.android.internal.LayerCreationArgs", std::nullopt);
}

void SurfaceFlingerTransactionsParser::AddNoopRow(
    tables::SurfaceFlingerTransactionsTable::Id snapshot_id,
    uint64_t transaction_id,
    int32_t pid,
    int32_t uid) {
  tables::SurfaceFlingerTransactionTable::Row transaction;
  transaction.snapshot_id = snapshot_id;
  transaction.transaction_id = static_cast<int64_t>(transaction_id);
  transaction.pid = static_cast<uint32_t>(pid);
  transaction.uid = static_cast<uint32_t>(uid);
  transaction.transaction_type =
      context_->storage->mutable_string_pool()->InternString("NOOP");
  context_->storage->mutable_surfaceflinger_transaction_table()->Insert(
      transaction);
}

void SurfaceFlingerTransactionsParser::AddLayerChangedRow(
    int64_t timestamp,
    protozero::ConstBytes layer_state,
    tables::SurfaceFlingerTransactionsTable::Id snapshot_id,
    uint64_t transaction_id,
    int32_t pid,
    int32_t uid,
    protozero::ConstBytes transaction) {
  tables::SurfaceFlingerTransactionTable::Row row;
  row.snapshot_id = snapshot_id;
  row.transaction_id = static_cast<int64_t>(transaction_id);
  row.pid = pid;
  row.uid = uid;

  com::android::internal::pbzero::LayerState::Decoder state_decoder(
      layer_state);
  row.layer_id = state_decoder.layer_id();
  row.transaction_type =
      context_->storage->mutable_string_pool()->InternString("LAYER_CHANGED");

  row.base64_proto_id = context_->storage->mutable_string_pool()
                            ->InternString(base::StringView(base::Base64Encode(
                                layer_state.data, layer_state.size)))
                            .raw_id();

  if (state_decoder.has_what()) {
    auto what = state_decoder.what();
    auto flags_id = layer_flag_ids_.find(what);
    if (flags_id != layer_flag_ids_.end()) {
      row.flags_id = flags_id->second;
    } else {
      auto curr_size = static_cast<uint32_t>(layer_flag_ids_.size()) +
                       static_cast<uint32_t>(display_flag_ids_.size());
      row.flags_id = curr_size;
      layer_flag_ids_[what] = curr_size;

      auto all_flags_low = std::vector<int32_t>{
          com::android::internal::pbzero::LayerState::ePositionChanged,
          com::android::internal::pbzero::LayerState::eLayerChanged,
          com::android::internal::pbzero::LayerState::eAlphaChanged,
          com::android::internal::pbzero::LayerState::eMatrixChanged,
          com::android::internal::pbzero::LayerState::eTransparentRegionChanged,
          com::android::internal::pbzero::LayerState::eFlagsChanged,
          com::android::internal::pbzero::LayerState::eLayerStackChanged,
          com::android::internal::pbzero::LayerState::
              eReleaseBufferListenerChanged,
          com::android::internal::pbzero::LayerState::eShadowRadiusChanged,
          com::android::internal::pbzero::LayerState::eShadowRadiusChanged,
          com::android::internal::pbzero::LayerState::eBufferCropChanged,
          com::android::internal::pbzero::LayerState::eRelativeLayerChanged,
          com::android::internal::pbzero::LayerState::eReparent,
          com::android::internal::pbzero::LayerState::eColorChanged,
          com::android::internal::pbzero::LayerState::eBufferTransformChanged,
          com::android::internal::pbzero::LayerState::
              eTransformToDisplayInverseChanged,
          com::android::internal::pbzero::LayerState::eCropChanged,
          com::android::internal::pbzero::LayerState::eBufferChanged,
          com::android::internal::pbzero::LayerState::eAcquireFenceChanged,
          com::android::internal::pbzero::LayerState::eDataspaceChanged,
          com::android::internal::pbzero::LayerState::eHdrMetadataChanged,
          com::android::internal::pbzero::LayerState::
              eSurfaceDamageRegionChanged,
          com::android::internal::pbzero::LayerState::eApiChanged,
          com::android::internal::pbzero::LayerState::eSidebandStreamChanged,
          com::android::internal::pbzero::LayerState::eColorTransformChanged,
          com::android::internal::pbzero::LayerState::
              eHasListenerCallbacksChanged,
          com::android::internal::pbzero::LayerState::eInputInfoChanged,
          com::android::internal::pbzero::LayerState::eCornerRadiusChanged,
      };
      auto low_tokens = DecodeFlags(what & 0xFFFFFFFFULL, all_flags_low);
      auto low_translated = std::vector<std::string>{};
      for (auto flag : low_tokens) {
        low_translated.push_back(
            com::android::internal::pbzero::LayerState_ChangesLsb_Name(
                static_cast<
                    com::android::internal::pbzero::LayerState_ChangesLsb>(
                    flag)));
      }
      AddFlags(low_translated, row.flags_id.value());

      auto all_flags_high = std::vector<int32_t>{
          com::android::internal::pbzero::LayerState::eDestinationFrameChanged,
          com::android::internal::pbzero::LayerState::eCachedBufferChanged,
          com::android::internal::pbzero::LayerState::eBackgroundColorChanged,
          com::android::internal::pbzero::LayerState::eMetadataChanged,
          com::android::internal::pbzero::LayerState::
              eColorSpaceAgnosticChanged,
          com::android::internal::pbzero::LayerState::
              eFrameRateSelectionPriority,
          com::android::internal::pbzero::LayerState::eFrameRateChanged,
          com::android::internal::pbzero::LayerState::
              eBackgroundBlurRadiusChanged,
          com::android::internal::pbzero::LayerState::eProducerDisconnect,
          com::android::internal::pbzero::LayerState::
              eFixedTransformHintChanged,
          com::android::internal::pbzero::LayerState::eFrameNumberChanged,
          com::android::internal::pbzero::LayerState::eBlurRegionsChanged,
          com::android::internal::pbzero::LayerState::eAutoRefreshChanged,
          com::android::internal::pbzero::LayerState::eStretchChanged,
          com::android::internal::pbzero::LayerState::eTrustedOverlayChanged,
          com::android::internal::pbzero::LayerState::eDropInputModeChanged,
      };
      auto high_tokens = DecodeFlags(what >> 32, all_flags_high);
      auto high_translated = std::vector<std::string>{};
      for (auto flag : high_tokens) {
        high_translated.push_back(
            com::android::internal::pbzero::LayerState_ChangesMsb_Name(
                static_cast<
                    com::android::internal::pbzero::LayerState_ChangesMsb>(
                    flag)));
      }
      AddFlags(high_translated, row.flags_id.value());
    }
  }

  auto row_id = context_->storage->mutable_surfaceflinger_transaction_table()
                    ->Insert(row)
                    .id;
  AddArgs(timestamp, layer_state, row_id, ".com.android.internal.LayerState",
          transaction);
}

void SurfaceFlingerTransactionsParser::ParseDisplayState(
    int64_t timestamp,
    protozero::ConstBytes display_state,
    tables::SurfaceFlingerTransactionsTable::Id snapshot_id,
    StringPool::Id transaction_type,
    std::optional<uint64_t> transaction_id,
    std::optional<int32_t> pid,
    std::optional<int32_t> uid,
    std::optional<protozero::ConstBytes> transaction) {
  tables::SurfaceFlingerTransactionTable::Row row;
  row.snapshot_id = snapshot_id;
  row.transaction_type = transaction_type;
  if (transaction_id.has_value()) {
    row.transaction_id = static_cast<int64_t>(transaction_id.value());
  }
  if (pid.has_value()) {
    row.pid = pid.value();
  }
  if (uid.has_value()) {
    row.uid = uid.value();
  }

  com::android::internal::pbzero::DisplayState::Decoder state_decoder(
      display_state);
  row.display_id = state_decoder.id();

  row.base64_proto_id = context_->storage->mutable_string_pool()
                            ->InternString(base::StringView(base::Base64Encode(
                                display_state.data, display_state.size)))
                            .raw_id();

  if (state_decoder.has_what()) {
    auto what = state_decoder.what();
    auto flags_id = display_flag_ids_.find(what);
    if (flags_id != display_flag_ids_.end()) {
      row.flags_id = flags_id->second;
    } else {
      auto curr_size = static_cast<uint32_t>(layer_flag_ids_.size()) +
                       static_cast<uint32_t>(display_flag_ids_.size());
      row.flags_id = curr_size;
      display_flag_ids_[what] = curr_size;

      auto all_flags = std::vector<int32_t>{
          com::android::internal::pbzero::DisplayState::eSurfaceChanged,
          com::android::internal::pbzero::DisplayState::eLayerStackChanged,
          com::android::internal::pbzero::DisplayState::
              eDisplayProjectionChanged,
          com::android::internal::pbzero::DisplayState::eDisplaySizeChanged,
          com::android::internal::pbzero::DisplayState::eFlagsChanged,
      };
      auto tokens = DecodeFlags(what, all_flags);
      auto translated = std::vector<std::string>{};
      for (auto flag : tokens) {
        translated.push_back(
            com::android::internal::pbzero::DisplayState_Changes_Name(
                static_cast<
                    com::android::internal::pbzero::DisplayState_Changes>(
                    flag)));
      }
      AddFlags(translated, row.flags_id.value());
    }
  }

  auto row_id = context_->storage->mutable_surfaceflinger_transaction_table()
                    ->Insert(row)
                    .id;

  AddArgs(timestamp, display_state, row_id,
          ".com.android.internal.DisplayState", transaction);
}

void SurfaceFlingerTransactionsParser::AddArgs(
    int64_t timestamp,
    protozero::ConstBytes blob,
    tables::SurfaceFlingerTransactionTable::Id row_id,
    std::string message_type,
    std::optional<protozero::ConstBytes> transaction) {
  ArgsTracker tracker(context_);
  auto inserter = tracker.AddArgsTo(row_id);
  ArgsParser writer(timestamp, inserter, *context_->storage,
                    *context_->process_tracker);
  base::Status status = args_parser_.ParseMessage(
      blob, message_type, nullptr /* parse all fields */, writer);
  if (!status.ok()) {
    context_->stats_tracker->IncrementStats(
        stats::winscope_sf_transactions_parse_errors);
  }
  if (transaction.has_value()) {
    // add apply token and transaction barriers to same arg set
    std::vector<uint32_t> allowed_fields = {10, 11};
    args_parser_.ParseMessage(transaction.value(),
                              ".com.android.internal.TransactionState",
                              &allowed_fields, writer);
  }
}

std::vector<int32_t> SurfaceFlingerTransactionsParser::DecodeFlags(
    uint32_t bitset,
    std::vector<int32_t> all_flags) {
  std::vector<int32_t> set_flags;
  for (const auto& flag : all_flags) {
    if ((bitset & static_cast<uint32_t>(flag)) != 0) {
      set_flags.push_back(flag);
    }
  }
  return set_flags;
}

void SurfaceFlingerTransactionsParser::AddFlags(std::vector<std::string> flags,
                                                uint32_t flags_id) {
  for (const auto& flag : flags) {
    tables::SurfaceFlingerTransactionFlagTable::Row row;
    row.flags_id = flags_id;
    row.flag = context_->storage->mutable_string_pool()->InternString(
        base::StringView(flag));
    context_->storage->mutable_surfaceflinger_transaction_flag_table()->Insert(
        row);
  }
}

}  // namespace trace_processor
}  // namespace perfetto
