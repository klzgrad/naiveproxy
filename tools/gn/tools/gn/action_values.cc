// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/action_values.h"

#include "tools/gn/settings.h"
#include "tools/gn/substitution_writer.h"
#include "tools/gn/target.h"

ActionValues::ActionValues() = default;

ActionValues::~ActionValues() = default;

void ActionValues::GetOutputsAsSourceFiles(
    const Target* target,
    std::vector<SourceFile>* result) const {
  if (target->output_type() == Target::BUNDLE_DATA) {
    // The bundle_data target has no output, the real output will be generated
    // by the create_bundle target.
  } else if (target->output_type() == Target::COPY_FILES ||
             target->output_type() == Target::ACTION_FOREACH) {
    // Copy and foreach applies the outputs to the sources.
    SubstitutionWriter::ApplyListToSources(target, target->settings(), outputs_,
                                           target->sources(), result);
  } else {
    // Actions (and anything else that happens to specify an output) just use
    // the output list with no substitution.
    SubstitutionWriter::GetListAsSourceFiles(outputs_, result);
  }
}
