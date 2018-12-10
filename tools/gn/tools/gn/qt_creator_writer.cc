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
#include "base/optional.h"

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
}  // namespace

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
                              FilePathToUTF8(project_dir) +
                              "': " + base::File::ErrorToString(error));
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

QtCreatorWriter::~QtCreatorWriter() = default;

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

namespace QtCreatorWriterUtils {

enum class CVersion {
  C99,
  C11,
};

enum class CxxVersion {
  CXX98,
  CXX03,
  CXX11,
  CXX14,
  CXX17,
};

std::string ToMacro(CVersion version) {
  const std::string s = "__STDC_VERSION__";

  switch(version) {
    case CVersion::C99:
      return s + " 199901L";
    case CVersion::C11:
      return s + " 201112L";
    }

  return std::string();
}

std::string ToMacro(CxxVersion version) {
  const std::string name = "__cplusplus";

  switch(version) {
    case CxxVersion::CXX98:
    case CxxVersion::CXX03:
      return name + " 199711L";
    case CxxVersion::CXX11:
      return name + " 201103L";
    case CxxVersion::CXX14:
      return name + " 201402L";
    case CxxVersion::CXX17:
      return name + " 201703L";
    }

  return std::string();
}

const std::map<std::string, CVersion> kFlagToCVersion{
  {"-std=gnu99" , CVersion::C99},
  {"-std=c99"   , CVersion::C99},
  {"-std=gnu11" , CVersion::C11},
  {"-std=c11"   , CVersion::C11}
};

const std::map<std::string, CxxVersion> kFlagToCxxVersion{
  {"-std=gnu++11", CxxVersion::CXX11}, {"-std=c++11", CxxVersion::CXX11},
  {"-std=gnu++98", CxxVersion::CXX98}, {"-std=c++98", CxxVersion::CXX98},
  {"-std=gnu++03", CxxVersion::CXX03}, {"-std=c++03", CxxVersion::CXX03},
  {"-std=gnu++14", CxxVersion::CXX14}, {"-std=c++14", CxxVersion::CXX14},
  {"-std=c++1y"  , CxxVersion::CXX14},
  {"-std=gnu++17", CxxVersion::CXX17}, {"-std=c++17", CxxVersion::CXX17},
  {"-std=c++1z"  , CxxVersion::CXX17},
};

template<typename Enum>
struct CompVersion {
  bool operator()(Enum a, Enum b) {
    return static_cast<int>(a) < static_cast<int>(b);
  }
};

struct CompilerOptions {
  base::Optional<CVersion> c_version_;
  base::Optional<CxxVersion> cxx_version_;

  void SetCVersion(CVersion ver) {
    SetVersionImpl(c_version_, ver);
  }

  void SetCxxVersion(CxxVersion ver) {
    SetVersionImpl(cxx_version_, ver);
  }

private:
  template<typename Version>
  void SetVersionImpl(base::Optional<Version> &cur_ver, Version ver) {
    if (cur_ver)
      cur_ver = std::max(*cur_ver, ver, CompVersion<Version> {});
    else
      cur_ver = ver;
  }
};

void ParseCompilerOption(const std::string& flag, CompilerOptions* options) {
  auto c_ver = kFlagToCVersion.find(flag);
  if (c_ver != kFlagToCVersion.end())
    options->SetCVersion(c_ver->second);

  auto cxx_ver = kFlagToCxxVersion.find(flag);
  if (cxx_ver != kFlagToCxxVersion.end())
    options->SetCxxVersion(cxx_ver->second);
}

void ParseCompilerOptions(const std::vector<std::string>& cflags,
                          CompilerOptions* options) {
  for (const std::string& flag : cflags)
    ParseCompilerOption(flag, options);
}

} // QtCreatorWriterUtils

void QtCreatorWriter::HandleTarget(const Target* target) {
  using namespace QtCreatorWriterUtils;

  SourceFile build_file = Loader::BuildFileForLabel(target->label());
  sources_.insert(FilePathToUTF8(build_settings_->GetFullPath(build_file)));
  AddToSources(target->settings()->import_manager().GetImportedFiles());

  AddToSources(target->sources());
  AddToSources(target->public_headers());

  for (ConfigValuesIterator it(target); !it.done(); it.Next()) {
    for (const auto& input : it.cur().inputs())
      sources_.insert(FilePathToUTF8(build_settings_->GetFullPath(input)));

    SourceFile precompiled_source = it.cur().precompiled_source();
    if (!precompiled_source.is_null()) {
      sources_.insert(
          FilePathToUTF8(build_settings_->GetFullPath(precompiled_source)));
    }

    for (const SourceDir& include_dir : it.cur().include_dirs()) {
      includes_.insert(
          FilePathToUTF8(build_settings_->GetFullPath(include_dir)));
    }

    static constexpr const char *define_str = "#define ";
    for (std::string define : it.cur().defines()) {
      size_t equal_pos = define.find('=');
      if (equal_pos != std::string::npos)
        define[equal_pos] = ' ';
      define.insert(0, define_str);
      defines_.insert(define);
    }

    CompilerOptions options;
    ParseCompilerOptions(it.cur().cflags(), &options);
    ParseCompilerOptions(it.cur().cflags_c(), &options);
    ParseCompilerOptions(it.cur().cflags_cc(), &options);

    auto add_define_version = [this] (auto &ver) {
      if (ver)
        defines_.insert(define_str + ToMacro(*ver));
    };
    add_define_version(options.c_version_);
    add_define_version(options.cxx_version_);
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
