// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_NINJA_CREATE_BUNDLE_TARGET_WRITER_H_
#define TOOLS_GN_NINJA_CREATE_BUNDLE_TARGET_WRITER_H_

#include "base/macros.h"
#include "tools/gn/ninja_target_writer.h"

class BundleFileRule;

// Writes a .ninja file for a bundle_data target type.
class NinjaCreateBundleTargetWriter : public NinjaTargetWriter {
 public:
  NinjaCreateBundleTargetWriter(const Target* target, std::ostream& out);
  ~NinjaCreateBundleTargetWriter() override;

  void Run() override;

 private:
  // Writes the Ninja rule for invoking the code signing script.
  //
  // Returns the name of the custom rule generated for the code signing step if
  // defined, otherwise returns an empty string.
  std::string WriteCodeSigningRuleDefinition();

  // Writes the steps to copy files into the bundle.
  //
  // The list of newly created files will be added to |output_files|.
  void WriteCopyBundleDataSteps(const std::vector<OutputFile>& order_only_deps,
                                std::vector<OutputFile>* output_files);

  // Writes the step to copy files BundleFileRule into the bundle.
  //
  // The list of newly created files will be added to |output_files|.
  void WriteCopyBundleFileRuleSteps(
      const BundleFileRule& file_rule,
      const std::vector<OutputFile>& order_only_deps,
      std::vector<OutputFile>* output_files);

  // Writes the step to compile assets catalogs.
  //
  // The list of newly created files will be added to |output_files|.
  void WriteCompileAssetsCatalogStep(
      const std::vector<OutputFile>& order_only_deps,
      std::vector<OutputFile>* output_files);

  // Writes the stamp file for the assets catalog compilation input
  // dependencies.
  OutputFile WriteCompileAssetsCatalogInputDepsStamp(
      const std::vector<const Target*>& dependencies);

  // Writes the code signing step (if a script is defined).
  //
  // The list of newly created files will be added to |output_files|. As the
  // code signing may depends on the full bundle structure, this step will
  // depends on all files generated via other rules.
  void WriteCodeSigningStep(const std::string& code_signing_rule_name,
                            const std::vector<OutputFile>& order_only_deps,
                            std::vector<OutputFile>* output_files);

  // Writes the stamp file for the code signing input dependencies.
  OutputFile WriteCodeSigningInputDepsStamp(
      const std::vector<OutputFile>& order_only_deps,
      std::vector<OutputFile>* output_files);

  DISALLOW_COPY_AND_ASSIGN(NinjaCreateBundleTargetWriter);
};

#endif  // TOOLS_GN_NINJA_CREATE_BUNDLE_TARGET_WRITER_H_
