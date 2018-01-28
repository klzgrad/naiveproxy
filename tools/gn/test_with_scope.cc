// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/test_with_scope.h"

#include <utility>

#include "base/bind.h"
#include "tools/gn/parser.h"
#include "tools/gn/tokenizer.h"

namespace {

BuildSettings CreateBuildSettingsForTest() {
  BuildSettings build_settings;
  build_settings.SetBuildDir(SourceDir("//out/Debug/"));
  return build_settings;
}

}  // namespace

TestWithScope::TestWithScope()
    : build_settings_(CreateBuildSettingsForTest()),
      settings_(&build_settings_, std::string()),
      toolchain_(&settings_, Label(SourceDir("//toolchain/"), "default")),
      scope_(&settings_),
      scope_progammatic_provider_(&scope_, true) {
  build_settings_.set_print_callback(
      base::Bind(&TestWithScope::AppendPrintOutput, base::Unretained(this)));

  settings_.set_toolchain_label(toolchain_.label());
  settings_.set_default_toolchain_label(toolchain_.label());

  SetupToolchain(&toolchain_);
  scope_.set_item_collector(&items_);
}

TestWithScope::~TestWithScope() {
}

Label TestWithScope::ParseLabel(const std::string& str) const {
  Err err;
  Label result = Label::Resolve(SourceDir("//"), toolchain_.label(),
                                Value(nullptr, str), &err);
  CHECK(!err.has_error());
  return result;
}

bool TestWithScope::ExecuteSnippet(const std::string& str, Err* err) {
  TestParseInput input(str);
  if (input.has_error()) {
    *err = input.parse_err();
    return false;
  }

  size_t first_item = items_.size();
  input.parsed()->Execute(&scope_, err);
  if (err->has_error())
    return false;

  for (size_t i = first_item; i < items_.size(); ++i) {
    CHECK(items_[i]->AsTarget() != nullptr)
        << "Only targets are supported in ExecuteSnippet()";
    items_[i]->AsTarget()->SetToolchain(&toolchain_);
    if (!items_[i]->OnResolved(err))
      return false;
  }
  return true;
}

// static
void TestWithScope::SetupToolchain(Toolchain* toolchain) {
  Err err;

  // CC
  std::unique_ptr<Tool> cc_tool(new Tool);
  SetCommandForTool(
      "cc {{source}} {{cflags}} {{cflags_c}} {{defines}} {{include_dirs}} "
      "-o {{output}}",
      cc_tool.get());
  cc_tool->set_outputs(SubstitutionList::MakeForTest(
      "{{source_out_dir}}/{{target_output_name}}.{{source_name_part}}.o"));
  toolchain->SetTool(Toolchain::TYPE_CC, std::move(cc_tool));

  // CXX
  std::unique_ptr<Tool> cxx_tool(new Tool);
  SetCommandForTool(
      "c++ {{source}} {{cflags}} {{cflags_cc}} {{defines}} {{include_dirs}} "
      "-o {{output}}",
      cxx_tool.get());
  cxx_tool->set_outputs(SubstitutionList::MakeForTest(
      "{{source_out_dir}}/{{target_output_name}}.{{source_name_part}}.o"));
  toolchain->SetTool(Toolchain::TYPE_CXX, std::move(cxx_tool));

  // OBJC
  std::unique_ptr<Tool> objc_tool(new Tool);
  SetCommandForTool(
      "objcc {{source}} {{cflags}} {{cflags_objc}} {{defines}} "
      "{{include_dirs}} -o {{output}}",
      objc_tool.get());
  objc_tool->set_outputs(SubstitutionList::MakeForTest(
      "{{source_out_dir}}/{{target_output_name}}.{{source_name_part}}.o"));
  toolchain->SetTool(Toolchain::TYPE_OBJC, std::move(objc_tool));

  // OBJC
  std::unique_ptr<Tool> objcxx_tool(new Tool);
  SetCommandForTool(
      "objcxx {{source}} {{cflags}} {{cflags_objcc}} {{defines}} "
      "{{include_dirs}} -o {{output}}",
      objcxx_tool.get());
  objcxx_tool->set_outputs(SubstitutionList::MakeForTest(
      "{{source_out_dir}}/{{target_output_name}}.{{source_name_part}}.o"));
  toolchain->SetTool(Toolchain::TYPE_OBJCXX, std::move(objcxx_tool));

  // Don't use RC and ASM tools in unit tests yet. Add here if needed.

  // ALINK
  std::unique_ptr<Tool> alink_tool(new Tool);
  SetCommandForTool("ar {{output}} {{source}}", alink_tool.get());
  alink_tool->set_lib_switch("-l");
  alink_tool->set_lib_dir_switch("-L");
  alink_tool->set_output_prefix("lib");
  alink_tool->set_outputs(SubstitutionList::MakeForTest(
      "{{target_out_dir}}/{{target_output_name}}.a"));
  toolchain->SetTool(Toolchain::TYPE_ALINK, std::move(alink_tool));

  // SOLINK
  std::unique_ptr<Tool> solink_tool(new Tool);
  SetCommandForTool("ld -shared -o {{target_output_name}}.so {{inputs}} "
      "{{ldflags}} {{libs}}", solink_tool.get());
  solink_tool->set_lib_switch("-l");
  solink_tool->set_lib_dir_switch("-L");
  solink_tool->set_output_prefix("lib");
  solink_tool->set_default_output_extension(".so");
  solink_tool->set_outputs(SubstitutionList::MakeForTest(
      "{{root_out_dir}}/{{target_output_name}}{{output_extension}}"));
  toolchain->SetTool(Toolchain::TYPE_SOLINK, std::move(solink_tool));

  // SOLINK_MODULE
  std::unique_ptr<Tool> solink_module_tool(new Tool);
  SetCommandForTool("ld -bundle -o {{target_output_name}}.so {{inputs}} "
      "{{ldflags}} {{libs}}", solink_module_tool.get());
  solink_module_tool->set_lib_switch("-l");
  solink_module_tool->set_lib_dir_switch("-L");
  solink_module_tool->set_output_prefix("lib");
  solink_module_tool->set_default_output_extension(".so");
  solink_module_tool->set_outputs(SubstitutionList::MakeForTest(
      "{{root_out_dir}}/{{target_output_name}}{{output_extension}}"));
  toolchain->SetTool(Toolchain::TYPE_SOLINK_MODULE,
                     std::move(solink_module_tool));

  // LINK
  std::unique_ptr<Tool> link_tool(new Tool);
  SetCommandForTool("ld -o {{target_output_name}} {{source}} "
      "{{ldflags}} {{libs}}", link_tool.get());
  link_tool->set_lib_switch("-l");
  link_tool->set_lib_dir_switch("-L");
  link_tool->set_outputs(SubstitutionList::MakeForTest(
      "{{root_out_dir}}/{{target_output_name}}"));
  toolchain->SetTool(Toolchain::TYPE_LINK, std::move(link_tool));

  // STAMP
  std::unique_ptr<Tool> stamp_tool(new Tool);
  SetCommandForTool("touch {{output}}", stamp_tool.get());
  toolchain->SetTool(Toolchain::TYPE_STAMP, std::move(stamp_tool));

  // COPY
  std::unique_ptr<Tool> copy_tool(new Tool);
  SetCommandForTool("cp {{source}} {{output}}", copy_tool.get());
  toolchain->SetTool(Toolchain::TYPE_COPY, std::move(copy_tool));

  // COPY_BUNDLE_DATA
  std::unique_ptr<Tool> copy_bundle_data_tool(new Tool);
  SetCommandForTool("cp {{source}} {{output}}", copy_bundle_data_tool.get());
  toolchain->SetTool(Toolchain::TYPE_COPY_BUNDLE_DATA,
                     std::move(copy_bundle_data_tool));

  // COMPILE_XCASSETS
  std::unique_ptr<Tool> compile_xcassets_tool(new Tool);
  SetCommandForTool("touch {{output}}", compile_xcassets_tool.get());
  toolchain->SetTool(Toolchain::TYPE_COMPILE_XCASSETS,
                     std::move(compile_xcassets_tool));

  toolchain->ToolchainSetupComplete();
}

// static
void TestWithScope::SetCommandForTool(const std::string& cmd, Tool* tool) {
  Err err;
  SubstitutionPattern command;
  command.Parse(cmd, nullptr, &err);
  CHECK(!err.has_error())
      << "Couldn't parse \"" << cmd << "\", " << "got " << err.message();
  tool->set_command(command);
}

void TestWithScope::AppendPrintOutput(const std::string& str) {
  print_output_.append(str);
}

TestParseInput::TestParseInput(const std::string& input)
    : input_file_(SourceFile("//test")) {
  input_file_.SetContents(input);

  tokens_ = Tokenizer::Tokenize(&input_file_, &parse_err_);
  if (!parse_err_.has_error())
    parsed_ = Parser::Parse(tokens_, &parse_err_);
}

TestParseInput::~TestParseInput() {
}

TestTarget::TestTarget(const TestWithScope& setup,
                       const std::string& label_string,
                       Target::OutputType type)
    : Target(setup.settings(), setup.ParseLabel(label_string)) {
  visibility().SetPublic();
  set_output_type(type);
  SetToolchain(setup.toolchain());
}

TestTarget::~TestTarget() {
}
