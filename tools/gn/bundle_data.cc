// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/bundle_data.h"

#include "base/logging.h"
#include "tools/gn/filesystem_utils.h"
#include "tools/gn/label_pattern.h"
#include "tools/gn/output_file.h"
#include "tools/gn/settings.h"
#include "tools/gn/substitution_writer.h"
#include "tools/gn/target.h"

namespace {

// Return directory of |path| without the trailing directory separator.
base::StringPiece FindDirNoTrailingSeparator(base::StringPiece path) {
  base::StringPiece::size_type pos = path.find_last_of("/\\");
  if (pos == base::StringPiece::npos)
    return base::StringPiece();
  return base::StringPiece(path.data(), pos);
}

bool IsSourceFileFromAssetsCatalog(base::StringPiece source,
                                   SourceFile* asset_catalog) {
  // Check whether |source| matches one of the following pattern:
  //    .*\.xcassets/Contents.json
  //    .*\.xcassets/[^/]*\.appiconset/[^/]*
  //    .*\.xcassets/[^/]*\.imageset/[^/]*
  //    .*\.xcassets/[^/]*\.launchimage/[^/]*
  bool is_file_from_asset_catalog = false;
  base::StringPiece dir = FindDirNoTrailingSeparator(source);
  if (source.ends_with("/Contents.json") && dir.ends_with(".xcassets")) {
    is_file_from_asset_catalog = true;
  } else if (dir.ends_with(".appiconset") || dir.ends_with(".imageset") ||
             dir.ends_with(".launchimage")) {
    dir = FindDirNoTrailingSeparator(dir);
    is_file_from_asset_catalog = dir.ends_with(".xcassets");
  }
  if (is_file_from_asset_catalog && asset_catalog) {
    std::string asset_catalog_path = dir.as_string();
    *asset_catalog = SourceFile(SourceFile::SWAP_IN, &asset_catalog_path);
  }
  return is_file_from_asset_catalog;
}

}  // namespace

BundleData::BundleData() {}

BundleData::~BundleData() {}

void BundleData::AddBundleData(const Target* target) {
  DCHECK_EQ(target->output_type(), Target::BUNDLE_DATA);
  for (const auto& pattern : bundle_deps_filter_) {
    if (pattern.Matches(target->label()))
      return;
  }
  bundle_deps_.push_back(target);
}

void BundleData::OnTargetResolved(Target* owning_target) {
  // Only initialize file_rules_ and assets_catalog_sources for "create_bundle"
  // target (properties are only used by those targets).
  if (owning_target->output_type() != Target::CREATE_BUNDLE)
    return;

  UniqueVector<const Target*> assets_catalog_deps;
  UniqueVector<SourceFile> assets_catalog_sources;

  for (const Target* target : bundle_deps_) {
    SourceFiles file_rule_sources;
    for (const SourceFile& source_file : target->sources()) {
      SourceFile assets_catalog;
      if (IsSourceFileFromAssetsCatalog(source_file.value(), &assets_catalog)) {
        assets_catalog_sources.push_back(assets_catalog);
        assets_catalog_deps.push_back(target);
      } else {
        file_rule_sources.push_back(source_file);
      }
    }

    if (!file_rule_sources.empty()) {
      DCHECK_EQ(target->action_values().outputs().list().size(), 1u);
      file_rules_.push_back(
          BundleFileRule(target, file_rule_sources,
                         target->action_values().outputs().list()[0]));
    }
  }

  assets_catalog_deps_.insert(assets_catalog_deps_.end(),
                              assets_catalog_deps.begin(),
                              assets_catalog_deps.end());
  assets_catalog_sources_.insert(assets_catalog_sources_.end(),
                                 assets_catalog_sources.begin(),
                                 assets_catalog_sources.end());

  GetSourceFiles(&owning_target->sources());
}

void BundleData::GetSourceFiles(SourceFiles* sources) const {
  for (const BundleFileRule& file_rule : file_rules_) {
    sources->insert(sources->end(), file_rule.sources().begin(),
                    file_rule.sources().end());
  }
  sources->insert(sources->end(), assets_catalog_sources_.begin(),
                  assets_catalog_sources_.end());
  if (!code_signing_script_.is_null()) {
    sources->insert(sources->end(), code_signing_sources_.begin(),
                    code_signing_sources_.end());
  }
}

void BundleData::GetOutputFiles(const Settings* settings,
                                OutputFiles* outputs) const {
  SourceFiles outputs_as_sources;
  GetOutputsAsSourceFiles(settings, &outputs_as_sources);
  for (const SourceFile& source_file : outputs_as_sources)
    outputs->push_back(OutputFile(settings->build_settings(), source_file));
}

void BundleData::GetOutputsAsSourceFiles(
    const Settings* settings,
    SourceFiles* outputs_as_source) const {
  for (const BundleFileRule& file_rule : file_rules_) {
    for (const SourceFile& source : file_rule.sources()) {
      outputs_as_source->push_back(
          file_rule.ApplyPatternToSource(settings, *this, source));
    }
  }

  if (!assets_catalog_sources_.empty())
    outputs_as_source->push_back(GetCompiledAssetCatalogPath());

  if (!partial_info_plist_.is_null())
    outputs_as_source->push_back(partial_info_plist_);

  if (!code_signing_script_.is_null()) {
    std::vector<SourceFile> code_signing_output_files;
    SubstitutionWriter::GetListAsSourceFiles(code_signing_outputs_,
                                             &code_signing_output_files);
    outputs_as_source->insert(outputs_as_source->end(),
                              code_signing_output_files.begin(),
                              code_signing_output_files.end());
  }

  if (!root_dir_.is_null())
    outputs_as_source->push_back(GetBundleRootDirOutput(settings));
}

SourceFile BundleData::GetCompiledAssetCatalogPath() const {
  DCHECK(!assets_catalog_sources_.empty());
  std::string assets_car_path = resources_dir_.value() + "/Assets.car";
  return SourceFile(SourceFile::SWAP_IN, &assets_car_path);
}

SourceFile BundleData::GetBundleRootDirOutput(const Settings* settings) const {
  std::string root_dir_value = root_dir().value();
  size_t last_separator = root_dir_value.rfind('/');
  if (last_separator != std::string::npos)
    root_dir_value = root_dir_value.substr(0, last_separator);

  return SourceFile(SourceFile::SWAP_IN, &root_dir_value);
}

SourceDir BundleData::GetBundleRootDirOutputAsDir(
    const Settings* settings) const {
  return SourceDir(GetBundleRootDirOutput(settings).value());
}
