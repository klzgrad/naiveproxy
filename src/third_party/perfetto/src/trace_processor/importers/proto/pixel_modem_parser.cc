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

#include "src/trace_processor/importers/proto/pixel_modem_parser.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/protozero/field.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/proto/pigweed_detokenizer.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {

namespace {

constexpr std::string_view kKeyDelimiterStart = "\u25A0";
constexpr std::string_view kKeyDelimiterEnd = "\u2666";
constexpr std::string_view kKeyDomain = "domain";
constexpr std::string_view kKeyFormat = "format";
constexpr std::string_view kModemNamePrefix = "Pixel Modem Events: ";
constexpr std::string_view kModemName = "Pixel Modem Events";

// Modem inputs in particular have this key-value encoding. It's not a Pigweed
// thing.
base::FlatHashMap<std::string, std::string> SplitUpModemString(
    const std::string& input) {
  auto delimStart = std::string(kKeyDelimiterStart);
  auto delimEnd = std::string(kKeyDelimiterEnd);

  base::FlatHashMap<std::string, std::string> result;
  std::vector<std::string> pairs = base::SplitString(input, delimStart);
  for (auto& it : pairs) {
    std::vector<std::string> pair = base::SplitString(it, delimEnd);
    if (pair.size() >= 2) {
      result.Insert(pair[0], pair[1]);
    }
  }
  return result;
}

}  // namespace

PixelModemParser::PixelModemParser(TraceProcessorContext* context)
    : context_(context),
      detokenizer_(pigweed::CreateNullDetokenizer()),
      template_id_(context->storage->InternString("raw_template")),
      token_id_(context->storage->InternString("token_id")),
      token_id_hex_(context->storage->InternString("token_id_hex")),
      packet_timestamp_id_(context->storage->InternString("packet_ts")) {}

PixelModemParser::~PixelModemParser() = default;

base::Status PixelModemParser::SetDatabase(protozero::ConstBytes blob) {
  ASSIGN_OR_RETURN(detokenizer_, pigweed::CreateDetokenizer(blob));
  return base::OkStatus();
}

base::Status PixelModemParser::ParseEvent(int64_t ts,
                                          uint64_t trace_packet_ts,
                                          protozero::ConstBytes blob) {
  ASSIGN_OR_RETURN(pigweed::DetokenizedString detokenized_str,
                   detokenizer_.Detokenize(blob));

  std::string event = detokenized_str.Format();

  base::FlatHashMap<std::string, std::string> map = SplitUpModemString(event);
  std::string* domain = map.Find(std::string(kKeyDomain));
  std::string* format = map.Find(std::string(kKeyFormat));

  static constexpr auto kBlueprint = tracks::SliceBlueprint(
      "pixel_modem_event",
      tracks::DimensionBlueprints(
          tracks::StringDimensionBlueprint("modem_domain")),
      tracks::FnNameBlueprint([](base::StringView domain) {
        if (domain.empty()) {
          return base::StackString<1024>("%.*s", int(kModemName.size()),
                                         kModemName.data());
        }
        return base::StackString<1024>("%.*s%.*s", int(kModemNamePrefix.size()),
                                       kModemNamePrefix.data(),
                                       int(domain.size()), domain.data());
      }));

  const std::string& slice_name = format ? *format : event;

  StringId slice_name_id = context_->storage->InternString(slice_name.c_str());
  TrackId id = context_->track_tracker->InternTrack(
      kBlueprint, tracks::Dimensions(domain ? base::StringView(*domain)
                                            : base::StringView()));
  context_->slice_tracker->Scoped(
      ts, id, kNullStringId, slice_name_id, 0,
      [this, &detokenized_str,
       trace_packet_ts](ArgsTracker::BoundInserter* inserter) {
        inserter->AddArg(template_id_,
                         Variadic::String(context_->storage->InternString(
                             detokenized_str.template_str().c_str())));
        uint32_t token = detokenized_str.token();
        inserter->AddArg(token_id_, Variadic::Integer(token));
        inserter->AddArg(token_id_hex_,
                         Variadic::String(context_->storage->InternString(
                             base::IntToHexString(token).c_str())));
        inserter->AddArg(packet_timestamp_id_,
                         Variadic::UnsignedInteger(trace_packet_ts));
        auto pw_args = detokenized_str.args();
        for (size_t i = 0; i < pw_args.size(); i++) {
          StringId arg_name = context_->storage->InternString(
              ("pw_token_" + std::to_string(token) + ".arg_" +
               std::to_string(i))
                  .c_str());
          auto arg = pw_args[i];
          if (auto* int_arg = std::get_if<int64_t>(&arg)) {
            inserter->AddArg(arg_name, Variadic::Integer(*int_arg));
          } else if (auto* uint_arg = std::get_if<uint64_t>(&arg)) {
            inserter->AddArg(arg_name, Variadic::UnsignedInteger(*uint_arg));
          } else {
            inserter->AddArg(arg_name, Variadic::Real(std::get<double>(arg)));
          }
        }
      });
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor
