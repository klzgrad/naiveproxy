// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_BUNDLE_DATA_H_
#define TOOLS_GN_BUNDLE_DATA_H_

#include <map>
#include <string>
#include <vector>

#include "tools/gn/action_values.h"
#include "tools/gn/bundle_file_rule.h"
#include "tools/gn/source_dir.h"
#include "tools/gn/source_file.h"
#include "tools/gn/substitution_list.h"
#include "tools/gn/unique_vector.h"

class LabelPattern;
class OutputFile;
class Settings;
class Target;

// BundleData holds the information required by "create_bundle" target.
class BundleData {
 public:
  using UniqueTargets = UniqueVector<const Target*>;
  using SourceFiles = std::vector<SourceFile>;
  using OutputFiles = std::vector<OutputFile>;
  using BundleFileRules = std::vector<BundleFileRule>;

  BundleData();
  ~BundleData();

  // Adds a bundle_data target to the recursive collection of all bundle_data
  // that the target depends on.
  void AddBundleData(const Target* target);

  // Called upon resolution of the target owning this instance of BundleData.
  // |owning_target| is the owning target.
  void OnTargetResolved(Target* owning_target);

  // Returns the list of inputs.
  void GetSourceFiles(SourceFiles* sources) const;

  // Returns the list of outputs.
  void GetOutputFiles(const Settings* settings,
                      OutputFiles* outputs) const;

  // Returns the list of outputs as SourceFile.
  void GetOutputsAsSourceFiles(
      const Settings* settings,
      SourceFiles* outputs_as_source) const;

  // Returns the path to the compiled asset catalog. Only valid if
  // assets_catalog_sources() is not empty.
  SourceFile GetCompiledAssetCatalogPath() const;

  // Returns the path to the top-level directory of the bundle. This is
  // based on root_dir(), but since that can be Bundle.app/Contents/ or
  // any other subpath, this is just the most top-level directory (e.g.,
  // just Bundle.app/).
  //
  // Note that this is a SourceFile instead of a SourceDir. This is because
  // the output of a create_bundle rule is a single logical unit, even though
  // it is really a directory containing many outputs. This allows other
  // targets to treat the bundle as a single unit, rather than a collection
  // of its contents.
  SourceFile GetBundleRootDirOutput(const Settings* settings) const;

  // Performs GetBundleRootDirOutput but returns the result as a directory.
  SourceDir GetBundleRootDirOutputAsDir(const Settings* settings) const;

  // Returns the list of inputs for the compilation of the asset catalog.
  SourceFiles& assets_catalog_sources() { return assets_catalog_sources_; }
  const SourceFiles& assets_catalog_sources() const {
    return assets_catalog_sources_;
  }

  // Returns the list of dependencies for the compilation of the asset catalog.
  std::vector<const Target*> assets_catalog_deps() const {
    return assets_catalog_deps_;
  }

  BundleFileRules& file_rules() { return file_rules_; }
  const BundleFileRules& file_rules() const { return file_rules_; }

  SourceDir& root_dir() { return root_dir_; }
  const SourceDir& root_dir() const { return root_dir_; }

  SourceDir& contents_dir() { return contents_dir_; }
  const SourceDir& contents_dir() const { return contents_dir_; }

  SourceDir& resources_dir() { return resources_dir_; }
  const SourceDir& resources_dir() const { return resources_dir_; }

  SourceDir& executable_dir() { return executable_dir_; }
  const SourceDir& executable_dir() const { return executable_dir_; }

  SourceDir& plugins_dir() { return plugins_dir_; }
  const SourceDir& plugins_dir() const { return plugins_dir_; }

  std::map<std::string, std::string>& xcode_extra_attributes() {
    return xcode_extra_attributes_;
  }
  const std::map<std::string, std::string>& xcode_extra_attributes() const {
    return xcode_extra_attributes_;
  }

  std::string& product_type() { return product_type_; }
  const std::string& product_type() const { return product_type_; }

  std::string& xcode_test_application_name() {
    return xcode_test_application_name_;
  }
  const std::string& xcode_test_application_name() const {
    return xcode_test_application_name_;
  }

  void set_partial_info_plist(const SourceFile& partial_info_plist) {
    partial_info_plist_ = partial_info_plist;
  }
  const SourceFile& partial_info_plist() const { return partial_info_plist_; }

  void set_code_signing_script(const SourceFile& script_file) {
    code_signing_script_ = script_file;
  }
  const SourceFile& code_signing_script() const { return code_signing_script_; }

  std::vector<SourceFile>& code_signing_sources() {
    return code_signing_sources_;
  }
  const std::vector<SourceFile>& code_signing_sources() const {
    return code_signing_sources_;
  }

  SubstitutionList& code_signing_outputs() { return code_signing_outputs_; }
  const SubstitutionList& code_signing_outputs() const {
    return code_signing_outputs_;
  }

  SubstitutionList& code_signing_args() { return code_signing_args_; }
  const SubstitutionList& code_signing_args() const {
    return code_signing_args_;
  }

  std::vector<LabelPattern>& bundle_deps_filter() {
    return bundle_deps_filter_;
  }
  const std::vector<LabelPattern>& bundle_deps_filter() const {
    return bundle_deps_filter_;
  }

  // Recursive collection of all bundle_data that the target depends on.
  const UniqueTargets& bundle_deps() const { return bundle_deps_; }

 private:
  SourceFiles assets_catalog_sources_;
  std::vector<const Target*> assets_catalog_deps_;
  BundleFileRules file_rules_;
  UniqueTargets bundle_deps_;
  std::vector<LabelPattern> bundle_deps_filter_;

  // All those values are subdirectories relative to root_build_dir, and apart
  // from root_dir_, they are either equal to root_dir_ or subdirectories of it.
  SourceDir root_dir_;
  SourceDir contents_dir_;
  SourceDir resources_dir_;
  SourceDir executable_dir_;
  SourceDir plugins_dir_;

  // The specified attributes will append to the build settings of the generated
  // Xcode target.
  std::map<std::string, std::string> xcode_extra_attributes_;

  // This is the target type as known to Xcode. This is only used to generate
  // the Xcode project file when using --ide=xcode.
  std::string product_type_;

  // Each Xcode unit test or ui test target must have a test application target,
  // and this value corresponds to the target name. This is only used to
  // generate the Xcode project when using --ide=xcode.
  std::string xcode_test_application_name_;

  // Path to the partial Info.plist generated by the asset catalog compiler
  // (corresponds to {{bundle_partial_info_plist}} expansion).
  SourceFile partial_info_plist_;

  // Holds the values (script name, sources, outputs, script arguments) for the
  // code signing step if defined.
  SourceFile code_signing_script_;
  std::vector<SourceFile> code_signing_sources_;
  SubstitutionList code_signing_outputs_;
  SubstitutionList code_signing_args_;

  DISALLOW_COPY_AND_ASSIGN(BundleData);
};

#endif  // TOOLS_GN_BUNDLE_DATA_H_
