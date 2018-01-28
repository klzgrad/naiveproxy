// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/qt_creator_writer.h"

#include <set>
#include <sstream>
#include <string>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/strings/utf_string_conversions.h"

#include "tools/gn/builder.h"
#include "tools/gn/config_values_extractors.h"
#include "tools/gn/deps_iterator.h"
#include "tools/gn/filesystem_utils.h"
#include "tools/gn/label.h"
#include "tools/gn/loader.h"

namespace {
base::FilePath::CharType kProjectDirName[] =
    FILE_PATH_LITERAL("qtcreator_project");
base::FilePath::CharType kProjectName[] = FILE_PATH_LITERAL("all");
base::FilePath::CharType kMainProjectFileSuffix[] =
    FILE_PATH_LITERAL(".creator");
base::FilePath::CharType kSourcesFileSuffix[] = FILE_PATH_LITERAL(".files");
base::FilePath::CharType kIncludesFileSuffix[] = FILE_PATH_LITERAL(".includes");
base::FilePath::CharType kDefinesFileSuffix[] = FILE_PATH_LITERAL(".config");
}

// static
bool QtCreatorWriter::RunAndWriteFile(const BuildSettings* build_settings,
                                      const Builder& builder,
                                      Err* err,
                                      const std::string& root_target) {
  base::FilePath project_dir =
      build_settings->GetFullPath(build_settings->build_dir())
          .Append(kProjectDirName);
  if (!base::DirectoryExists(project_dir)) {
    base::File::Error error;
    if (!base::CreateDirectoryAndGetError(project_dir, &error)) {
      *err =
          Err(Location(), "Could not create the QtCreator project directory '" +
                              FilePathToUTF8(project_dir) + "': " +
                              base::File::ErrorToString(error));
      return false;
    }
  }

  base::FilePath project_prefix = project_dir.Append(kProjectName);
  QtCreatorWriter gen(build_settings, builder, project_prefix, root_target);
  gen.Run();
  if (gen.err_.has_error()) {
    *err = gen.err_;
    return false;
  }
  return true;
}

QtCreatorWriter::QtCreatorWriter(const BuildSettings* build_settings,
                                 const Builder& builder,
                                 const base::FilePath& project_prefix,
                                 const std::string& root_target_name)
    : build_settings_(build_settings),
      builder_(builder),
      project_prefix_(project_prefix),
      root_target_name_(root_target_name) {}

QtCreatorWriter::~QtCreatorWriter() {}

void QtCreatorWriter::CollectDeps(const Target* target) {
  for (const auto& dep : target->GetDeps(Target::DEPS_ALL)) {
    const Target* dep_target = dep.ptr;
    if (targets_.count(dep_target))
      continue;
    targets_.insert(dep_target);
    CollectDeps(dep_target);
  }
}

bool QtCreatorWriter::DiscoverTargets() {
  auto all_targets = builder_.GetAllResolvedTargets();

  if (root_target_name_.empty()) {
    targets_ = std::set<const Target*>(all_targets.begin(), all_targets.end());
    return true;
  }

  const Target* root_target = nullptr;
  for (const Target* target : all_targets) {
    if (target->label().name() == root_target_name_) {
      root_target = target;
      break;
    }
  }

  if (!root_target) {
    err_ = Err(Location(), "Target '" + root_target_name_ + "' not found.");
    return false;
  }

  targets_.insert(root_target);
  CollectDeps(root_target);
  return true;
}

void QtCreatorWriter::AddToSources(const Target::FileList& files) {
  for (const SourceFile& file : files) {
    const std::string& file_path =
        FilePathToUTF8(build_settings_->GetFullPath(file));
    sources_.insert(file_path);
  }
}

void QtCreatorWriter::HandleTarget(const Target* target) {
  SourceFile build_file = Loader::BuildFileForLabel(target->label());
  sources_.insert(FilePathToUTF8(build_settings_->GetFullPath(build_file)));
  AddToSources(target->settings()->import_manager().GetImportedFiles());

  AddToSources(target->sources());
  AddToSources(target->public_headers());
  AddToSources(target->inputs());

  for (ConfigValuesIterator it(target); !it.done(); it.Next()) {
    SourceFile precompiled_source = it.cur().precompiled_source();
    if (!precompiled_source.is_null()) {
      sources_.insert(
          FilePathToUTF8(build_settings_->GetFullPath(precompiled_source)));
    }

    for (const SourceDir& include_dir : it.cur().include_dirs()) {
      includes_.insert(
          FilePathToUTF8(build_settings_->GetFullPath(include_dir)));
    }

    for (std::string define : it.cur().defines()) {
      size_t equal_pos = define.find('=');
      if (equal_pos != std::string::npos)
        define[equal_pos] = ' ';
      define.insert(0, "#define ");
      defines_.insert(define);
    }
  }
}

void QtCreatorWriter::GenerateFile(const base::FilePath::CharType* suffix,
                                   const std::set<std::string>& items) {
  const base::FilePath file_path = project_prefix_.AddExtension(suffix);
  std::ostringstream output;
  for (const std::string& item : items)
    output << item << std::endl;
  WriteFileIfChanged(file_path, output.str(), &err_);
}

void QtCreatorWriter::Run() {
  if (!DiscoverTargets())
    return;

  for (const Target* target : targets_) {
    if (target->toolchain()->label() !=
        builder_.loader()->GetDefaultToolchain())
      continue;
    HandleTarget(target);
  }

  std::set<std::string> empty_list;

  GenerateFile(kMainProjectFileSuffix, empty_list);
  GenerateFile(kSourcesFileSuffix, sources_);
  GenerateFile(kIncludesFileSuffix, includes_);
  GenerateFile(kDefinesFileSuffix, defines_);
}
