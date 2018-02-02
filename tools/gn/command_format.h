// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_COMAND_FORMAT_H_
#define TOOLS_GN_COMAND_FORMAT_H_

#include <string>

class Setup;
class SourceFile;

namespace commands {

bool FormatFileToString(Setup* setup,
                        const SourceFile& file,
                        bool dump_tree,
                        std::string* output);

bool FormatStringToString(const std::string& input,
                          bool dump_tree,
                          std::string* output);

}  // namespace commands

#endif  // TOOLS_GN_COMAND_FORMAT_H_

