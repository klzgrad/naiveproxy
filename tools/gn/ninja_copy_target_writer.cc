// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/ninja_copy_target_writer.h"

#include "base/strings/string_util.h"
#include "tools/gn/ninja_utils.h"
#include "tools/gn/output_file.h"
#include "tools/gn/scheduler.h"
#include "tools/gn/string_utils.h"
#include "tools/gn/substitution_list.h"
#include "tools/gn/substitution_writer.h"
#include "tools/gn/target.h"
#include "tools/gn/toolchain.h"

NinjaCopyTargetWriter::NinjaCopyTargetWriter(const Target* target,
                                             std::ostream& out)
    : NinjaTargetWriter(target, out) {
}

NinjaCopyTargetWriter::~NinjaCopyTargetWriter() {
}

void NinjaCopyTargetWriter::Run() {
  const Tool* copy_tool = target_->toolchain()->GetTool(Toolchain::TYPE_COPY);
  if (!copy_tool) {
    g_scheduler->FailWithError(Err(
        nullptr, "Copy tool not defined",
        "The toolchain " +
            target_->toolchain()->label().GetUserVisibleName(false) +
            "\n used by target " + target_->label().GetUserVisibleName(false) +
            "\n doesn't define a \"copy\" tool."));
    return;
  }

  const Tool* stamp_tool = target_->toolchain()->GetTool(Toolchain::TYPE_STAMP);
  if (!stamp_tool) {
    g_scheduler->FailWithError(Err(
        nullptr, "Copy tool not defined",
        "The toolchain " +
            target_->toolchain()->label().GetUserVisibleName(false) +
            "\n used by target " + target_->label().GetUserVisibleName(false) +
            "\n doesn't define a \"stamp\" tool."));
    return;
  }

  // Figure out the substitutions used by the copy and stamp tools.
  SubstitutionBits required_bits = copy_tool->substitution_bits();
  required_bits.MergeFrom(stamp_tool->substitution_bits());

  // General target-related substitutions needed by both tools.
  WriteSharedVars(required_bits);

  std::vector<OutputFile> output_files;
  WriteCopyRules(&output_files);
  out_ << std::endl;
  WriteStampForTarget(output_files, std::vector<OutputFile>());
}

void NinjaCopyTargetWriter::WriteCopyRules(
    std::vector<OutputFile>* output_files) {
  CHECK(target_->action_values().outputs().list().size() == 1);
  const SubstitutionList& output_subst_list =
      target_->action_values().outputs();
  CHECK_EQ(1u, output_subst_list.list().size())
      << "Should have one entry exactly.";
  const SubstitutionPattern& output_subst = output_subst_list.list()[0];

  std::string tool_name =
      GetNinjaRulePrefixForToolchain(settings_) +
      Toolchain::ToolTypeToName(Toolchain::TYPE_COPY);

  OutputFile input_dep =
      WriteInputDepsStampAndGetDep(std::vector<const Target*>());

  // Note that we don't write implicit deps for copy steps. "copy" only
  // depends on the output files themselves, rather than having includes
  // (the possibility of generated #includes is the main reason for implicit
  // dependencies).
  //
  // It would seem that specifying implicit dependencies on the deps of the
  // copy command would still be harmeless. But Chrome implements copy tools
  // as hard links (much faster) which don't change the timestamp. If the
  // ninja rule looks like this:
  //   output: copy input | foo.stamp
  // The copy will not make a new timestamp on the output file, but the
  // foo.stamp file generated from a previous step will have a new timestamp.
  // The copy rule will therefore look out-of-date to Ninja and the rule will
  // get rebuilt.
  //
  // If this copy is copying a generated file, not listing the implicit
  // dependency will be fine as long as the input to the copy is properly
  // listed as the output from the step that generated it.
  //
  // Moreover, doing this assumes that the copy step is always a simple
  // locally run command, so there is no need for a toolchain dependency.
  //
  // Note that there is the need in some cases for order-only dependencies
  // where a command might need to make sure something else runs before it runs
  // to avoid conflicts. Such cases should be avoided where possible, but
  // sometimes that's not possible.
  for (const auto& input_file : target_->sources()) {
    OutputFile output_file =
        SubstitutionWriter::ApplyPatternToSourceAsOutputFile(
            target_, target_->settings(), output_subst, input_file);
    output_files->push_back(output_file);

    out_ << "build ";
    path_output_.WriteFile(out_, output_file);
    out_ << ": " << tool_name << " ";
    path_output_.WriteFile(out_, input_file);
    if (!input_dep.value().empty()) {
      out_ << " || ";
      path_output_.WriteFile(out_, input_dep);
    }
    out_ << std::endl;
  }
}
