// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/settings.h"

#include "base/logging.h"
#include "build/build_config.h"
#include "tools/gn/filesystem_utils.h"

Settings::Settings(const BuildSettings* build_settings,
                   const std::string& output_subdir_name)
    : build_settings_(build_settings),
      import_manager_(),
      base_config_(this),
      greedy_target_generation_(false) {
  if (output_subdir_name.empty()) {
    toolchain_output_dir_ = build_settings->build_dir();
  } else {
    // We guarantee this ends in a slash.
    DCHECK(output_subdir_name[output_subdir_name.size() - 1] == '/');
    toolchain_output_subdir_.value().append(output_subdir_name);

    DCHECK(!build_settings->build_dir().is_null());
    toolchain_output_dir_ = SourceDir(build_settings->build_dir().value() +
                                      toolchain_output_subdir_.value());
  }
  // The output dir will be null in some tests and when invoked to parsed
  // one-off data without doing generation.
  if (!toolchain_output_dir_.is_null())
    toolchain_gen_dir_ = SourceDir(toolchain_output_dir_.value() + "gen/");
}

Settings::~Settings() {
}
