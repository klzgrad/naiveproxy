// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_VISUAL_STUDIO_WRITER_H_
#define TOOLS_GN_VISUAL_STUDIO_WRITER_H_

#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "tools/gn/path_output.h"

namespace base {
class FilePath;
}

class Builder;
class BuildSettings;
class Err;
class SourceFile;
class Target;

class VisualStudioWriter {
 public:
  enum Version {
    Vs2013 = 1,  // Visual Studio 2013
    Vs2015,      // Visual Studio 2015
    Vs2017       // Visual Studio 2017
  };

  // Writes Visual Studio project and solution files. |sln_name| is the optional
  // solution file name ("all" is used if not specified). |filters| is optional
  // semicolon-separated list of label patterns used to limit the set of
  // generated projects. Only matching targets and their dependencies (unless
  // |no_deps| is true) will be included to the solution. On failure will
  // populate |err| and will return false. |win_sdk| is the Windows SDK version
  // which will be used by Visual Studio IntelliSense.
  static bool RunAndWriteFiles(const BuildSettings* build_settings,
                               const Builder& builder,
                               Version version,
                               const std::string& sln_name,
                               const std::string& filters,
                               const std::string& win_sdk,
                               bool no_deps,
                               Err* err);

 private:
  FRIEND_TEST_ALL_PREFIXES(VisualStudioWriterTest, ResolveSolutionFolders);
  FRIEND_TEST_ALL_PREFIXES(VisualStudioWriterTest,
                           ResolveSolutionFolders_AbsPath);

  // Solution project or folder.
  struct SolutionEntry {
    SolutionEntry(const std::string& name,
                  const std::string& path,
                  const std::string& guid);
    virtual ~SolutionEntry();

    // Entry name. For projects must be unique in the solution.
    std::string name;
    // Absolute project file or folder directory path.
    std::string path;
    // GUID-like string.
    std::string guid;
    // Pointer to parent folder. nullptr if entry has no parent.
    SolutionEntry* parent_folder;
  };

  struct SolutionProject : public SolutionEntry {
    SolutionProject(const std::string& name,
                    const std::string& path,
                    const std::string& guid,
                    const std::string& label_dir_path,
                    const std::string& config_platform);
    ~SolutionProject() override;

    // Absolute label dir path.
    std::string label_dir_path;
    // Configuration platform. May be different than solution config platform.
    std::string config_platform;
  };

  struct SourceFileCompileTypePair {
    SourceFileCompileTypePair(const SourceFile* file, const char* compile_type);
    ~SourceFileCompileTypePair();

    // Source file.
    const SourceFile* file;
    // Compile type string.
    const char* compile_type;
  };

  using SolutionProjects = std::vector<std::unique_ptr<SolutionProject>>;
  using SolutionFolders = std::vector<std::unique_ptr<SolutionEntry>>;
  using SourceFileCompileTypePairs = std::vector<SourceFileCompileTypePair>;

  VisualStudioWriter(const BuildSettings* build_settings,
                     const char* config_platform,
                     Version version,
                     const std::string& win_kit);
  ~VisualStudioWriter();

  bool WriteProjectFiles(const Target* target, Err* err);
  bool WriteProjectFileContents(std::ostream& out,
                                const SolutionProject& solution_project,
                                const Target* target,
                                SourceFileCompileTypePairs* source_types,
                                Err* err);
  void WriteFiltersFileContents(std::ostream& out,
                                const Target* target,
                                const SourceFileCompileTypePairs& source_types);
  bool WriteSolutionFile(const std::string& sln_name, Err* err);
  void WriteSolutionFileContents(std::ostream& out,
                                 const base::FilePath& solution_dir_path);

  // Resolves all solution folders (parent folders for projects) into |folders_|
  // and updates |root_folder_dir_|. Also sets |parent_folder| for |projects_|.
  void ResolveSolutionFolders();

  std::string GetNinjaTarget(const Target* target);

  const BuildSettings* build_settings_;

  // Toolset version.
  const char* toolset_version_;

  // Project version.
  const char* project_version_;

  // Visual Studio version string.
  const char* version_string_;

  // Platform for solution configuration (Win32, x64). Some projects may be
  // configured for different platform.
  const char* config_platform_;

  // All projects contained by solution.
  SolutionProjects projects_;

  // Absolute root solution folder path.
  std::string root_folder_path_;

  // Folders for all solution projects.
  SolutionFolders folders_;

  // Semicolon-separated Windows SDK include directories.
  std::string windows_kits_include_dirs_;

  // Path formatter for ninja targets.
  PathOutput ninja_path_output_;

  // Windows 10 SDK version string (e.g. 10.0.14393.0)
  std::string windows_sdk_version_;

  DISALLOW_COPY_AND_ASSIGN(VisualStudioWriter);
};

#endif  // TOOLS_GN_VISUAL_STUDIO_WRITER_H_
