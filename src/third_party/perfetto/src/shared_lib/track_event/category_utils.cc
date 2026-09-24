/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "src/shared_lib/track_event/category_utils.h"
#include <functional>
#include "perfetto/ext/base/string_view.h"

namespace perfetto::shlib {

namespace {

enum class MatchType { kExact, kPattern };

bool NameMatchesPattern(const std::string& pattern,
                        const perfetto::base::StringView& name,
                        MatchType match_type) {
  // To avoid pulling in all of std::regex, for now we only support a single "*"
  // wildcard at the end of the pattern.
  size_t i = pattern.find('*');
  if (i != std::string::npos) {
    if (match_type != MatchType::kPattern)
      return false;
    return name.substr(0, i) ==
           perfetto::base::StringView(pattern).substr(0, i);
  }
  return name == perfetto::base::StringView(pattern);
}

bool NameMatchesPatternList(const std::vector<std::string>& patterns,
                            const perfetto::base::StringView& name,
                            MatchType match_type) {
  for (const auto& pattern : patterns) {
    if (NameMatchesPattern(pattern, name, match_type))
      return true;
  }
  return false;
}

}  // namespace

bool IsSingleCategoryEnabled(
    const PerfettoTeCategoryDescriptor& c,
    const perfetto::protos::gen::TrackEventConfig& config) {
  auto has_matching_tag = [&](std::function<bool(const char*)> matcher) {
    for (size_t i = 0; i < c.num_tags; ++i) {
      if (matcher(c.tags[i]))
        return true;
    }
    return false;
  };
  // First try exact matches, then pattern matches.
  const std::array<MatchType, 2> match_types = {
      {MatchType::kExact, MatchType::kPattern}};
  for (auto match_type : match_types) {
    // 1. Enabled categories.
    if (NameMatchesPatternList(config.enabled_categories(), c.name,
                               match_type)) {
      return true;
    }

    // 2. Enabled tags.
    if (has_matching_tag([&](const char* tag) {
          return NameMatchesPatternList(config.enabled_tags(), tag, match_type);
        })) {
      return true;
    }

    // 3. Disabled categories.
    if (NameMatchesPatternList(config.disabled_categories(), c.name,
                               match_type)) {
      return false;
    }

    // 4. Disabled tags.
    if (has_matching_tag([&](const char* tag) {
          return NameMatchesPatternList(config.disabled_tags(), tag,
                                        match_type);
        })) {
      return false;
    }
  }

  // If nothing matched, the category is disabled by default. N.B. this behavior
  // is different than the C++ TrackEvent API.
  return false;
}

void SerializeCategory(const PerfettoTeCategoryDescriptor& desc,
                       perfetto::protos::pbzero::TrackEventDescriptor* ted) {
  auto* c = ted->add_available_categories();
  c->set_name(desc.name);
  if (desc.desc)
    c->set_description(desc.desc);
  for (size_t j = 0; j < desc.num_tags; ++j) {
    c->add_tags(desc.tags[j]);
  }
}

}  // namespace perfetto::shlib
