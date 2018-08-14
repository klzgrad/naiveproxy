// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_NINJA_UTILS_H_
#define TOOLS_GN_NINJA_UTILS_H_

#include <string>

class Settings;
class SourceFile;
class Target;

// Example: "base/base.ninja". The string version will not be escaped, and
// will always have slashes for path separators.
SourceFile GetNinjaFileForTarget(const Target* target);

// Returns the name of the root .ninja file for the given toolchain.
SourceFile GetNinjaFileForToolchain(const Settings* settings);

// Returns the prefix applied to the Ninja rules in a given toolchain so they
// don't collide with rules from other toolchains.
std::string GetNinjaRulePrefixForToolchain(const Settings* settings);

#endif  // TOOLS_GN_NINJA_UTILS_H_
