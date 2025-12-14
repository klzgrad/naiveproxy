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

#include "src/trace_processor/importers/common/deobfuscation_mapping_table.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"

namespace perfetto {
namespace trace_processor {

bool DeobfuscationMappingTable::AddClassTranslation(
    const PackageId& package,
    StringId obfuscated_class_name,
    StringId deobfuscated_class_name,
    base::FlatHashMap<StringId, StringId> obfuscated_to_deobfuscated_members) {
  if (PERFETTO_UNLIKELY(!default_package_id_)) {
    default_package_id_ = package;
  }

  ObfuscatedClassesToMembers& classes_to_members =
      class_per_package_translation_[package];
  return classes_to_members
      .Insert(obfuscated_class_name,
              ClassTranslation{deobfuscated_class_name,
                               std::move(obfuscated_to_deobfuscated_members)})
      .second;
}

std::optional<StringId> DeobfuscationMappingTable::TranslateClass(
    StringId obfuscated_class_name) const {
  if (PERFETTO_UNLIKELY(!default_package_id_.has_value())) {
    return std::nullopt;
  }
  return TranslateClass(default_package_id_.value(), obfuscated_class_name);
}

std::optional<StringId> DeobfuscationMappingTable::TranslateClass(
    const PackageId& package,
    StringId obfuscated_class_name) const {
  const ObfuscatedClassesToMembers* classes_translation_ptr =
      class_per_package_translation_.Find(package);
  if (classes_translation_ptr == nullptr) {
    return std::nullopt;
  }
  const ClassTranslation* class_translation_ptr =
      classes_translation_ptr->Find(obfuscated_class_name);
  if (class_translation_ptr == nullptr) {
    return std::nullopt;
  }
  return class_translation_ptr->deobfuscated_class_name;
}

std::optional<StringId> DeobfuscationMappingTable::TranslateMember(
    const PackageId& package,
    StringId obfuscated_class_name,
    StringId obfuscated_member) const {
  const ObfuscatedClassesToMembers* classes_translation_ptr =
      class_per_package_translation_.Find(package);
  if (classes_translation_ptr == nullptr) {
    return std::nullopt;
  }

  const ClassTranslation* class_translation_ptr =
      classes_translation_ptr->Find(obfuscated_class_name);
  if (class_translation_ptr == nullptr) {
    return std::nullopt;
  }

  const StringId* member_translation_ptr =
      class_translation_ptr->members.Find(obfuscated_member);
  if (member_translation_ptr == nullptr) {
    return std::nullopt;
  }
  return *member_translation_ptr;
}

}  // namespace trace_processor
}  // namespace perfetto
