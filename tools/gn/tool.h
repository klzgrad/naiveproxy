// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_TOOL_H_
#define TOOLS_GN_TOOL_H_

#include <string>

#include "base/logging.h"
#include "base/macros.h"
#include "tools/gn/label.h"
#include "tools/gn/label_ptr.h"
#include "tools/gn/substitution_list.h"
#include "tools/gn/substitution_pattern.h"

class ParseNode;
class Pool;

class Tool {
 public:
  enum DepsFormat {
    DEPS_GCC = 0,
    DEPS_MSVC = 1
  };

  enum PrecompiledHeaderType {
    PCH_NONE = 0,
    PCH_GCC = 1,
    PCH_MSVC = 2
  };

  Tool();
  ~Tool();

  const ParseNode* defined_from() const { return defined_from_; }
  void set_defined_from(const ParseNode* df) { defined_from_ = df; }

  // Getters/setters ----------------------------------------------------------
  //
  // After the tool has had its attributes set, the caller must call
  // SetComplete(), at which point no other changes can be made.

  // Command to run.
  const SubstitutionPattern& command() const {
    return command_;
  }
  void set_command(SubstitutionPattern cmd) {
    DCHECK(!complete_);
    command_ = std::move(cmd);
  }

  // Should include a leading "." if nonempty.
  const std::string& default_output_extension() const {
    return default_output_extension_;
  }
  void set_default_output_extension(std::string ext) {
    DCHECK(!complete_);
    DCHECK(ext.empty() || ext[0] == '.');
    default_output_extension_ = std::move(ext);
  }

  const SubstitutionPattern& default_output_dir() const {
    return default_output_dir_;
  }
  void set_default_output_dir(SubstitutionPattern dir) {
    DCHECK(!complete_);
    default_output_dir_ = std::move(dir);
  }

  // Dependency file (if supported).
  const SubstitutionPattern& depfile() const {
    return depfile_;
  }
  void set_depfile(SubstitutionPattern df) {
    DCHECK(!complete_);
    depfile_ = std::move(df);
  }

  DepsFormat depsformat() const {
    return depsformat_;
  }
  void set_depsformat(DepsFormat f) {
    DCHECK(!complete_);
    depsformat_ = f;
  }

  PrecompiledHeaderType precompiled_header_type() const {
    return precompiled_header_type_;
  }
  void set_precompiled_header_type(PrecompiledHeaderType pch_type) {
    precompiled_header_type_ = pch_type;
  }

  const SubstitutionPattern& description() const {
    return description_;
  }
  void set_description(SubstitutionPattern desc) {
    DCHECK(!complete_);
    description_ = std::move(desc);
  }

  const std::string& lib_switch() const {
    return lib_switch_;
  }
  void set_lib_switch(std::string s) {
    DCHECK(!complete_);
    lib_switch_ = std::move(s);
  }

  const std::string& lib_dir_switch() const {
    return lib_dir_switch_;
  }
  void set_lib_dir_switch(std::string s) {
    DCHECK(!complete_);
    lib_dir_switch_ = std::move(s);
  }

  const SubstitutionList& outputs() const {
    return outputs_;
  }
  void set_outputs(SubstitutionList out) {
    DCHECK(!complete_);
    outputs_ = std::move(out);
  }

  // Should match files in the outputs() if nonempty.
  const SubstitutionPattern& link_output() const {
    return link_output_;
  }
  void set_link_output(SubstitutionPattern link_out) {
    DCHECK(!complete_);
    link_output_ = std::move(link_out);
  }

  const SubstitutionPattern& depend_output() const {
    return depend_output_;
  }
  void set_depend_output(SubstitutionPattern dep_out) {
    DCHECK(!complete_);
    depend_output_ = std::move(dep_out);
  }

  const SubstitutionList& runtime_outputs() const {
    return runtime_outputs_;
  }
  void set_runtime_outputs(SubstitutionList run_out) {
    DCHECK(!complete_);
    runtime_outputs_ = std::move(run_out);
  }

  const std::string& output_prefix() const {
    return output_prefix_;
  }
  void set_output_prefix(std::string s) {
    DCHECK(!complete_);
    output_prefix_ = std::move(s);
  }

  bool restat() const {
    return restat_;
  }
  void set_restat(bool r) {
    DCHECK(!complete_);
    restat_ = r;
  }

  const SubstitutionPattern& rspfile() const {
    return rspfile_;
  }
  void set_rspfile(SubstitutionPattern rsp) {
    DCHECK(!complete_);
    rspfile_ = std::move(rsp);
  }

  const SubstitutionPattern& rspfile_content() const {
    return rspfile_content_;
  }
  void set_rspfile_content(SubstitutionPattern content) {
    DCHECK(!complete_);
    rspfile_content_ = std::move(content);
  }

  const LabelPtrPair<Pool>& pool() const { return pool_; }
  void set_pool(LabelPtrPair<Pool> pool) { pool_ = std::move(pool); }

  // Other functions ----------------------------------------------------------

  // Called when the toolchain is saving this tool, after everything is filled
  // in.
  void SetComplete();

  // Returns true if this tool has separate outputs for dependency tracking
  // and linking.
  bool has_separate_solink_files() const {
    return !link_output_.empty() || !depend_output_.empty();
  }

  // Substitutions required by this tool.
  const SubstitutionBits& substitution_bits() const {
    DCHECK(complete_);
    return substitution_bits_;
  }

  bool OnResolved(Err* err);

 private:
  const ParseNode* defined_from_;

  SubstitutionPattern command_;
  std::string default_output_extension_;
  SubstitutionPattern default_output_dir_;
  SubstitutionPattern depfile_;
  DepsFormat depsformat_;
  PrecompiledHeaderType precompiled_header_type_;
  SubstitutionPattern description_;
  std::string lib_switch_;
  std::string lib_dir_switch_;
  SubstitutionList outputs_;
  SubstitutionPattern link_output_;
  SubstitutionPattern depend_output_;
  SubstitutionList runtime_outputs_;
  std::string output_prefix_;
  bool restat_;
  SubstitutionPattern rspfile_;
  SubstitutionPattern rspfile_content_;
  LabelPtrPair<Pool> pool_;

  bool complete_;

  SubstitutionBits substitution_bits_;

  DISALLOW_COPY_AND_ASSIGN(Tool);
};

#endif  // TOOLS_GN_TOOL_H_
