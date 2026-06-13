/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "src/protozero/filtering/message_filter_config.h"

#include <cstdint>
#include <optional>
#include <string>

#include "perfetto/base/status.h"
#include "protos/perfetto/config/trace_config.gen.h"
#include "src/protozero/filtering/message_filter.h"
#include "src/protozero/filtering/string_filter.h"

namespace protozero {

namespace {

using TraceFilter = perfetto::protos::gen::TraceConfig::TraceFilter;
using StringFilterRule = TraceFilter::StringFilterRule;

std::optional<StringFilter::Policy> ConvertPolicy(
    TraceFilter::StringFilterPolicy policy) {
  switch (policy) {
    case TraceFilter::SFP_UNSPECIFIED:
      return std::nullopt;
    case TraceFilter::SFP_MATCH_REDACT_GROUPS:
      return StringFilter::Policy::kMatchRedactGroups;
    case TraceFilter::SFP_ATRACE_MATCH_REDACT_GROUPS:
      return StringFilter::Policy::kAtraceMatchRedactGroups;
    case TraceFilter::SFP_MATCH_BREAK:
      return StringFilter::Policy::kMatchBreak;
    case TraceFilter::SFP_ATRACE_MATCH_BREAK:
      return StringFilter::Policy::kAtraceMatchBreak;
    case TraceFilter::SFP_ATRACE_REPEATED_SEARCH_REDACT_GROUPS:
      return StringFilter::Policy::kAtraceRepeatedSearchRedactGroups;
  }
  return std::nullopt;
}

std::optional<StringFilter::SemanticTypeMask> ConvertSemanticTypeMask(
    const StringFilterRule& rule) {
  // UNSPECIFIED (0) is treated as its own category - it only matches rules
  // that explicitly include bit 0 in their mask. If no semantic types are
  // specified, default to matching only UNSPECIFIED (bit 0).
  if (rule.semantic_type().empty()) {
    return StringFilter::SemanticTypeMask::Unspecified();
  }
  StringFilter::SemanticTypeMask mask;
  for (const auto& type : rule.semantic_type()) {
    auto semantic_type = static_cast<uint32_t>(type);
    if (semantic_type >= StringFilter::SemanticTypeMask::kLimit) {
      return std::nullopt;
    }
    mask.Set(semantic_type);
  }
  return mask;
}

}  // namespace

perfetto::base::Status LoadMessageFilterConfig(const TraceFilter& filt,
                                               MessageFilter* filter) {
  StringFilter& string_filter = filter->string_filter();

  auto add_rule = [&](const auto& rule) -> perfetto::base::Status {
    auto policy = ConvertPolicy(rule.policy());
    if (!policy.has_value()) {
      return perfetto::base::ErrStatus(
          "Trace filter has invalid string filtering rules");
    }
    auto semantic_type = ConvertSemanticTypeMask(rule);
    if (!semantic_type.has_value()) {
      return perfetto::base::ErrStatus(
          "Trace filter has invalid semantic types in string filtering rules");
    }
    string_filter.AddRule(*policy, rule.regex_pattern(),
                          rule.atrace_payload_starts_with(), rule.name(),
                          *semantic_type);
    return perfetto::base::OkStatus();
  };

  // Load base string filter chain.
  for (const auto& rule : filt.string_filter_chain().rules()) {
    auto status = add_rule(rule);
    if (!status.ok())
      return status;
  }

  // Load v54 string filter chain. Rules with matching names will replace
  // existing rules; others will be appended.
  for (const auto& rule : filt.string_filter_chain_v54().rules()) {
    auto status = add_rule(rule);
    if (!status.ok())
      return status;
  }

  const std::string& bytecode_v1 = filt.bytecode();
  const std::string& bytecode_v2 = filt.bytecode_v2();
  const std::string& bytecode = bytecode_v2.empty() ? bytecode_v1 : bytecode_v2;
  const std::string& overlay = filt.bytecode_overlay_v54();
  if (!filter->LoadFilterBytecode(bytecode.data(), bytecode.size(),
                                  overlay.empty() ? nullptr : overlay.data(),
                                  overlay.size())) {
    return perfetto::base::ErrStatus("Trace filter bytecode invalid");
  }

  return perfetto::base::OkStatus();
}

}  // namespace protozero
