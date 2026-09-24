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

#include "perfetto/ext/profiling/smaps.h"

#include <fnmatch.h>
#include <stdio.h>
#include <cstdlib>
#include <cstring>

#include <deque>
#include <string>
#include <string_view>
#include <vector>

#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/protozero/packed_repeated_fields.h"
#include "protos/perfetto/trace/profiling/smaps.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {
namespace profiling {
namespace {

constexpr std::string_view kDeletedSuffix = " (deleted)";
constexpr std::string_view kDefaultReplacement = "<pf_redacted>";

class StringInterner {
 public:
  using StringId = size_t;

  StringInterner() {
    // index zero is always the empty string
    Intern(std::string_view{});
  }

  StringId Intern(std::string_view s) {
    if (auto* p = map_.Find(s); p) {
      return *p;
    }
    size_t index = storage_.size();
    storage_.emplace_back(s);
    std::string_view stable_sv(storage_.back());
    map_.Insert(stable_sv, index);
    return index;
  }

  std::deque<std::string> ConsumeStringsAndReset() {
    map_.Clear();
    auto ret = std::move(storage_);
    storage_.clear();
    Intern(std::string_view{});
    return ret;
  }

 private:
  base::FlatHashMap<std::string_view, StringId> map_;
  std::deque<std::string> storage_;
};

struct Vma {
  StringInterner::StringId name_id = 0;
  uint32_t aggregate_count = 1;

  uint64_t size_kb = 0;
  uint64_t rss_kb = 0;
  uint64_t anonymous_kb = 0;
  uint64_t swap_kb = 0;
  uint64_t shared_clean_kb = 0;
  uint64_t shared_dirty_kb = 0;
  uint64_t private_clean_kb = 0;
  uint64_t private_dirty_kb = 0;
  uint64_t locked_kb = 0;
  uint64_t pss_kb = 0;
  uint64_t pss_dirty_kb = 0;
  uint64_t swap_pss_kb = 0;
};

// clang-format off
enum SmapsField : uint32_t {
  kSize         = 1 << 0,
  kRss          = 1 << 1,
  kAnonymous    = 1 << 2,
  kSwap         = 1 << 3,
  kSharedClean  = 1 << 4,
  kSharedDirty  = 1 << 5,
  kPrivateClean = 1 << 6,
  kPrivateDirty = 1 << 7,
  kLocked       = 1 << 8,
  kPss          = 1 << 9,
  kPssDirty     = 1 << 10,
  kSwapPss      = 1 << 11,
};
// clang-format on

// Convenience mapping between config proto enums, implementation bitflags,
// field offsets, and trace proto field ids.
struct SmapsFieldDef {
  SmapsField flag;
  uint64_t Vma::* member_ptr;
  int32_t config_pb_enum;
  uint32_t trace_field_id;
};
using SC = protos::gen::SmapsConfig;
using SP = protos::pbzero::PackedSmaps;
// clang-format off
constexpr SmapsFieldDef kSmapsFieldDefs[] = {
  {kSize,         &Vma::size_kb,          SC::VMA_FIELD_SIZE,          SP::kSizeKbFieldNumber},
  {kRss,          &Vma::rss_kb,           SC::VMA_FIELD_RSS,           SP::kRssKbFieldNumber},
  {kAnonymous,    &Vma::anonymous_kb,     SC::VMA_FIELD_ANONYMOUS,     SP::kAnonymousKbFieldNumber},
  {kSwap,         &Vma::swap_kb,          SC::VMA_FIELD_SWAP,          SP::kSwapKbFieldNumber},
  {kSharedClean,  &Vma::shared_clean_kb,  SC::VMA_FIELD_SHARED_CLEAN,  SP::kSharedCleanKbFieldNumber},
  {kSharedDirty,  &Vma::shared_dirty_kb,  SC::VMA_FIELD_SHARED_DIRTY,  SP::kSharedDirtyKbFieldNumber},
  {kPrivateClean, &Vma::private_clean_kb, SC::VMA_FIELD_PRIVATE_CLEAN, SP::kPrivateCleanKbFieldNumber},
  {kPrivateDirty, &Vma::private_dirty_kb, SC::VMA_FIELD_PRIVATE_DIRTY, SP::kPrivateDirtyKbFieldNumber},
  {kLocked,       &Vma::locked_kb,        SC::VMA_FIELD_LOCKED,        SP::kLockedKbFieldNumber},
  {kPss,          &Vma::pss_kb,           SC::VMA_FIELD_PSS,           SP::kPssKbFieldNumber},
  {kPssDirty,     &Vma::pss_dirty_kb,     SC::VMA_FIELD_PSS_DIRTY,     SP::kPssDirtyKbFieldNumber},
  {kSwapPss,      &Vma::swap_pss_kb,      SC::VMA_FIELD_SWAP_PSS,      SP::kSwapPssKbFieldNumber},
};
// clang-format on

void AggregateVma(Vma& dest, const Vma& src) {
  dest.aggregate_count += src.aggregate_count;

  for (const auto& field : kSmapsFieldDefs) {
    dest.*(field.member_ptr) += src.*(field.member_ptr);
  }
}

std::string_view ExtractMappingName(std::string_view line) {
  // Skip until the last space-delimited column, which can itself contain spaces
  // so we can't tokenise from the end.
  size_t pos = 0;
  for (int i = 0; i < 5; ++i) {
    if (pos = line.find(' ', pos); pos == std::string_view::npos)
      return {};
    if (pos = line.find_first_not_of(' ', pos); pos == std::string_view::npos)
      return {};
  }

  size_t end = line.size() - 1;  // loop above guarantees size > 0
  if (pos >= end)
    return {};
  return line.substr(pos, end - pos);
}

void ParseSmapsLine(const char* line, Vma& vma, uint32_t& fields) {
  if (!line)
    return;

  // Note: strtoull skips leading spaces and the "kB" suffix. This is not
  // interchangeable with base::CStringToUInt64.
  switch (line[0]) {
    case 'S': {
      if ((fields & kSize) && strncmp(line, "Size:", 5) == 0) {
        vma.size_kb = std::strtoull(line + 5, nullptr, 10);
        fields &= ~kSize;
      } else if ((fields & kSwap) && strncmp(line, "Swap:", 5) == 0) {
        vma.swap_kb = std::strtoull(line + 5, nullptr, 10);
        fields &= ~kSwap;
      } else if ((fields & kSwapPss) && strncmp(line, "SwapPss:", 8) == 0) {
        vma.swap_pss_kb = std::strtoull(line + 8, nullptr, 10);
        fields &= ~kSwapPss;
      } else if ((fields & kSharedClean) &&
                 strncmp(line, "Shared_Clean:", 13) == 0) {
        vma.shared_clean_kb = std::strtoull(line + 13, nullptr, 10);
        fields &= ~kSharedClean;
      } else if ((fields & kSharedDirty) &&
                 strncmp(line, "Shared_Dirty:", 13) == 0) {
        vma.shared_dirty_kb = std::strtoull(line + 13, nullptr, 10);
        fields &= ~kSharedDirty;
      }
      break;
    }
    case 'R': {
      if ((fields & kRss) && strncmp(line, "Rss:", 4) == 0) {
        vma.rss_kb = std::strtoull(line + 4, nullptr, 10);
        fields &= ~kRss;
      }
      break;
    }
    case 'A': {
      if ((fields & kAnonymous) && strncmp(line, "Anonymous:", 10) == 0) {
        vma.anonymous_kb = std::strtoull(line + 10, nullptr, 10);
        fields &= ~kAnonymous;
      }
      break;
    }
    case 'P': {
      if ((fields & kPss) && strncmp(line, "Pss:", 4) == 0) {
        vma.pss_kb = std::strtoull(line + 4, nullptr, 10);
        fields &= ~kPss;
      } else if ((fields & kPssDirty) && strncmp(line, "Pss_Dirty:", 10) == 0) {
        vma.pss_dirty_kb = std::strtoull(line + 10, nullptr, 10);
        fields &= ~kPssDirty;
      } else if ((fields & kPrivateClean) &&
                 strncmp(line, "Private_Clean:", 14) == 0) {
        vma.private_clean_kb = std::strtoull(line + 14, nullptr, 10);
        fields &= ~kPrivateClean;
      } else if ((fields & kPrivateDirty) &&
                 strncmp(line, "Private_Dirty:", 14) == 0) {
        vma.private_dirty_kb = std::strtoull(line + 14, nullptr, 10);
        fields &= ~kPrivateDirty;
      }
      break;
    }
    case 'L': {
      if ((fields & kLocked) && strncmp(line, "Locked:", 7) == 0) {
        vma.locked_kb = std::strtoull(line + 7, nullptr, 10);
        fields &= ~kLocked;
      }
      break;
    }
    default:
      break;
  }
}

template <typename FN>
void Parse(FILE* file,
           StringInterner& interner,
           uint32_t requested_fields,
           FN callback) {
  Vma vma = Vma{};
  bool in_vma = false;
  // bitmask of the fields that still need to be parsed for the current vma
  uint32_t fields_to_parse = 0;

  // getline (re)allocates the buffer, so free it when done
  char* buf = nullptr;
  auto getline_cleanup = base::OnScopeExit([&] { free(buf); });

  size_t buf_len = 0;
  ssize_t read_len = 0;
  while ((read_len = getline(&buf, &buf_len, file)) != -1) {
    std::string_view line(buf, static_cast<size_t>(read_len));
    if (line.empty())
      continue;

    // Test if we're at a new vma boundary by checking that this isn't a
    // colon-delimited line. Example of the two types of line:
    // 7f13720e6000-7f13720e8000 r-xp 00000000 00:00 0            [vdso]
    // Size:                  8 kB
    // Rss:                   8 kB
    size_t space_pos = line.find(' ');
    size_t colon_pos = line.find(':');
    if (colon_pos == std::string_view::npos || space_pos < colon_pos) {
      if (in_vma) {
        callback(vma);
      }

      vma = Vma{};
      vma.name_id = interner.Intern(ExtractMappingName(line));
      in_vma = true;
      fields_to_parse = requested_fields;

    } else if (in_vma) {
      if (!fields_to_parse) {
        continue;  // done, skip until the next vma
      }
      ParseSmapsLine(buf, vma, fields_to_parse);
    }
  }

  if (in_vma) {
    callback(vma);
  }
}

bool MatchRedactionPattern(const char* name,
                           const protos::gen::RedactionRule& rule) {
  using RR = protos::gen::RedactionRule;
  const char* pattern = rule.pattern().c_str();
  if (rule.match_mode() == RR::MATCH_MODE_PREFIX) {
    return !strncmp(name, pattern, rule.pattern().length());
  }
  if (rule.match_mode() == RR::MATCH_MODE_GLOB_PATH) {
    return !fnmatch(pattern, name, FNM_NOESCAPE | FNM_PATHNAME);
  }
  if (rule.match_mode() == RR::MATCH_MODE_GLOB_STRING) {
    return !fnmatch(pattern, name, FNM_NOESCAPE);
  }
  // unknown enum: default to matching against any pattern.
  return true;
}

const std::string& MaybeRedactName(
    std::string& name,
    const std::vector<protos::gen::RedactionRule>& rules,
    std::string& extra_storage) {
  if (name.empty())
    return name;

  // Trim any "(deleted)" suffix that the kernel appends
  // for file-backed mappings where the file has been deleted.
  // Do the edit in-place as we're minimising allocations. We'll restore the
  // original string after matching.
  size_t deleted_suffix_pos = std::string_view::npos;
  if (name.size() >= kDeletedSuffix.size() &&
      !name.compare(name.size() - kDeletedSuffix.size(), kDeletedSuffix.size(),
                    kDeletedSuffix)) {
    deleted_suffix_pos = name.size() - kDeletedSuffix.size();
    name[deleted_suffix_pos] = '\0';
  }

  const protos::gen::RedactionRule* rule = nullptr;
  for (const auto& candidate_rule : rules) {
    if (MatchRedactionPattern(name.c_str(), candidate_rule)) {
      rule = &candidate_rule;
      break;  // first matching rule wins
    }
  }

  // Restore original string.
  if (deleted_suffix_pos != std::string_view::npos) {
    name[deleted_suffix_pos] = ' ';
  }

  if (!rule || rule->keep_full()) {
    // No match or explicit allow -> keep original string.
    return name;
  }

  // At this point, we know that we need to redact at least parts of the string.
  // Find the prefix and suffix of the original string to keep, and then replace
  // the rest.
  std::string_view replacement = rule->has_replacement_name()
                                     ? rule->replacement_name()
                                     : kDefaultReplacement;

  // We matched against the pattern but this looks like an anonymous or special
  // mapping. None of the path-based rules apply, so replace the whole name.
  if (name[0] != '/') {
    name = replacement;
    return name;
  }

  // Prefix: keep up to N path elements:
  // keep = 1 for /x/y/z -> /x/
  // keep = 2 for /x/y/z -> /x/y/
  // keep = 3 for /x/y/z -> /x/y/z
  // keep = 4 for /x/y/z -> /x/y/z
  std::string_view keep_prefix;
  if (rule->keep_path_elements() > 0) {
    size_t pos = 0;
    size_t max_elems = rule->keep_path_elements();
    for (size_t i = 0; i < max_elems && pos != std::string_view::npos; ++i) {
      pos = name.find('/', pos + 1);
    }
    size_t prefix_len = (pos != std::string_view::npos) ? pos + 1 : name.size();
    keep_prefix = std::string_view(name.data(), prefix_len);
  }

  // Suffix: keep any (deleted) and optionally retain the file extension:
  // /x/y/z.so -> .so
  // /x/y/z.tar (deleted) -> .tar (deleted)
  // /x/y.y/z -> ""
  // /x/y/.z -> ""
  size_t keep_suffix_pos = deleted_suffix_pos;
  if (rule->keep_file_extension()) {
    size_t last_dot = name.rfind('.');
    size_t last_slash = name.rfind('/');
    if ((last_dot != std::string_view::npos &&
         last_slash != std::string_view::npos) &&
        last_dot > last_slash + 1) {
      keep_suffix_pos = last_dot;
    }
  }
  std::string_view keep_suffix;
  if (keep_suffix_pos != std::string_view::npos) {
    keep_suffix = std::string_view(name).substr(keep_suffix_pos);
  }

  // Now assemble the redacted name, trying to stay within the original string
  // to minimise string copies.

  // Keep rules cover the entire name, so no redaction.
  if ((keep_prefix.length() + keep_suffix.length()) >= name.length()) {
    return name;
  }

  // If the combined pattern fits, rewrite and return the original string.
  size_t new_len =
      keep_prefix.length() + replacement.length() + keep_suffix.length();
  if (new_len <= name.capacity()) {
    size_t cut_pos = keep_prefix.length();
    size_t cut_len =
        name.length() - keep_prefix.length() - keep_suffix.length();

    name.replace(cut_pos, cut_len, replacement);
    return name;
  }

  // Unlikely: redacted name is larger than the original, build it in the
  // pre-allocated string.
  extra_storage.clear();
  if (extra_storage.capacity() < new_len) {
    extra_storage.reserve(new_len);
  }
  extra_storage.append(keep_prefix);
  extra_storage.append(replacement);
  extra_storage.append(keep_suffix);
  return extra_storage;
}

void SerializeStringTable(
    protos::pbzero::PackedSmaps* packed_smaps,
    std::deque<std::string>& strings,
    const std::vector<protos::gen::RedactionRule>& rules) {
  if (rules.empty()) {
    for (auto& v : strings) {
      packed_smaps->add_string_table(v);
    }
    return;
  }

  std::string reusable_string;
  for (auto& v : strings) {
    const auto& redacted = MaybeRedactName(v, rules, reusable_string);
    packed_smaps->add_string_table(redacted);
  }
}

}  // namespace

void ParseAndSerializeSmaps(FILE* file,
                            const protos::gen::SmapsConfig& config,
                            protos::pbzero::SmapsPacket* packet) {
  if (!file || !packet)
    return;

  // Config -> bitmask of fields to collect.
  uint32_t parser_mask = kSize | kRss | kAnonymous | kSwap;
  if (config.vma_fields_size()) {
    parser_mask = 0;
    for (int32_t pb_enum : config.vma_fields()) {
      for (const auto& def : kSmapsFieldDefs) {
        if (def.config_pb_enum == pb_enum) {
          parser_mask |= def.flag;
        }
      }
    }
  }
  bool aggregated = !config.unaggregated();

  // Parse the file:

  StringInterner interner;
  std::vector<Vma> vmas;
  // If we're aggregating by name, use the vector as a map with the interned
  // name as the index (since the StringInterner assigns ids in a sequential
  // order).
  // So since the interner always assigns the empty string the id 0,
  // pre-create that vector entry.
  if (aggregated) {
    vmas.push_back(Vma{});
    vmas[0].aggregate_count = 0;
  }

  Parse(file, interner, parser_mask, [&vmas, aggregated](Vma vma) {
    if (!aggregated) {
      vmas.push_back(vma);
      return;
    }
    // aggregated: index into vector with interned id.
    size_t name_id = vma.name_id;
    if (name_id < vmas.size()) {
      AggregateVma(vmas[name_id], vma);
    } else {
      vmas.resize(name_id + 1);
      vmas[name_id] = vma;
    }
  });

  // Serialise the proto:

  auto packed_smaps = packet->set_packed_entries();
  auto string_table = interner.ConsumeStringsAndReset();
  SerializeStringTable(packed_smaps, string_table,
                       config.name_redaction_rules());

  protozero::PackedVarInt packed;
  // If aggregating: write aggregate_count, but skip name_id as a size
  // optimisation. We write the aggregated vmas exactly in string_table order.
  if (aggregated) {
    packed.Reset();
    for (const auto& vma : vmas) {
      packed.Append(vma.aggregate_count);
    }
    packed_smaps->set_aggregate_count(packed);
  } else {
    // Unaggregated: write name_id.
    for (const auto& vma : vmas) {
      packed.Append(static_cast<uint32_t>(vma.name_id));
    }
    packed_smaps->set_name_id(packed);
  }

  // write value fields
  for (const auto& field : kSmapsFieldDefs) {
    if (parser_mask & field.flag) {
      packed.Reset();
      for (const auto& vma : vmas) {
        packed.Append(vma.*(field.member_ptr));
      }
      packed_smaps->AppendBytes(field.trace_field_id, packed.data(),
                                packed.size());
    }
  }
}

}  // namespace profiling
}  // namespace perfetto
