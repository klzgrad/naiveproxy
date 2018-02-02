// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_QT_CREATOR_WRITER_H_
#define TOOLS_GN_QT_CREATOR_WRITER_H_

#include <set>
#include <string>

#include "base/files/file_path.h"
#include "base/macros.h"
#include "tools/gn/err.h"
#include "tools/gn/target.h"

class Builder;
class BuildSettings;

class QtCreatorWriter {
 public:
  static bool RunAndWriteFile(const BuildSettings* build_settings,
                              const Builder& builder,
                              Err* err,
                              const std::string& root_target);

 private:
  QtCreatorWriter(const BuildSettings* build_settings,
                  const Builder& builder,
                  const base::FilePath& project_prefix,
                  const std::string& root_target_name);
  ~QtCreatorWriter();

  void Run();

  bool DiscoverTargets();
  void HandleTarget(const Target* target);

  void CollectDeps(const Target* target);
  void AddToSources(const Target::FileList& files);
  void GenerateFile(const base::FilePath::CharType* suffix,
                    const std::set<std::string>& items);

  const BuildSettings* build_settings_;
  const Builder& builder_;
  base::FilePath project_prefix_;
  std::string root_target_name_;
  std::set<const Target*> targets_;
  std::set<std::string> sources_;
  std::set<std::string> includes_;
  std::set<std::string> defines_;
  Err err_;

  DISALLOW_COPY_AND_ASSIGN(QtCreatorWriter);
};

#endif  // TOOLS_GN_QT_CREATOR_WRITER_H_
