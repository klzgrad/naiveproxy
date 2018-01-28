// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/bundle_file_rule.h"

#include "tools/gn/output_file.h"
#include "tools/gn/settings.h"
#include "tools/gn/substitution_pattern.h"
#include "tools/gn/substitution_writer.h"
#include "tools/gn/target.h"

BundleFileRule::BundleFileRule(const Target* bundle_data_target,
                               const std::vector<SourceFile> sources,
                               const SubstitutionPattern& pattern)
    : target_(bundle_data_target), sources_(sources), pattern_(pattern) {
  // target_ may be null during testing.
  DCHECK(!target_ || target_->output_type() == Target::BUNDLE_DATA);
}

BundleFileRule::BundleFileRule(const BundleFileRule& other) = default;

BundleFileRule::~BundleFileRule() = default;

SourceFile BundleFileRule::ApplyPatternToSource(
    const Settings* settings,
    const BundleData& bundle_data,
    const SourceFile& source_file) const {
  std::string output_path;
  for (const auto& subrange : pattern_.ranges()) {
    switch (subrange.type) {
      case SUBSTITUTION_LITERAL:
        output_path.append(subrange.literal);
        break;
      case SUBSTITUTION_BUNDLE_ROOT_DIR:
        output_path.append(bundle_data.root_dir().value());
        break;
      case SUBSTITUTION_BUNDLE_CONTENTS_DIR:
        output_path.append(bundle_data.contents_dir().value());
        break;
      case SUBSTITUTION_BUNDLE_RESOURCES_DIR:
        output_path.append(bundle_data.resources_dir().value());
        break;
      case SUBSTITUTION_BUNDLE_EXECUTABLE_DIR:
        output_path.append(bundle_data.executable_dir().value());
        break;
      case SUBSTITUTION_BUNDLE_PLUGINS_DIR:
        output_path.append(bundle_data.plugins_dir().value());
        break;
      default:
        output_path.append(SubstitutionWriter::GetSourceSubstitution(
            target_, target_->settings(), source_file, subrange.type,
            SubstitutionWriter::OUTPUT_ABSOLUTE, SourceDir()));
        break;
    }
  }
  return SourceFile(SourceFile::SWAP_IN, &output_path);
}

OutputFile BundleFileRule::ApplyPatternToSourceAsOutputFile(
    const Settings* settings,
    const BundleData& bundle_data,
    const SourceFile& source_file) const {
  return OutputFile(settings->build_settings(),
                    ApplyPatternToSource(settings, bundle_data, source_file));
}
