// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_NINJA_COPY_TARGET_WRITER_H_
#define TOOLS_GN_NINJA_COPY_TARGET_WRITER_H_

#include "base/macros.h"
#include "tools/gn/ninja_target_writer.h"

// Writes a .ninja file for a copy target type.
class NinjaCopyTargetWriter : public NinjaTargetWriter {
 public:
  NinjaCopyTargetWriter(const Target* target, std::ostream& out);
  ~NinjaCopyTargetWriter() override;

  void Run() override;

 private:
  // Writes the rules top copy the file(s), putting the computed output file
  // name(s) into the given vector.
  void WriteCopyRules(std::vector<OutputFile>* output_files);

  DISALLOW_COPY_AND_ASSIGN(NinjaCopyTargetWriter);
};

#endif  // TOOLS_GN_NINJA_COPY_TARGET_WRITER_H_
