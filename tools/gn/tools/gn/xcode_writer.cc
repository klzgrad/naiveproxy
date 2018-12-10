// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/xcode_writer.h"

#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

#include "base/environment.h"
#include "base/logging.h"
#include "base/sha1.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "tools/gn/args.h"
#include "tools/gn/build_settings.h"
#include "tools/gn/builder.h"
#include "tools/gn/commands.h"
#include "tools/gn/deps_iterator.h"
#include "tools/gn/filesystem_utils.h"
#include "tools/gn/settings.h"
#include "tools/gn/source_file.h"
#include "tools/gn/target.h"
#include "tools/gn/value.h"
#include "tools/gn/variables.h"
#include "tools/gn/xcode_object.h"

namespace {

using TargetToFileList = std::unordered_map<const Target*, Target::FileList>;
using TargetToTarget = std::unordered_map<const Target*, const Target*>;
using TargetToPBXTarget = std::unordered_map<const Target*, PBXTarget*>;

const char* kXCTestFileSuffixes[] = {
    "egtest.m",
    "egtest.mm",
    "xctest.m",
    "xctest.mm",
};

const char kXCTestModuleTargetNamePostfix[] = "_module";
const char kXCUITestRunnerTargetNamePostfix[] = "_runner";

struct SafeEnvironmentVariableInfo {
  const char* name;
  bool capture_at_generation;
};

SafeEnvironmentVariableInfo kSafeEnvironmentVariables[] = {
    {"HOME", true},
    {"LANG", true},
    {"PATH", true},
    {"USER", true},
    {"TMPDIR", false},
    {"ICECC_VERSION", true},
    {"ICECC_CLANG_REMOTE_CPP", true}};

XcodeWriter::TargetOsType GetTargetOs(const Args& args) {
  const Value* target_os_value = args.GetArgOverride(variables::kTargetOs);
  if (target_os_value) {
    if (target_os_value->type() == Value::STRING) {
      if (target_os_value->string_value() == "ios")
        return XcodeWriter::WRITER_TARGET_OS_IOS;
    }
  }
  return XcodeWriter::WRITER_TARGET_OS_MACOS;
}

std::string GetBuildScript(const std::string& target_name,
                           const std::string& ninja_extra_args,
                           base::Environment* environment) {
  std::stringstream script;
  script << "echo note: \"Compile and copy " << target_name << " via ninja\"\n"
         << "exec ";

  // Launch ninja with a sanitized environment (Xcode sets many environment
  // variable overridding settings, including the SDK, thus breaking hermetic
  // build).
  script << "env -i ";
  for (const auto& variable : kSafeEnvironmentVariables) {
    script << variable.name << "=\"";

    std::string value;
    if (variable.capture_at_generation)
      environment->GetVar(variable.name, &value);

    if (!value.empty())
      script << value;
    else
      script << "$" << variable.name;
    script << "\" ";
  }

  script << "ninja -C .";
  if (!ninja_extra_args.empty())
    script << " " << ninja_extra_args;
  if (!target_name.empty())
    script << " " << target_name;
  script << "\nexit 1\n";
  return script.str();
}

bool IsApplicationTarget(const Target* target) {
  return target->output_type() == Target::CREATE_BUNDLE &&
         target->bundle_data().product_type() ==
             "com.apple.product-type.application";
}

bool IsXCUITestRunnerTarget(const Target* target) {
  return IsApplicationTarget(target) &&
         base::EndsWith(target->label().name(),
                        kXCUITestRunnerTargetNamePostfix,
                        base::CompareCase::SENSITIVE);
}

bool IsXCTestModuleTarget(const Target* target) {
  return target->output_type() == Target::CREATE_BUNDLE &&
         target->bundle_data().product_type() ==
             "com.apple.product-type.bundle.unit-test" &&
         base::EndsWith(target->label().name(), kXCTestModuleTargetNamePostfix,
                        base::CompareCase::SENSITIVE);
}

bool IsXCUITestModuleTarget(const Target* target) {
  return target->output_type() == Target::CREATE_BUNDLE &&
         target->bundle_data().product_type() ==
             "com.apple.product-type.bundle.ui-testing" &&
         base::EndsWith(target->label().name(), kXCTestModuleTargetNamePostfix,
                        base::CompareCase::SENSITIVE);
}

bool IsXCTestFile(const SourceFile& file) {
  std::string file_name = file.GetName();
  for (size_t i = 0; i < arraysize(kXCTestFileSuffixes); ++i) {
    if (base::EndsWith(file_name, kXCTestFileSuffixes[i],
                       base::CompareCase::SENSITIVE)) {
      return true;
    }
  }

  return false;
}

const Target* FindApplicationTargetByName(
    const std::string& target_name,
    const std::vector<const Target*>& targets) {
  for (const Target* target : targets) {
    if (target->label().name() == target_name) {
      DCHECK(IsApplicationTarget(target));
      return target;
    }
  }
  NOTREACHED();
  return nullptr;
}

// Adds |base_pbxtarget| as a dependency of |dependent_pbxtarget| in the
// generated Xcode project.
void AddPBXTargetDependency(const PBXTarget* base_pbxtarget,
                            PBXTarget* dependent_pbxtarget,
                            const PBXProject* project) {
  auto container_item_proxy =
      std::make_unique<PBXContainerItemProxy>(project, base_pbxtarget);
  auto dependency = std::make_unique<PBXTargetDependency>(
      base_pbxtarget, std::move(container_item_proxy));

  dependent_pbxtarget->AddDependency(std::move(dependency));
}

// Adds the corresponding test application target as dependency of xctest or
// xcuitest module target in the generated Xcode project.
void AddDependencyTargetForTestModuleTargets(
    const std::vector<const Target*>& targets,
    const TargetToPBXTarget& bundle_target_to_pbxtarget,
    const PBXProject* project) {
  for (const Target* target : targets) {
    if (!IsXCTestModuleTarget(target) && !IsXCUITestModuleTarget(target))
      continue;

    const Target* test_application_target = FindApplicationTargetByName(
        target->bundle_data().xcode_test_application_name(), targets);
    const PBXTarget* test_application_pbxtarget =
        bundle_target_to_pbxtarget.at(test_application_target);
    PBXTarget* module_pbxtarget = bundle_target_to_pbxtarget.at(target);
    DCHECK(test_application_pbxtarget);
    DCHECK(module_pbxtarget);

    AddPBXTargetDependency(test_application_pbxtarget, module_pbxtarget,
                           project);
  }
}

// Searches the list of xctest files recursively under |target|.
void SearchXCTestFilesForTarget(const Target* target,
                                TargetToFileList* xctest_files_per_target) {
  // Early return if already visited and processed.
  if (xctest_files_per_target->find(target) != xctest_files_per_target->end())
    return;

  Target::FileList xctest_files;
  for (const SourceFile& file : target->sources()) {
    if (IsXCTestFile(file)) {
      xctest_files.push_back(file);
    }
  }

  // Call recursively on public and private deps.
  for (const auto& t : target->public_deps()) {
    SearchXCTestFilesForTarget(t.ptr, xctest_files_per_target);
    const Target::FileList& deps_xctest_files =
        (*xctest_files_per_target)[t.ptr];
    xctest_files.insert(xctest_files.end(), deps_xctest_files.begin(),
                        deps_xctest_files.end());
  }

  for (const auto& t : target->private_deps()) {
    SearchXCTestFilesForTarget(t.ptr, xctest_files_per_target);
    const Target::FileList& deps_xctest_files =
        (*xctest_files_per_target)[t.ptr];
    xctest_files.insert(xctest_files.end(), deps_xctest_files.begin(),
                        deps_xctest_files.end());
  }

  // Sort xctest_files to remove duplicates.
  std::sort(xctest_files.begin(), xctest_files.end());
  xctest_files.erase(std::unique(xctest_files.begin(), xctest_files.end()),
                     xctest_files.end());

  xctest_files_per_target->insert(std::make_pair(target, xctest_files));
}

// Add all source files for indexing, both private and public.
void AddSourceFilesToProjectForIndexing(
    const std::vector<const Target*>& targets,
    PBXProject* project,
    SourceDir source_dir,
    const BuildSettings* build_settings) {
  std::vector<SourceFile> sources;
  for (const Target* target : targets) {
    for (const SourceFile& source : target->sources()) {
      if (IsStringInOutputDir(build_settings->build_dir(), source.value()))
        continue;

      sources.push_back(source);
    }

    if (target->all_headers_public())
      continue;

    for (const SourceFile& source : target->public_headers()) {
      if (IsStringInOutputDir(build_settings->build_dir(), source.value()))
        continue;

      sources.push_back(source);
    }
  }

  // Sort sources to ensure determinism of the project file generation and
  // remove duplicate reference to the source files (can happen due to the
  // bundle_data targets).
  std::sort(sources.begin(), sources.end());
  sources.erase(std::unique(sources.begin(), sources.end()), sources.end());

  for (const SourceFile& source : sources) {
    std::string source_file = RebasePath(source.value(), source_dir,
                                         build_settings->root_path_utf8());
    project->AddSourceFileToIndexingTarget(source_file, source_file,
                                           CompilerFlags::NONE);
  }
}

// Add xctest files to the "Compiler Sources" of corresponding test module
// native targets.
void AddXCTestFilesToTestModuleTarget(const Target::FileList& xctest_file_list,
                                      PBXNativeTarget* native_target,
                                      PBXProject* project,
                                      SourceDir source_dir,
                                      const BuildSettings* build_settings) {
  for (const SourceFile& source : xctest_file_list) {
    std::string source_path = RebasePath(source.value(), source_dir,
                                         build_settings->root_path_utf8());

    // Test files need to be known to Xcode for proper indexing and for
    // discovery of tests function for XCTest and XCUITest, but the compilation
    // is done via ninja and thus must prevent Xcode from compiling the files by
    // adding '-help' as per file compiler flag.
    project->AddSourceFile(source_path, source_path, CompilerFlags::HELP,
                           native_target);
  }
}

class CollectPBXObjectsPerClassHelper : public PBXObjectVisitor {
 public:
  CollectPBXObjectsPerClassHelper() = default;

  void Visit(PBXObject* object) override {
    DCHECK(object);
    objects_per_class_[object->Class()].push_back(object);
  }

  const std::map<PBXObjectClass, std::vector<const PBXObject*>>&
  objects_per_class() const {
    return objects_per_class_;
  }

 private:
  std::map<PBXObjectClass, std::vector<const PBXObject*>> objects_per_class_;

  DISALLOW_COPY_AND_ASSIGN(CollectPBXObjectsPerClassHelper);
};

std::map<PBXObjectClass, std::vector<const PBXObject*>>
CollectPBXObjectsPerClass(PBXProject* project) {
  CollectPBXObjectsPerClassHelper visitor;
  project->Visit(visitor);
  return visitor.objects_per_class();
}

class RecursivelyAssignIdsHelper : public PBXObjectVisitor {
 public:
  RecursivelyAssignIdsHelper(const std::string& seed)
      : seed_(seed), counter_(0) {}

  void Visit(PBXObject* object) override {
    std::stringstream buffer;
    buffer << seed_ << " " << object->Name() << " " << counter_;
    std::string hash = base::SHA1HashString(buffer.str());
    DCHECK_EQ(hash.size() % 4, 0u);

    uint32_t id[3] = {0, 0, 0};
    const uint32_t* ptr = reinterpret_cast<const uint32_t*>(hash.data());
    for (size_t i = 0; i < hash.size() / 4; i++)
      id[i % 3] ^= ptr[i];

    object->SetId(base::HexEncode(id, sizeof(id)));
    ++counter_;
  }

 private:
  std::string seed_;
  int64_t counter_;

  DISALLOW_COPY_AND_ASSIGN(RecursivelyAssignIdsHelper);
};

void RecursivelyAssignIds(PBXProject* project) {
  RecursivelyAssignIdsHelper visitor(project->Name());
  project->Visit(visitor);
}

}  // namespace

// static
bool XcodeWriter::RunAndWriteFiles(const std::string& workspace_name,
                                   const std::string& root_target_name,
                                   const std::string& ninja_extra_args,
                                   const std::string& dir_filters_string,
                                   const BuildSettings* build_settings,
                                   const Builder& builder,
                                   Err* err) {
  const XcodeWriter::TargetOsType target_os =
      GetTargetOs(build_settings->build_args());

  PBXAttributes attributes;
  switch (target_os) {
    case XcodeWriter::WRITER_TARGET_OS_IOS:
      attributes["SDKROOT"] = "iphoneos";
      attributes["TARGETED_DEVICE_FAMILY"] = "1,2";
      break;
    case XcodeWriter::WRITER_TARGET_OS_MACOS:
      attributes["SDKROOT"] = "macosx";
      break;
  }

  const std::string source_path = FilePathToUTF8(
      UTF8ToFilePath(RebasePath("//", build_settings->build_dir()))
          .StripTrailingSeparators());

  std::string config_name = FilePathToUTF8(build_settings->build_dir()
                                               .Resolve(base::FilePath())
                                               .StripTrailingSeparators()
                                               .BaseName());
  DCHECK(!config_name.empty());

  std::string::size_type separator = config_name.find('-');
  if (separator != std::string::npos)
    config_name = config_name.substr(0, separator);

  std::vector<const Target*> targets;
  std::vector<const Target*> all_targets = builder.GetAllResolvedTargets();
  if (!XcodeWriter::FilterTargets(build_settings, all_targets,
                                  dir_filters_string, &targets, err)) {
    return false;
  }

  XcodeWriter workspace(workspace_name);
  workspace.CreateProductsProject(targets, all_targets, attributes, source_path,
                                  config_name, root_target_name,
                                  ninja_extra_args, build_settings, target_os);

  return workspace.WriteFiles(build_settings, err);
}

XcodeWriter::XcodeWriter(const std::string& name) : name_(name) {
  if (name_.empty())
    name_.assign("all");
}

XcodeWriter::~XcodeWriter() = default;

// static
bool XcodeWriter::FilterTargets(const BuildSettings* build_settings,
                                const std::vector<const Target*>& all_targets,
                                const std::string& dir_filters_string,
                                std::vector<const Target*>* targets,
                                Err* err) {
  // Filter targets according to the semicolon-delimited list of label patterns,
  // if defined, first.
  targets->reserve(all_targets.size());
  if (dir_filters_string.empty()) {
    *targets = all_targets;
  } else {
    std::vector<LabelPattern> filters;
    if (!commands::FilterPatternsFromString(build_settings, dir_filters_string,
                                            &filters, err)) {
      return false;
    }

    commands::FilterTargetsByPatterns(all_targets, filters, targets);
  }

  // Filter out all target of type EXECUTABLE that are direct dependency of
  // a BUNDLE_DATA target (under the assumption that they will be part of a
  // CREATE_BUNDLE target generating an application bundle). Sort the list
  // of targets per pointer to use binary search for the removal.
  std::sort(targets->begin(), targets->end());

  for (const Target* target : all_targets) {
    if (!target->settings()->is_default())
      continue;

    if (target->output_type() != Target::BUNDLE_DATA)
      continue;

    for (const auto& pair : target->GetDeps(Target::DEPS_LINKED)) {
      if (pair.ptr->output_type() != Target::EXECUTABLE)
        continue;

      auto iter = std::lower_bound(targets->begin(), targets->end(), pair.ptr);
      if (iter != targets->end() && *iter == pair.ptr)
        targets->erase(iter);
    }
  }

  // Sort the list of targets per-label to get a consistent ordering of them
  // in the generated Xcode project (and thus stability of the file generated).
  std::sort(targets->begin(), targets->end(),
            [](const Target* a, const Target* b) {
              return a->label().name() < b->label().name();
            });

  return true;
}

void XcodeWriter::CreateProductsProject(
    const std::vector<const Target*>& targets,
    const std::vector<const Target*>& all_targets,
    const PBXAttributes& attributes,
    const std::string& source_path,
    const std::string& config_name,
    const std::string& root_target,
    const std::string& ninja_extra_args,
    const BuildSettings* build_settings,
    TargetOsType target_os) {
  std::unique_ptr<PBXProject> main_project(
      new PBXProject("products", config_name, source_path, attributes));

  std::vector<const Target*> bundle_targets;
  TargetToPBXTarget bundle_target_to_pbxtarget;

  std::string build_path;
  std::unique_ptr<base::Environment> env(base::Environment::Create());
  SourceDir source_dir("//");
  AddSourceFilesToProjectForIndexing(all_targets, main_project.get(),
                                     source_dir, build_settings);
  main_project->AddAggregateTarget(
      "All", GetBuildScript(root_target, ninja_extra_args, env.get()));

  // Needs to search for xctest files under the application targets, and this
  // variable is used to store the results of visited targets, thus making the
  // search more efficient.
  TargetToFileList xctest_files_per_target;

  for (const Target* target : targets) {
    switch (target->output_type()) {
      case Target::EXECUTABLE:
        if (target_os == XcodeWriter::WRITER_TARGET_OS_IOS)
          continue;

        main_project->AddNativeTarget(
            target->label().name(), "compiled.mach-o.executable",
            target->output_name().empty() ? target->label().name()
                                          : target->output_name(),
            "com.apple.product-type.tool",
            GetBuildScript(target->label().name(), ninja_extra_args,
                           env.get()));
        break;

      case Target::CREATE_BUNDLE: {
        if (target->bundle_data().product_type().empty())
          continue;

        // For XCUITest, two CREATE_BUNDLE targets are generated:
        // ${target_name}_runner and ${target_name}_module, however, Xcode
        // requires only one target named ${target_name} to run tests.
        if (IsXCUITestRunnerTarget(target))
          continue;
        std::string pbxtarget_name = target->label().name();
        if (IsXCUITestModuleTarget(target)) {
          std::string target_name = target->label().name();
          pbxtarget_name = target_name.substr(
              0, target_name.rfind(kXCTestModuleTargetNamePostfix));
        }

        PBXAttributes xcode_extra_attributes =
            target->bundle_data().xcode_extra_attributes();

        const std::string& target_output_name =
            RebasePath(target->bundle_data()
                           .GetBundleRootDirOutput(target->settings())
                           .value(),
                       build_settings->build_dir());
        PBXNativeTarget* native_target = main_project->AddNativeTarget(
            pbxtarget_name, std::string(), target_output_name,
            target->bundle_data().product_type(),
            GetBuildScript(pbxtarget_name, ninja_extra_args, env.get()),
            xcode_extra_attributes);

        bundle_targets.push_back(target);
        bundle_target_to_pbxtarget.insert(
            std::make_pair(target, native_target));

        if (!IsXCTestModuleTarget(target) && !IsXCUITestModuleTarget(target))
          continue;

        // For XCTest, test files are compiled into the application bundle.
        // For XCUITest, test files are compiled into the test module bundle.
        const Target* target_with_xctest_files = nullptr;
        if (IsXCTestModuleTarget(target)) {
          target_with_xctest_files = FindApplicationTargetByName(
              target->bundle_data().xcode_test_application_name(), targets);
        } else if (IsXCUITestModuleTarget(target)) {
          target_with_xctest_files = target;
        } else {
          NOTREACHED();
        }

        SearchXCTestFilesForTarget(target_with_xctest_files,
                                   &xctest_files_per_target);
        const Target::FileList& xctest_file_list =
            xctest_files_per_target[target_with_xctest_files];

        // Add xctest files to the "Compiler Sources" of corresponding xctest
        // and xcuitest native targets for proper indexing and for discovery of
        // tests function.
        AddXCTestFilesToTestModuleTarget(xctest_file_list, native_target,
                                         main_project.get(), source_dir,
                                         build_settings);
        break;
      }

      default:
        break;
    }
  }

  // Adding the corresponding test application target as a dependency of xctest
  // or xcuitest module target in the generated Xcode project so that the
  // application target is re-compiled when compiling the test module target.
  AddDependencyTargetForTestModuleTargets(
      bundle_targets, bundle_target_to_pbxtarget, main_project.get());

  projects_.push_back(std::move(main_project));
}

bool XcodeWriter::WriteFiles(const BuildSettings* build_settings, Err* err) {
  for (const auto& project : projects_) {
    if (!WriteProjectFile(build_settings, project.get(), err))
      return false;
  }

  SourceFile xcworkspacedata_file =
      build_settings->build_dir().ResolveRelativeFile(
          Value(nullptr, name_ + ".xcworkspace/contents.xcworkspacedata"), err);
  if (xcworkspacedata_file.is_null())
    return false;

  std::stringstream xcworkspacedata_string_out;
  WriteWorkspaceContent(xcworkspacedata_string_out);

  return WriteFileIfChanged(build_settings->GetFullPath(xcworkspacedata_file),
                            xcworkspacedata_string_out.str(), err);
}

bool XcodeWriter::WriteProjectFile(const BuildSettings* build_settings,
                                   PBXProject* project,
                                   Err* err) {
  SourceFile pbxproj_file = build_settings->build_dir().ResolveRelativeFile(
      Value(nullptr, project->Name() + ".xcodeproj/project.pbxproj"), err);
  if (pbxproj_file.is_null())
    return false;

  std::stringstream pbxproj_string_out;
  WriteProjectContent(pbxproj_string_out, project);

  if (!WriteFileIfChanged(build_settings->GetFullPath(pbxproj_file),
                          pbxproj_string_out.str(), err))
    return false;

  return true;
}

void XcodeWriter::WriteWorkspaceContent(std::ostream& out) {
  out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      << "<Workspace version = \"1.0\">\n";
  for (const auto& project : projects_) {
    out << "  <FileRef location = \"group:" << project->Name()
        << ".xcodeproj\"></FileRef>\n";
  }
  out << "</Workspace>\n";
}

void XcodeWriter::WriteProjectContent(std::ostream& out, PBXProject* project) {
  RecursivelyAssignIds(project);

  out << "// !$*UTF8*$!\n"
      << "{\n"
      << "\tarchiveVersion = 1;\n"
      << "\tclasses = {\n"
      << "\t};\n"
      << "\tobjectVersion = 46;\n"
      << "\tobjects = {\n";

  for (auto& pair : CollectPBXObjectsPerClass(project)) {
    out << "\n"
        << "/* Begin " << ToString(pair.first) << " section */\n";
    std::sort(pair.second.begin(), pair.second.end(),
              [](const PBXObject* a, const PBXObject* b) {
                return a->id() < b->id();
              });
    for (auto* object : pair.second) {
      object->Print(out, 2);
    }
    out << "/* End " << ToString(pair.first) << " section */\n";
  }

  out << "\t};\n"
      << "\trootObject = " << project->Reference() << ";\n"
      << "}\n";
}
