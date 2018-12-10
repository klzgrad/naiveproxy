// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_VALUE_EXTRACTORS_H_
#define TOOLS_GN_VALUE_EXTRACTORS_H_

#include <string>
#include <vector>

#include "tools/gn/label_ptr.h"
#include "tools/gn/lib_file.h"
#include "tools/gn/unique_vector.h"

class BuildSettings;
class Err;
class Label;
class LabelPattern;
class SourceDir;
class SourceFile;
class Value;

// On failure, returns false and sets the error.
bool ExtractListOfStringValues(const Value& value,
                               std::vector<std::string>* dest,
                               Err* err);

// Looks for a list of source files relative to a given current dir.
bool ExtractListOfRelativeFiles(const BuildSettings* build_settings,
                                const Value& value,
                                const SourceDir& current_dir,
                                std::vector<SourceFile>* files,
                                Err* err);

// Extracts a list of libraries. When they contain a "/" they are treated as
// source paths and are otherwise treated as plain strings.
bool ExtractListOfLibs(const BuildSettings* build_settings,
                       const Value& value,
                       const SourceDir& current_dir,
                       std::vector<LibFile>* libs,
                       Err* err);

// Looks for a list of source directories relative to a given current dir.
bool ExtractListOfRelativeDirs(const BuildSettings* build_settings,
                               const Value& value,
                               const SourceDir& current_dir,
                               std::vector<SourceDir>* dest,
                               Err* err);

// Extracts the list of labels and their origins to the given vector. Only the
// labels are filled in, the ptr for each pair in the vector will be null.
bool ExtractListOfLabels(const Value& value,
                         const SourceDir& current_dir,
                         const Label& current_toolchain,
                         LabelTargetVector* dest,
                         Err* err);

// Extracts the list of labels and their origins to the given vector. For the
// version taking Label*Pair, only the labels are filled in, the ptr for each
// pair in the vector will be null. Sets an error and returns false if a label
// is maformed or there are duplicates.
bool ExtractListOfUniqueLabels(const Value& value,
                               const SourceDir& current_dir,
                               const Label& current_toolchain,
                               UniqueVector<Label>* dest,
                               Err* err);
bool ExtractListOfUniqueLabels(const Value& value,
                               const SourceDir& current_dir,
                               const Label& current_toolchain,
                               UniqueVector<LabelConfigPair>* dest,
                               Err* err);
bool ExtractListOfUniqueLabels(const Value& value,
                               const SourceDir& current_dir,
                               const Label& current_toolchain,
                               UniqueVector<LabelTargetPair>* dest,
                               Err* err);

bool ExtractRelativeFile(const BuildSettings* build_settings,
                         const Value& value,
                         const SourceDir& current_dir,
                         SourceFile* file,
                         Err* err);

bool ExtractListOfLabelPatterns(const Value& value,
                                const SourceDir& current_dir,
                                std::vector<LabelPattern>* patterns,
                                Err* err);

#endif  // TOOLS_GN_VALUE_EXTRACTORS_H_
