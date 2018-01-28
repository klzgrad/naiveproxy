// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/config_values_generator.h"

#include "base/strings/string_util.h"
#include "tools/gn/config_values.h"
#include "tools/gn/scope.h"
#include "tools/gn/settings.h"
#include "tools/gn/value.h"
#include "tools/gn/value_extractors.h"
#include "tools/gn/variables.h"

namespace {

void GetStringList(
    Scope* scope,
    const char* var_name,
    ConfigValues* config_values,
    std::vector<std::string>& (ConfigValues::* accessor)(),
    Err* err) {
  const Value* value = scope->GetValue(var_name, true);
  if (!value)
    return;  // No value, empty input and succeed.

  ExtractListOfStringValues(*value, &(config_values->*accessor)(), err);
}

void GetDirList(
    Scope* scope,
    const char* var_name,
    ConfigValues* config_values,
    const SourceDir input_dir,
    std::vector<SourceDir>& (ConfigValues::* accessor)(),
    Err* err) {
  const Value* value = scope->GetValue(var_name, true);
  if (!value)
    return;  // No value, empty input and succeed.

  std::vector<SourceDir> result;
  ExtractListOfRelativeDirs(scope->settings()->build_settings(),
                            *value, input_dir, &result, err);
  (config_values->*accessor)().swap(result);
}

}  // namespace

ConfigValuesGenerator::ConfigValuesGenerator(
    ConfigValues* dest_values,
    Scope* scope,
    const SourceDir& input_dir,
    Err* err)
    : config_values_(dest_values),
      scope_(scope),
      input_dir_(input_dir),
      err_(err) {
}

ConfigValuesGenerator::~ConfigValuesGenerator() {
}

void ConfigValuesGenerator::Run() {
#define FILL_STRING_CONFIG_VALUE(name) \
    GetStringList(scope_, #name, config_values_, &ConfigValues::name, err_);
#define FILL_DIR_CONFIG_VALUE(name) \
    GetDirList(scope_, #name, config_values_, input_dir_, \
               &ConfigValues::name, err_);

  FILL_STRING_CONFIG_VALUE(arflags)
  FILL_STRING_CONFIG_VALUE(asmflags)
  FILL_STRING_CONFIG_VALUE(cflags)
  FILL_STRING_CONFIG_VALUE(cflags_c)
  FILL_STRING_CONFIG_VALUE(cflags_cc)
  FILL_STRING_CONFIG_VALUE(cflags_objc)
  FILL_STRING_CONFIG_VALUE(cflags_objcc)
  FILL_STRING_CONFIG_VALUE(defines)
  FILL_DIR_CONFIG_VALUE(   include_dirs)
  FILL_STRING_CONFIG_VALUE(ldflags)
  FILL_DIR_CONFIG_VALUE(   lib_dirs)

#undef FILL_STRING_CONFIG_VALUE
#undef FILL_DIR_CONFIG_VALUE

  // Libs
  const Value* libs_value = scope_->GetValue("libs", true);
  if (libs_value) {
    ExtractListOfLibs(scope_->settings()->build_settings(), *libs_value,
                      input_dir_, &config_values_->libs(), err_);
  }

  // Precompiled headers.
  const Value* precompiled_header_value =
      scope_->GetValue(variables::kPrecompiledHeader, true);
  if (precompiled_header_value) {
    if (!precompiled_header_value->VerifyTypeIs(Value::STRING, err_))
      return;

    // Check for common errors. This is a string and not a file.
    const std::string& pch_string = precompiled_header_value->string_value();
    if (base::StartsWith(pch_string, "//", base::CompareCase::SENSITIVE)) {
      *err_ = Err(*precompiled_header_value,
          "This precompiled_header value is wrong.",
          "You need to specify a string that the compiler will match against\n"
          "the #include lines rather than a GN-style file name.\n");
      return;
    }
    config_values_->set_precompiled_header(pch_string);
  }

  const Value* precompiled_source_value =
      scope_->GetValue(variables::kPrecompiledSource, true);
  if (precompiled_source_value) {
    config_values_->set_precompiled_source(
        input_dir_.ResolveRelativeFile(
            *precompiled_source_value, err_,
            scope_->settings()->build_settings()->root_path_utf8()));
    if (err_->has_error())
      return;
  }
}
