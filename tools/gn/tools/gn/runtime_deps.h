// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_RUNTIME_DEPS_H
#define TOOLS_GN_RUNTIME_DEPS_H

#include <utility>
#include <vector>

class Builder;
class Err;
class OutputFile;
class Target;

extern const char kRuntimeDeps_Help[];

// Computes the runtime dependencies of the given target. The result is a list
// of pairs listing the runtime dependency and the target that the runtime
// dependency is from (for blaming).
std::vector<std::pair<OutputFile, const Target*>> ComputeRuntimeDeps(
    const Target* target);

// Writes all runtime deps files requested on the command line, or does nothing
// if no files were specified.
bool WriteRuntimeDepsFilesIfNecessary(const Builder& builder, Err* err);

#endif  // TOOLS_GN_RUNTIME_DEPS_H
