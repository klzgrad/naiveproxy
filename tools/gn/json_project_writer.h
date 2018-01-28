// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_JSON_WRITER_H_
#define TOOLS_GN_JSON_WRITER_H_

#include "tools/gn/err.h"
#include "tools/gn/target.h"

class Builder;
class BuildSettings;

class JSONProjectWriter {
 public:
  static bool RunAndWriteFiles(const BuildSettings* build_setting,
                               const Builder& builder,
                               const std::string& file_name,
                               const std::string& exec_script,
                               const std::string& exec_script_extra_args,
                               const std::string& dir_filter_string,
                               bool quiet,
                               Err* err);
};

#endif
