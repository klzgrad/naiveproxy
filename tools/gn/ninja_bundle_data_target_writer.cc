// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/ninja_bundle_data_target_writer.h"

#include "tools/gn/output_file.h"
#include "tools/gn/settings.h"
#include "tools/gn/target.h"

NinjaBundleDataTargetWriter::NinjaBundleDataTargetWriter(const Target* target,
                                                         std::ostream& out)
    : NinjaTargetWriter(target, out) {}

NinjaBundleDataTargetWriter::~NinjaBundleDataTargetWriter() = default;

void NinjaBundleDataTargetWriter::Run() {
  std::vector<OutputFile> output_files;
  for (const SourceFile& source_file : target_->sources()) {
    output_files.push_back(
        OutputFile(settings_->build_settings(), source_file));
  }

  std::vector<OutputFile> input_deps = WriteInputDepsStampAndGetDep(
      std::vector<const Target*>(), /*num_stamp_uses=*/1);
  output_files.insert(output_files.end(), input_deps.begin(), input_deps.end());

  std::vector<OutputFile> order_only_deps;
  for (const auto& pair : target_->data_deps())
    order_only_deps.push_back(pair.ptr->dependency_output_file());

  WriteStampForTarget(output_files, order_only_deps);
}
