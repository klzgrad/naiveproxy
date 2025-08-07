/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "src/trace_processor/importers/proto/metadata_minimal_module.h"

#include "perfetto/ext/base/base64.h"
#include "perfetto/ext/base/string_utils.h"
#include "src/trace_processor/importers/common/metadata_tracker.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/types/trace_processor_context.h"

#include "protos/perfetto/trace/chrome/chrome_benchmark_metadata.pbzero.h"
#include "protos/perfetto/trace/chrome/chrome_metadata.pbzero.h"

namespace perfetto {
namespace trace_processor {

using perfetto::protos::pbzero::TracePacket;

MetadataMinimalModule::MetadataMinimalModule(TraceProcessorContext* context)
    : context_(context) {
  RegisterForField(TracePacket::kChromeMetadataFieldNumber, context);
  RegisterForField(TracePacket::kChromeBenchmarkMetadataFieldNumber, context);
}

ModuleResult MetadataMinimalModule::TokenizePacket(
    const protos::pbzero::TracePacket::Decoder& decoder,
    TraceBlobView*,
    int64_t,
    RefPtr<PacketSequenceStateGeneration>,
    uint32_t field_id) {
  switch (field_id) {
    case TracePacket::kChromeMetadataFieldNumber: {
      ParseChromeMetadataPacket(decoder.chrome_metadata());
      return ModuleResult::Handled();
    }
    case TracePacket::kChromeBenchmarkMetadataFieldNumber: {
      ParseChromeBenchmarkMetadata(decoder.chrome_benchmark_metadata());
      return ModuleResult::Handled();
    }
  }
  return ModuleResult::Ignored();
}

void MetadataMinimalModule::ParseChromeBenchmarkMetadata(ConstBytes blob) {
  TraceStorage* storage = context_->storage.get();
  MetadataTracker* metadata = context_->metadata_tracker.get();

  protos::pbzero::ChromeBenchmarkMetadata::Decoder packet(blob.data, blob.size);
  if (packet.has_benchmark_name()) {
    auto benchmark_name_id = storage->InternString(packet.benchmark_name());
    metadata->SetMetadata(metadata::benchmark_name,
                          Variadic::String(benchmark_name_id));
  }
  if (packet.has_benchmark_description()) {
    auto benchmark_description_id =
        storage->InternString(packet.benchmark_description());
    metadata->SetMetadata(metadata::benchmark_description,
                          Variadic::String(benchmark_description_id));
  }
  if (packet.has_label()) {
    auto label_id = storage->InternString(packet.label());
    metadata->SetMetadata(metadata::benchmark_label,
                          Variadic::String(label_id));
  }
  if (packet.has_story_name()) {
    auto story_name_id = storage->InternString(packet.story_name());
    metadata->SetMetadata(metadata::benchmark_story_name,
                          Variadic::String(story_name_id));
  }
  for (auto it = packet.story_tags(); it; ++it) {
    auto story_tag_id = storage->InternString(*it);
    metadata->AppendMetadata(metadata::benchmark_story_tags,
                             Variadic::String(story_tag_id));
  }
  if (packet.has_benchmark_start_time_us()) {
    metadata->SetMetadata(metadata::benchmark_start_time_us,
                          Variadic::Integer(packet.benchmark_start_time_us()));
  }
  if (packet.has_story_run_time_us()) {
    metadata->SetMetadata(metadata::benchmark_story_run_time_us,
                          Variadic::Integer(packet.story_run_time_us()));
  }
  if (packet.has_story_run_index()) {
    metadata->SetMetadata(metadata::benchmark_story_run_index,
                          Variadic::Integer(packet.story_run_index()));
  }
  if (packet.has_had_failures()) {
    metadata->SetMetadata(metadata::benchmark_had_failures,
                          Variadic::Integer(packet.had_failures()));
  }
}

void MetadataMinimalModule::ParseChromeMetadataPacket(ConstBytes blob) {
  TraceStorage* storage = context_->storage.get();
  MetadataTracker* metadata = context_->metadata_tracker.get();

  // TODO(b/322298334): There is no easy way to associate ChromeMetadataPacket
  // with ChromeMetadata for the same instance, so we have opted for letters to
  // differentiate Chrome instances for ChromeMetadataPacket. When a unifying
  // Chrome instance ID is in place, update this code to use the same counter
  // as ChromeMetadata values.
  base::StackString<6> metadata_prefix(
      "cr-%c-", static_cast<char>('a' + (chrome_metadata_count_ % 26)));
  chrome_metadata_count_++;

  // Typed chrome metadata proto. The untyped metadata is parsed below in
  // ParseChromeEvents().
  protos::pbzero::ChromeMetadataPacket::Decoder packet_decoder(blob.data,
                                                               blob.size);

  if (packet_decoder.has_chrome_version_code()) {
    metadata->SetDynamicMetadata(
        storage->InternString(base::StringView(metadata_prefix.ToStdString() +
                                               "playstore_version_code")),
        Variadic::Integer(packet_decoder.chrome_version_code()));
  }
  if (packet_decoder.has_enabled_categories()) {
    auto categories_id =
        storage->InternString(packet_decoder.enabled_categories());
    metadata->SetDynamicMetadata(
        storage->InternString(base::StringView(metadata_prefix.ToStdString() +
                                               "enabled_categories")),
        Variadic::String(categories_id));
  }

  if (packet_decoder.has_field_trial_hashes()) {
    std::string field_trials;

    // Add  a line break after every 2 field trial hashes to better utilize the
    // UI space.
    int line_size = 0;
    for (auto it = packet_decoder.field_trial_hashes(); it; ++it) {
      if (line_size == 2) {
        field_trials.append("\n");
        line_size = 1;
      } else {
        line_size++;
      }

      perfetto::protos::pbzero::ChromeMetadataPacket::FinchHash::Decoder
          field_trial(*it);

      base::StackString<45> field_trial_string(
          "{ name: %u, group: %u } ", field_trial.name(), field_trial.group());

      field_trials.append(field_trial_string.ToStdString());
    }

    StringId field_trials_string =
        context_->storage->InternString(base::StringView(field_trials));
    metadata->SetDynamicMetadata(
        storage->InternString(base::StringView(metadata_prefix.ToStdString() +
                                               "field_trial_hashes")),
        Variadic::String(field_trials_string));
  }

  if (packet_decoder.has_background_tracing_metadata()) {
    auto background_tracing_metadata =
        packet_decoder.background_tracing_metadata();

    std::string base64 = base::Base64Encode(background_tracing_metadata.data,
                                            background_tracing_metadata.size);
    metadata->SetDynamicMetadata(
        storage->InternString("cr-background_tracing_metadata"),
        Variadic::String(storage->InternString(base::StringView(base64))));

    protos::pbzero::BackgroundTracingMetadata::Decoder metadata_decoder(
        background_tracing_metadata.data, background_tracing_metadata.size);
    if (metadata_decoder.has_scenario_name_hash()) {
      metadata->SetDynamicMetadata(
          storage->InternString("cr-scenario_name_hash"),
          Variadic::Integer(metadata_decoder.scenario_name_hash()));
    }
    auto triggered_rule = metadata_decoder.triggered_rule();
    if (!metadata_decoder.has_triggered_rule())
      return;
    protos::pbzero::BackgroundTracingMetadata::TriggerRule::Decoder
        triggered_rule_decoder(triggered_rule.data, triggered_rule.size);
  }
}

}  // namespace trace_processor
}  // namespace perfetto
