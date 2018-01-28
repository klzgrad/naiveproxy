// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/ninja_action_target_writer.h"

#include <stddef.h>

#include "base/strings/string_util.h"
#include "tools/gn/deps_iterator.h"
#include "tools/gn/err.h"
#include "tools/gn/pool.h"
#include "tools/gn/settings.h"
#include "tools/gn/string_utils.h"
#include "tools/gn/substitution_writer.h"
#include "tools/gn/target.h"

NinjaActionTargetWriter::NinjaActionTargetWriter(const Target* target,
                                                 std::ostream& out)
    : NinjaTargetWriter(target, out),
      path_output_no_escaping_(
          target->settings()->build_settings()->build_dir(),
          target->settings()->build_settings()->root_path_utf8(),
          ESCAPE_NONE) {
}

NinjaActionTargetWriter::~NinjaActionTargetWriter() = default;

void NinjaActionTargetWriter::Run() {
  std::string custom_rule_name = WriteRuleDefinition();

  // Collect our deps to pass as "extra hard dependencies" for input deps. This
  // will force all of the action's dependencies to be completed before the
  // action is run. Usually, if an action has a dependency, it will be
  // operating on the result of that previous step, so we need to be sure to
  // serialize these.
  std::vector<const Target*> extra_hard_deps;
  for (const auto& pair : target_->GetDeps(Target::DEPS_LINKED))
    extra_hard_deps.push_back(pair.ptr);

  // For ACTIONs this is a bit inefficient since it creates an input dep
  // stamp file even though we're only going to use it once. It would save a
  // build step to skip this and write the order-only deps directly on the
  // build rule. This should probably be handled by WriteInputDepsStampAndGetDep
  // automatically if we supply a count of sources (so it can optimize based on
  // how many times things would be duplicated).
  OutputFile input_dep = WriteInputDepsStampAndGetDep(extra_hard_deps);
  out_ << std::endl;

  // Collects all output files for writing below.
  std::vector<OutputFile> output_files;

  if (target_->output_type() == Target::ACTION_FOREACH) {
    // Write separate build lines for each input source file.
    WriteSourceRules(custom_rule_name, input_dep, &output_files);
  } else {
    DCHECK(target_->output_type() == Target::ACTION);

    // Write a rule that invokes the script once with the outputs as outputs,
    // and the data as inputs. It does not depend on the sources.
    out_ << "build";
    SubstitutionWriter::GetListAsOutputFiles(
        settings_, target_->action_values().outputs(), &output_files);
    path_output_.WriteFiles(out_, output_files);

    out_ << ": " << custom_rule_name;
    if (!input_dep.value().empty()) {
      // As in WriteSourceRules, we want to force this target to rebuild any
      // time any of its dependencies change.
      out_ << " | ";
      path_output_.WriteFile(out_, input_dep);
    }
    out_ << std::endl;
    if (target_->action_values().has_depfile()) {
      out_ << "  depfile = ";
      WriteDepfile(SourceFile());
      out_ << std::endl;
    }
    if (target_->action_values().pool().ptr) {
      out_ << "  pool = ";
      out_ << target_->action_values().pool().ptr->GetNinjaName(
          settings_->default_toolchain_label());
      out_ << std::endl;
    }
  }
  out_ << std::endl;

  // Write the stamp, which also depends on all data deps. These are needed at
  // runtime and should be compiled when the action is, but don't need to be
  // done before we run the action.
  std::vector<OutputFile> data_outs;
  for (const auto& dep : target_->data_deps())
    data_outs.push_back(dep.ptr->dependency_output_file());
  WriteStampForTarget(output_files, data_outs);
}

std::string NinjaActionTargetWriter::WriteRuleDefinition() {
  // Make a unique name for this rule.
  //
  // Use a unique name for the response file when there are multiple build
  // steps so that they don't stomp on each other. When there are no sources,
  // there will be only one invocation so we can use a simple name.
  std::string target_label = target_->label().GetUserVisibleName(true);
  std::string custom_rule_name(target_label);
  base::ReplaceChars(custom_rule_name, ":/()", "_", &custom_rule_name);
  custom_rule_name.append("_rule");

  const SubstitutionList& args = target_->action_values().args();
  EscapeOptions args_escape_options;
  args_escape_options.mode = ESCAPE_NINJA_COMMAND;

  out_ << "rule " << custom_rule_name << std::endl;

  if (target_->action_values().uses_rsp_file()) {
    // Needs a response file. The unique_name part is for action_foreach so
    // each invocation of the rule gets a different response file. This isn't
    // strictly necessary for regular one-shot actions, but it's easier to
    // just always define unique_name.
    std::string rspfile = custom_rule_name;
    if (!target_->sources().empty())
      rspfile += ".$unique_name";
    rspfile += ".rsp";
    out_ << "  rspfile = " << rspfile << std::endl;

    // Response file contents.
    out_ << "  rspfile_content =";
    for (const auto& arg :
         target_->action_values().rsp_file_contents().list()) {
      out_ << " ";
      SubstitutionWriter::WriteWithNinjaVariables(
          arg, args_escape_options, out_);
    }
    out_ << std::endl;
  }

  out_ << "  command = ";
  path_output_.WriteFile(out_, settings_->build_settings()->python_path());
  out_ << " ";
  path_output_.WriteFile(out_, target_->action_values().script());
  for (const auto& arg : args.list()) {
    out_ << " ";
    SubstitutionWriter::WriteWithNinjaVariables(
        arg, args_escape_options, out_);
  }
  out_ << std::endl;
  out_ << "  description = ACTION " << target_label << std::endl;
  out_ << "  restat = 1" << std::endl;
  const Tool* tool = target_->toolchain()->GetTool(Toolchain::TYPE_ACTION);
  if (tool && tool->pool().ptr) {
    out_ << "  pool = ";
    out_ << tool->pool().ptr->GetNinjaName(
        settings_->default_toolchain_label());
    out_ << std::endl;
  }

  return custom_rule_name;
}

void NinjaActionTargetWriter::WriteSourceRules(
    const std::string& custom_rule_name,
    const OutputFile& input_dep,
    std::vector<OutputFile>* output_files) {
  EscapeOptions args_escape_options;
  args_escape_options.mode = ESCAPE_NINJA_COMMAND;
  // We're writing the substitution values, these should not be quoted since
  // they will get pasted into the real command line.
  args_escape_options.inhibit_quoting = true;

  const Target::FileList& sources = target_->sources();
  for (size_t i = 0; i < sources.size(); i++) {
    out_ << "build";
    WriteOutputFilesForBuildLine(sources[i], output_files);

    out_ << ": " << custom_rule_name << " ";
    path_output_.WriteFile(out_, sources[i]);
    if (!input_dep.value().empty()) {
      // Using "|" for the dependencies forces all implicit dependencies to be
      // fully up to date before running the action, and will re-run this
      // action if any input dependencies change. This is important because
      // this action may consume the outputs of previous steps.
      out_ << " | ";
      path_output_.WriteFile(out_, input_dep);
    }
    out_ << std::endl;

    // Response files require a unique name be defined.
    if (target_->action_values().uses_rsp_file())
      out_ << "  unique_name = " << i << std::endl;

    // The required types is the union of the args and response file. This
    // might theoretically duplicate a definition if the same substitution is
    // used in both the args and the response file. However, this should be
    // very unusual (normally the substitutions will go in one place or the
    // other) and the redundant assignment won't bother Ninja.
    SubstitutionWriter::WriteNinjaVariablesForSource(
        target_, settings_, sources[i],
        target_->action_values().args().required_types(),
        args_escape_options, out_);
    SubstitutionWriter::WriteNinjaVariablesForSource(
        target_, settings_, sources[i],
        target_->action_values().rsp_file_contents().required_types(),
        args_escape_options, out_);

    if (target_->action_values().has_depfile()) {
      out_ << "  depfile = ";
      WriteDepfile(sources[i]);
      out_ << std::endl;
    }
  }
}

void NinjaActionTargetWriter::WriteOutputFilesForBuildLine(
    const SourceFile& source,
    std::vector<OutputFile>* output_files) {
  size_t first_output_index = output_files->size();

  SubstitutionWriter::ApplyListToSourceAsOutputFile(
      target_, settings_, target_->action_values().outputs(), source,
      output_files);

  for (size_t i = first_output_index; i < output_files->size(); i++) {
    out_ << " ";
    path_output_.WriteFile(out_, (*output_files)[i]);
  }
}

void NinjaActionTargetWriter::WriteDepfile(const SourceFile& source) {
  path_output_.WriteFile(out_,
      SubstitutionWriter::ApplyPatternToSourceAsOutputFile(
          target_, settings_, target_->action_values().depfile(), source));
}
