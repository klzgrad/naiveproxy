/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include <optional>

#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/importers/common/args_translation_table.h"

namespace perfetto {
namespace trace_processor {

namespace {

// The raw symbol name is namespace::Interface::Method_Sym::IPCStableHash.
// We want to return namespace::Interface::Method.
std::string ExtractMojoMethod(const std::string& method_symbol) {
  // The symbol ends with "()" for some platforms, but not for all of them.
  std::string without_sym_suffix = base::StripSuffix(method_symbol, "()");
  // This suffix is platform-independent, it's coming from Chromium code.
  // https://source.chromium.org/chromium/chromium/src/+/main:mojo/public/tools/bindings/generators/cpp_templates/interface_declaration.tmpl;l=66;drc=9d9e6f5ce548ecf228aed711f55b11c7ea8bdb55
  constexpr char kSymSuffix[] = "_Sym::IPCStableHash";
  return base::StripSuffix(without_sym_suffix, kSymSuffix);
}

// The raw symbol name is namespace::Interface::Method_Sym::IPCStableHash.
// We want to return namespace.Interface (for historical compatibility).
std::string ExtractMojoInterfaceTag(const std::string& method_symbol) {
  auto parts = base::SplitString(method_symbol, "::");
  // If we have too few parts, return the original string as is to simplify
  // debugging.
  if (parts.size() <= 2) {
    return method_symbol;
  }
  // Remove Method_Sym and IPCStableHash parts.
  parts.erase(parts.end() - 2, parts.end());
  return base::Join(parts, ".");
}

}  // namespace

ArgsTranslationTable::ArgsTranslationTable(TraceStorage* storage)
    : storage_(storage),
      interned_chrome_histogram_hash_key_(
          storage->InternString(kChromeHistogramHashKey)),
      interned_chrome_histogram_name_key_(
          storage->InternString(kChromeHistogramNameKey)),
      interned_chrome_user_event_hash_key_(
          storage->InternString(kChromeUserEventHashKey)),
      interned_chrome_user_event_action_key_(
          storage->InternString(kChromeUserEventActionKey)),
      interned_chrome_performance_mark_site_hash_key_(
          storage->InternString(kChromePerformanceMarkSiteHashKey)),
      interned_chrome_performance_mark_site_key_(
          storage->InternString(kChromePerformanceMarkSiteKey)),
      interned_chrome_performance_mark_mark_hash_key_(
          storage->InternString(kChromePerformanceMarkMarkHashKey)),
      interned_chrome_performance_mark_mark_key_(
          storage->InternString(kChromePerformanceMarkMarkKey)),
      interned_chrome_trigger_hash_key_(
          storage->InternString(kChromeTriggerHashKey)),
      interned_chrome_trigger_name_key_(
          storage->InternString(kChromeTriggerNameKey)),
      interned_mojo_method_mapping_id_(
          storage->InternString(kMojoMethodMappingIdKey)),
      interned_mojo_method_rel_pc_(storage->InternString(kMojoMethodRelPcKey)),
      interned_mojo_method_name_(storage->InternString(kMojoMethodNameKey)),
      interned_mojo_interface_tag_(storage->InternString(kMojoIntefaceTagKey)),
      interned_obfuscated_view_dump_class_name_flat_key_(
          storage->InternString(kObfuscatedViewDumpClassNameFlatKey)) {}

bool ArgsTranslationTable::NeedsTranslation(StringId flat_key_id,
                                            StringId key_id,
                                            Variadic::Type type) const {
  return KeyIdAndTypeToEnum(flat_key_id, key_id, type).has_value();
}

void ArgsTranslationTable::TranslateArgs(
    const ArgsTracker::CompactArgSet& arg_set,
    ArgsTracker::BoundInserter& inserter) const {
  std::optional<uint64_t> mapping_id;
  std::optional<uint64_t> rel_pc;

  for (const auto& arg : arg_set) {
    const auto key_type =
        KeyIdAndTypeToEnum(arg.flat_key, arg.key, arg.value.type);
    if (!key_type.has_value()) {
      inserter.AddArg(arg.flat_key, arg.key, arg.value, arg.update_policy);
      continue;
    }

    switch (*key_type) {
      case KeyType::kChromeHistogramHash: {
        inserter.AddArg(interned_chrome_histogram_hash_key_, arg.value);
        const std::optional<base::StringView> translated_value =
            TranslateChromeHistogramHash(arg.value.uint_value);
        if (translated_value) {
          inserter.AddArg(
              interned_chrome_histogram_name_key_,
              Variadic::String(storage_->InternString(*translated_value)));
        }
        break;
      }
      case KeyType::kChromeUserEventHash: {
        inserter.AddArg(interned_chrome_user_event_hash_key_, arg.value);
        const std::optional<base::StringView> translated_value =
            TranslateChromeUserEventHash(arg.value.uint_value);
        if (translated_value) {
          inserter.AddArg(
              interned_chrome_user_event_action_key_,
              Variadic::String(storage_->InternString(*translated_value)));
        }
        break;
      }
      case KeyType::kChromePerformanceMarkMarkHash: {
        inserter.AddArg(interned_chrome_performance_mark_mark_hash_key_,
                        arg.value);
        const std::optional<base::StringView> translated_value =
            TranslateChromePerformanceMarkMarkHash(arg.value.uint_value);
        if (translated_value) {
          inserter.AddArg(
              interned_chrome_performance_mark_mark_key_,
              Variadic::String(storage_->InternString(*translated_value)));
        }
        break;
      }
      case KeyType::kChromePerformanceMarkSiteHash: {
        inserter.AddArg(interned_chrome_performance_mark_site_hash_key_,
                        arg.value);
        const std::optional<base::StringView> translated_value =
            TranslateChromePerformanceMarkSiteHash(arg.value.uint_value);
        if (translated_value) {
          inserter.AddArg(
              interned_chrome_performance_mark_site_key_,
              Variadic::String(storage_->InternString(*translated_value)));
        }
        break;
      }
      case KeyType::kClassName: {
        const std::optional<StringId> translated_class_name =
            TranslateClassName(arg.value.string_value);
        if (translated_class_name) {
          inserter.AddArg(arg.flat_key, arg.key,
                          Variadic::String(*translated_class_name));
        } else {
          inserter.AddArg(arg.flat_key, arg.key, arg.value);
        }
        break;
      }
      case KeyType::kChromeTriggerHash: {
        inserter.AddArg(interned_chrome_trigger_hash_key_, arg.value);
        const std::optional<base::StringView> translated_value =
            TranslateChromeStudyHash(arg.value.uint_value);
        if (translated_value) {
          inserter.AddArg(
              interned_chrome_trigger_name_key_,
              Variadic::String(storage_->InternString(*translated_value)));
        }
        break;
      }
      case KeyType::kMojoMethodMappingId: {
        mapping_id = arg.value.uint_value;
        break;
      }
      case KeyType::kMojoMethodRelPc: {
        rel_pc = arg.value.uint_value;
        break;
      }
    }
  }
  EmitMojoMethodLocation(mapping_id, rel_pc, inserter);
}

std::optional<ArgsTranslationTable::KeyType>
ArgsTranslationTable::KeyIdAndTypeToEnum(StringId flat_key_id,
                                         StringId key_id,
                                         Variadic::Type type) const {
  if (type == Variadic::Type::kUint) {
    if (key_id == interned_chrome_histogram_hash_key_) {
      return KeyType::kChromeHistogramHash;
    }
    if (key_id == interned_chrome_user_event_hash_key_) {
      return KeyType::kChromeUserEventHash;
    }
    if (key_id == interned_chrome_performance_mark_mark_hash_key_) {
      return KeyType::kChromePerformanceMarkMarkHash;
    }
    if (key_id == interned_chrome_performance_mark_site_hash_key_) {
      return KeyType::kChromePerformanceMarkSiteHash;
    }
    if (key_id == interned_mojo_method_mapping_id_) {
      return KeyType::kMojoMethodMappingId;
    }
    if (key_id == interned_mojo_method_rel_pc_) {
      return KeyType::kMojoMethodRelPc;
    }
    if (key_id == interned_chrome_trigger_hash_key_) {
      return KeyType::kChromeTriggerHash;
    }
  } else if (type == Variadic::Type::kString) {
    if (flat_key_id == interned_obfuscated_view_dump_class_name_flat_key_) {
      return KeyType::kClassName;
    }
  }
  return std::nullopt;
}

std::optional<base::StringView>
ArgsTranslationTable::TranslateChromeHistogramHash(uint64_t hash) const {
  auto* value = chrome_histogram_hash_to_name_.Find(hash);
  if (!value) {
    return std::nullopt;
  }
  return base::StringView(*value);
}

std::optional<base::StringView>
ArgsTranslationTable::TranslateChromeUserEventHash(uint64_t hash) const {
  auto* value = chrome_user_event_hash_to_action_.Find(hash);
  if (!value) {
    return std::nullopt;
  }
  return base::StringView(*value);
}

std::optional<base::StringView>
ArgsTranslationTable::TranslateChromePerformanceMarkSiteHash(
    uint64_t hash) const {
  auto* value = chrome_performance_mark_site_hash_to_name_.Find(hash);
  if (!value) {
    return std::nullopt;
  }
  return base::StringView(*value);
}

std::optional<base::StringView>
ArgsTranslationTable::TranslateChromePerformanceMarkMarkHash(
    uint64_t hash) const {
  auto* value = chrome_performance_mark_mark_hash_to_name_.Find(hash);
  if (!value) {
    return std::nullopt;
  }
  return base::StringView(*value);
}

std::optional<base::StringView> ArgsTranslationTable::TranslateChromeStudyHash(
    uint64_t hash) const {
  auto* value = chrome_study_hash_to_name_.Find(hash);
  if (!value) {
    return std::nullopt;
  }
  return base::StringView(*value);
}

std::optional<ArgsTranslationTable::SourceLocation>
ArgsTranslationTable::TranslateNativeSymbol(MappingId mapping_id,
                                            uint64_t rel_pc) const {
  auto loc =
      native_symbol_to_location_.Find(std::make_pair(mapping_id, rel_pc));
  if (!loc) {
    return std::nullopt;
  }
  return *loc;
}

std::optional<StringId> ArgsTranslationTable::TranslateClassName(
    StringId obfuscated_class_name_id) const {
  return deobfuscation_mapping_table_.TranslateClass(obfuscated_class_name_id);
}

void ArgsTranslationTable::EmitMojoMethodLocation(
    std::optional<uint64_t> mapping_id,
    std::optional<uint64_t> rel_pc,
    ArgsTracker::BoundInserter& inserter) const {
  if (!mapping_id || !rel_pc) {
    return;
  }
  const MappingId row_id(static_cast<uint32_t>(*mapping_id));
  const auto loc = TranslateNativeSymbol(row_id, *rel_pc);
  if (loc) {
    inserter.AddArg(interned_mojo_method_name_,
                    Variadic::String(storage_->InternString(base::StringView(
                        ExtractMojoMethod((loc->function_name))))));
    inserter.AddArg(interned_mojo_interface_tag_,
                    Variadic::String(storage_->InternString(base::StringView(
                        ExtractMojoInterfaceTag(loc->function_name)))),
                    // If the trace already has interface tag as a raw string
                    // (older Chromium versions, local traces, and so on), use
                    // the raw string.
                    GlobalArgsTracker::UpdatePolicy::kSkipIfExists);
  } else {
    // Could not find the corresponding source location. Let's emit raw arg
    // values so that the data doesn't silently go missing.
    inserter.AddArg(interned_mojo_method_mapping_id_,
                    Variadic::UnsignedInteger(*mapping_id));
    inserter.AddArg(interned_mojo_method_rel_pc_,
                    Variadic::UnsignedInteger(*rel_pc));
  }
}

}  // namespace trace_processor
}  // namespace perfetto
