// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/build_settings.h"
#include "tools/gn/functions.h"
#include "tools/gn/loader.h"
#include "tools/gn/parse_tree.h"
#include "tools/gn/scope.h"
#include "tools/gn/settings.h"

namespace functions {

const char kSetDefaultToolchain[] = "set_default_toolchain";
const char kSetDefaultToolchain_HelpShort[] =
    "set_default_toolchain: Sets the default toolchain name.";
const char kSetDefaultToolchain_Help[] =
    R"(set_default_toolchain: Sets the default toolchain name.

  set_default_toolchain(toolchain_label)

  The given label should identify a toolchain definition (see "gn help
  toolchain"). This toolchain will be used for all targets unless otherwise
  specified.

  This function is only valid to call during the processing of the build
  configuration file. Since the build configuration file is processed
  separately for each toolchain, this function will be a no-op when called
  under any non-default toolchains.

  For example, the default toolchain should be appropriate for the current
  environment. If the current environment is 32-bit and somebody references a
  target with a 64-bit toolchain, we wouldn't want processing of the build
  config file for the 64-bit toolchain to reset the default toolchain to
  64-bit, we want to keep it 32-bits.

Argument

  toolchain_label
      Toolchain name.

Example

  # Set default toolchain only has an effect when run in the context of the
  # default toolchain. Pick the right one according to the current CPU
  # architecture.
  if (target_cpu == "x64") {
    set_default_toolchain("//toolchains:64")
  } else if (target_cpu == "x86") {
    set_default_toolchain("//toolchains:32")
  }
)";

Value RunSetDefaultToolchain(Scope* scope,
                             const FunctionCallNode* function,
                             const std::vector<Value>& args,
                             Err* err) {
  if (!scope->IsProcessingBuildConfig()) {
    *err = Err(function->function(), "Must be called from build config.",
        "set_default_toolchain can only be called from the build configuration "
        "file.");
    return Value();
  }

  // When the loader is expecting the default toolchain to be set, it will set
  // this key on the scope to point to the destination.
  Label* default_toolchain_dest = static_cast<Label*>(
      scope->GetProperty(Loader::kDefaultToolchainKey, nullptr));
  if (!default_toolchain_dest)
    return Value();

  const SourceDir& current_dir = scope->GetSourceDir();
  const Label& default_toolchain = ToolchainLabelForScope(scope);

  if (!EnsureSingleStringArg(function, args, err))
    return Value();
  Label toolchain_label(
      Label::Resolve(current_dir, default_toolchain, args[0], err));
  if (toolchain_label.is_null())
    return Value();

  *default_toolchain_dest = toolchain_label;
  return Value();
}

}  // namespace functions
