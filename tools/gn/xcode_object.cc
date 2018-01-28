// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/xcode_object.h"

#include <iomanip>
#include <sstream>
#include <utility>

#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/strings/string_util.h"
#include "tools/gn/filesystem_utils.h"
#include "tools/gn/source_file.h"
#include "tools/gn/source_file_type.h"

// Helper methods -------------------------------------------------------------

namespace {
struct IndentRules {
  bool one_line;
  unsigned level;
};

std::vector<std::unique_ptr<PBXObject>> EmptyPBXObjectVector() {
  return std::vector<std::unique_ptr<PBXObject>>();
}

bool CharNeedEscaping(char c) {
  if (base::IsAsciiAlpha(c) || base::IsAsciiDigit(c))
    return false;
  if (c == '$' || c == '.' || c == '/' || c == '_')
    return false;
  return true;
}

bool StringNeedEscaping(const std::string& string) {
  if (string.empty())
    return true;
  if (string.find("___") != std::string::npos)
    return true;

  for (char c : string) {
    if (CharNeedEscaping(c))
      return true;
  }
  return false;
}

std::string EncodeString(const std::string& string) {
  if (!StringNeedEscaping(string))
    return string;

  std::stringstream buffer;
  buffer << '"';
  for (char c : string) {
    if (c <= 31) {
      switch (c) {
        case '\a':
          buffer << "\\a";
          break;
        case '\b':
          buffer << "\\b";
          break;
        case '\t':
          buffer << "\\t";
          break;
        case '\n':
        case '\r':
          buffer << "\\n";
          break;
        case '\v':
          buffer << "\\v";
          break;
        case '\f':
          buffer << "\\f";
          break;
        default:
          buffer << std::hex << std::setw(4) << std::left << "\\U"
                 << static_cast<unsigned>(c);
          break;
      }
    } else {
      if (c == '"' || c == '\\')
        buffer << '\\';
      buffer << c;
    }
  }
  buffer << '"';
  return buffer.str();
}

struct SourceTypeForExt {
  const char* ext;
  const char* source_type;
};

const SourceTypeForExt kSourceTypeForExt[] = {
    {"a", "archive.ar"},
    {"app", "wrapper.application"},
    {"appex", "wrapper.app-extension"},
    {"bdic", "file"},
    {"bundle", "wrapper.cfbundle"},
    {"c", "sourcecode.c.c"},
    {"cc", "sourcecode.cpp.cpp"},
    {"cpp", "sourcecode.cpp.cpp"},
    {"css", "text.css"},
    {"cxx", "sourcecode.cpp.cpp"},
    {"dart", "sourcecode"},
    {"dylib", "compiled.mach-o.dylib"},
    {"framework", "wrapper.framework"},
    {"h", "sourcecode.c.h"},
    {"hxx", "sourcecode.cpp.h"},
    {"icns", "image.icns"},
    {"java", "sourcecode.java"},
    {"js", "sourcecode.javascript"},
    {"kext", "wrapper.kext"},
    {"m", "sourcecode.c.objc"},
    {"mm", "sourcecode.cpp.objcpp"},
    {"nib", "wrapper.nib"},
    {"o", "compiled.mach-o.objfile"},
    {"pdf", "image.pdf"},
    {"pl", "text.script.perl"},
    {"plist", "text.plist.xml"},
    {"pm", "text.script.perl"},
    {"png", "image.png"},
    {"py", "text.script.python"},
    {"r", "sourcecode.rez"},
    {"rez", "sourcecode.rez"},
    {"s", "sourcecode.asm"},
    {"storyboard", "file.storyboard"},
    {"strings", "text.plist.strings"},
    {"swift", "sourcecode.swift"},
    {"ttf", "file"},
    {"xcassets", "folder.assetcatalog"},
    {"xcconfig", "text.xcconfig"},
    {"xcdatamodel", "wrapper.xcdatamodel"},
    {"xcdatamodeld", "wrapper.xcdatamodeld"},
    {"xib", "file.xib"},
    {"y", "sourcecode.yacc"},
};

const char* GetSourceType(const base::StringPiece& ext) {
  for (size_t i = 0; i < arraysize(kSourceTypeForExt); ++i) {
    if (kSourceTypeForExt[i].ext == ext)
      return kSourceTypeForExt[i].source_type;
  }

  return "text";
}

bool HasExplicitFileType(const base::StringPiece& ext) {
  return ext == "dart";
}

bool IsSourceFileForIndexing(const SourceFile& src) {
  const SourceFileType type = GetSourceFileType(src);
  return type == SOURCE_C || type == SOURCE_CPP || type == SOURCE_M ||
         type == SOURCE_MM;
}

void PrintValue(std::ostream& out, IndentRules rules, unsigned value) {
  out << value;
}

void PrintValue(std::ostream& out, IndentRules rules, const char* value) {
  out << EncodeString(value);
}

void PrintValue(std::ostream& out,
                IndentRules rules,
                const std::string& value) {
  out << EncodeString(value);
}

void PrintValue(std::ostream& out, IndentRules rules, const PBXObject* value) {
  out << value->Reference();
}

template <typename ObjectClass>
void PrintValue(std::ostream& out,
                IndentRules rules,
                const std::unique_ptr<ObjectClass>& value) {
  PrintValue(out, rules, value.get());
}

template <typename ValueType>
void PrintValue(std::ostream& out,
                IndentRules rules,
                const std::vector<ValueType>& values) {
  IndentRules sub_rule{rules.one_line, rules.level + 1};
  out << "(" << (rules.one_line ? " " : "\n");
  for (const auto& value : values) {
    if (!sub_rule.one_line)
      out << std::string(sub_rule.level, '\t');

    PrintValue(out, sub_rule, value);
    out << "," << (rules.one_line ? " " : "\n");
  }

  if (!rules.one_line && rules.level)
    out << std::string(rules.level, '\t');
  out << ")";
}

template <typename ValueType>
void PrintValue(std::ostream& out,
                IndentRules rules,
                const std::map<std::string, ValueType>& values) {
  IndentRules sub_rule{rules.one_line, rules.level + 1};
  out << "{" << (rules.one_line ? " " : "\n");
  for (const auto& pair : values) {
    if (!sub_rule.one_line)
      out << std::string(sub_rule.level, '\t');

    out << pair.first << " = ";
    PrintValue(out, sub_rule, pair.second);
    out << ";" << (rules.one_line ? " " : "\n");
  }

  if (!rules.one_line && rules.level)
    out << std::string(rules.level, '\t');
  out << "}";
}

template <typename ValueType>
void PrintProperty(std::ostream& out,
                   IndentRules rules,
                   const char* name,
                   ValueType&& value) {
  if (!rules.one_line && rules.level)
    out << std::string(rules.level, '\t');

  out << name << " = ";
  PrintValue(out, rules, std::forward<ValueType>(value));
  out << ";" << (rules.one_line ? " " : "\n");
}
}  // namespace

// PBXObjectClass -------------------------------------------------------------

const char* ToString(PBXObjectClass cls) {
  switch (cls) {
    case PBXAggregateTargetClass:
      return "PBXAggregateTarget";
    case PBXBuildFileClass:
      return "PBXBuildFile";
    case PBXContainerItemProxyClass:
      return "PBXContainerItemProxy";
    case PBXFileReferenceClass:
      return "PBXFileReference";
    case PBXFrameworksBuildPhaseClass:
      return "PBXFrameworksBuildPhase";
    case PBXGroupClass:
      return "PBXGroup";
    case PBXNativeTargetClass:
      return "PBXNativeTarget";
    case PBXProjectClass:
      return "PBXProject";
    case PBXShellScriptBuildPhaseClass:
      return "PBXShellScriptBuildPhase";
    case PBXSourcesBuildPhaseClass:
      return "PBXSourcesBuildPhase";
    case PBXTargetDependencyClass:
      return "PBXTargetDependency";
    case XCBuildConfigurationClass:
      return "XCBuildConfiguration";
    case XCConfigurationListClass:
      return "XCConfigurationList";
  }
  NOTREACHED();
  return nullptr;
}

// PBXObjectVisitor -----------------------------------------------------------

PBXObjectVisitor::PBXObjectVisitor() {}

PBXObjectVisitor::~PBXObjectVisitor() {}

// PBXObject ------------------------------------------------------------------

PBXObject::PBXObject() {}

PBXObject::~PBXObject() {}

void PBXObject::SetId(const std::string& id) {
  DCHECK(id_.empty());
  DCHECK(!id.empty());
  id_.assign(id);
}

std::string PBXObject::Reference() const {
  std::string comment = Comment();
  if (comment.empty())
    return id_;

  return id_ + " /* " + comment + " */";
}

std::string PBXObject::Comment() const {
  return Name();
}

void PBXObject::Visit(PBXObjectVisitor& visitor) {
  visitor.Visit(this);
}

// PBXBuildPhase --------------------------------------------------------------

PBXBuildPhase::PBXBuildPhase() {}

PBXBuildPhase::~PBXBuildPhase() {}

// PBXTarget ------------------------------------------------------------------

PBXTarget::PBXTarget(const std::string& name,
                     const std::string& shell_script,
                     const std::string& config_name,
                     const PBXAttributes& attributes)
    : configurations_(new XCConfigurationList(config_name, attributes, this)),
      name_(name) {
  if (!shell_script.empty()) {
    build_phases_.push_back(
        base::MakeUnique<PBXShellScriptBuildPhase>(name, shell_script));
  }
}

PBXTarget::~PBXTarget() {}

void PBXTarget::AddDependency(std::unique_ptr<PBXTargetDependency> dependency) {
  DCHECK(dependency);
  dependencies_.push_back(std::move(dependency));
}

std::string PBXTarget::Name() const {
  return name_;
}

void PBXTarget::Visit(PBXObjectVisitor& visitor) {
  PBXObject::Visit(visitor);
  configurations_->Visit(visitor);
  for (const auto& dependency : dependencies_)
    dependency->Visit(visitor);
  for (const auto& build_phase : build_phases_)
    build_phase->Visit(visitor);
}

// PBXAggregateTarget ---------------------------------------------------------

PBXAggregateTarget::PBXAggregateTarget(const std::string& name,
                                       const std::string& shell_script,
                                       const std::string& config_name,
                                       const PBXAttributes& attributes)
    : PBXTarget(name, shell_script, config_name, attributes) {}

PBXAggregateTarget::~PBXAggregateTarget() {}

PBXObjectClass PBXAggregateTarget::Class() const {
  return PBXAggregateTargetClass;
}

void PBXAggregateTarget::Print(std::ostream& out, unsigned indent) const {
  const std::string indent_str(indent, '\t');
  const IndentRules rules = {false, indent + 1};
  out << indent_str << Reference() << " = {\n";
  PrintProperty(out, rules, "isa", ToString(Class()));
  PrintProperty(out, rules, "buildConfigurationList", configurations_);
  PrintProperty(out, rules, "buildPhases", build_phases_);
  PrintProperty(out, rules, "dependencies", EmptyPBXObjectVector());
  PrintProperty(out, rules, "name", name_);
  PrintProperty(out, rules, "productName", name_);
  out << indent_str << "};\n";
}

// PBXBuildFile ---------------------------------------------------------------

PBXBuildFile::PBXBuildFile(const PBXFileReference* file_reference,
                           const PBXSourcesBuildPhase* build_phase,
                           const CompilerFlags compiler_flag)
    : file_reference_(file_reference),
      build_phase_(build_phase),
      compiler_flag_(compiler_flag) {
  DCHECK(file_reference_);
  DCHECK(build_phase_);
}

PBXBuildFile::~PBXBuildFile() {}

PBXObjectClass PBXBuildFile::Class() const {
  return PBXBuildFileClass;
}

std::string PBXBuildFile::Name() const {
  return file_reference_->Name() + " in " + build_phase_->Name();
}

void PBXBuildFile::Print(std::ostream& out, unsigned indent) const {
  const std::string indent_str(indent, '\t');
  const IndentRules rules = {true, 0};
  out << indent_str << Reference() << " = {";
  PrintProperty(out, rules, "isa", ToString(Class()));
  PrintProperty(out, rules, "fileRef", file_reference_);
  if (compiler_flag_ == CompilerFlags::HELP) {
    std::map<std::string, std::string> settings = {
        {"COMPILER_FLAGS", "--help"},
    };
    PrintProperty(out, rules, "settings", settings);
  }
  out << "};\n";
}

// PBXContainerItemProxy ------------------------------------------------------
PBXContainerItemProxy::PBXContainerItemProxy(const PBXProject* project,
                                             const PBXTarget* target)
    : project_(project), target_(target) {}

PBXContainerItemProxy::~PBXContainerItemProxy() {}

PBXObjectClass PBXContainerItemProxy::Class() const {
  return PBXContainerItemProxyClass;
}

void PBXContainerItemProxy::Visit(PBXObjectVisitor& visitor) {
  PBXObject::Visit(visitor);
}

std::string PBXContainerItemProxy::Name() const {
  return "PBXContainerItemProxy";
}

void PBXContainerItemProxy::Print(std::ostream& out, unsigned indent) const {
  const std::string indent_str(indent, '\t');
  const IndentRules rules = {true, 0};
  out << indent_str << Reference() << " = {";
  PrintProperty(out, rules, "isa", ToString(Class()));
  PrintProperty(out, rules, "containerPortal", project_);
  PrintProperty(out, rules, "proxyType", 1u);
  PrintProperty(out, rules, "remoteGlobalIDString", target_);
  PrintProperty(out, rules, "remoteInfo", target_->Name());
  out << indent_str << "};\n";
}

// PBXFileReference -----------------------------------------------------------

PBXFileReference::PBXFileReference(const std::string& name,
                                   const std::string& path,
                                   const std::string& type)
    : name_(name), path_(path), type_(type) {}

PBXFileReference::~PBXFileReference() {}

PBXObjectClass PBXFileReference::Class() const {
  return PBXFileReferenceClass;
}

std::string PBXFileReference::Name() const {
  return name_;
}

void PBXFileReference::Print(std::ostream& out, unsigned indent) const {
  const std::string indent_str(indent, '\t');
  const IndentRules rules = {true, 0};
  out << indent_str << Reference() << " = {";
  PrintProperty(out, rules, "isa", ToString(Class()));

  if (!type_.empty()) {
    PrintProperty(out, rules, "explicitFileType", type_);
    PrintProperty(out, rules, "includeInIndex", 0u);
  } else {
    base::StringPiece ext = FindExtension(&name_);
    if (HasExplicitFileType(ext))
      PrintProperty(out, rules, "explicitFileType", GetSourceType(ext));
    else
      PrintProperty(out, rules, "lastKnownFileType", GetSourceType(ext));
  }

  if (!name_.empty())
    PrintProperty(out, rules, "name", name_);

  DCHECK(!path_.empty());
  PrintProperty(out, rules, "path", path_);
  PrintProperty(out, rules, "sourceTree",
                type_.empty() ? "<group>" : "BUILT_PRODUCTS_DIR");
  out << "};\n";
}

// PBXFrameworksBuildPhase ----------------------------------------------------

PBXFrameworksBuildPhase::PBXFrameworksBuildPhase() {}

PBXFrameworksBuildPhase::~PBXFrameworksBuildPhase() {}

PBXObjectClass PBXFrameworksBuildPhase::Class() const {
  return PBXFrameworksBuildPhaseClass;
}

std::string PBXFrameworksBuildPhase::Name() const {
  return "Frameworks";
}

void PBXFrameworksBuildPhase::Print(std::ostream& out, unsigned indent) const {
  const std::string indent_str(indent, '\t');
  const IndentRules rules = {false, indent + 1};
  out << indent_str << Reference() << " = {\n";
  PrintProperty(out, rules, "isa", ToString(Class()));
  PrintProperty(out, rules, "buildActionMask", 0x7fffffffu);
  PrintProperty(out, rules, "files", EmptyPBXObjectVector());
  PrintProperty(out, rules, "runOnlyForDeploymentPostprocessing", 0u);
  out << indent_str << "};\n";
}

// PBXGroup -------------------------------------------------------------------

PBXGroup::PBXGroup(const std::string& path, const std::string& name)
    : name_(name), path_(path) {}

PBXGroup::~PBXGroup() {}

PBXObject* PBXGroup::AddChild(std::unique_ptr<PBXObject> child) {
  DCHECK(child);
  children_.push_back(std::move(child));
  return children_.back().get();
}

PBXFileReference* PBXGroup::AddSourceFile(const std::string& navigator_path,
                                          const std::string& source_path) {
  DCHECK(!navigator_path.empty());
  DCHECK(!source_path.empty());
  std::string::size_type sep = navigator_path.find("/");
  if (sep == std::string::npos) {
    // Prevent same file reference being created and added multiple times.
    for (const auto& child : children_) {
      if (child->Class() != PBXFileReferenceClass)
        continue;

      PBXFileReference* child_as_file_reference =
          static_cast<PBXFileReference*>(child.get());
      if (child_as_file_reference->Name() == navigator_path &&
          child_as_file_reference->path() == source_path) {
        return child_as_file_reference;
      }
    }

    children_.push_back(base::MakeUnique<PBXFileReference>(
        navigator_path, source_path, std::string()));
    return static_cast<PBXFileReference*>(children_.back().get());
  }

  PBXGroup* group = nullptr;
  base::StringPiece component(navigator_path.data(), sep);
  for (const auto& child : children_) {
    if (child->Class() != PBXGroupClass)
      continue;

    PBXGroup* child_as_group = static_cast<PBXGroup*>(child.get());
    if (child_as_group->name_ == component) {
      group = child_as_group;
      break;
    }
  }

  if (!group) {
    children_.push_back(base::MakeUnique<PBXGroup>(component.as_string(),
                                                   component.as_string()));
    group = static_cast<PBXGroup*>(children_.back().get());
  }

  DCHECK(group);
  DCHECK(group->name_ == component);
  return group->AddSourceFile(navigator_path.substr(sep + 1), source_path);
}

PBXObjectClass PBXGroup::Class() const {
  return PBXGroupClass;
}

std::string PBXGroup::Name() const {
  if (!name_.empty())
    return name_;
  if (!path_.empty())
    return path_;
  return std::string();
}

void PBXGroup::Visit(PBXObjectVisitor& visitor) {
  PBXObject::Visit(visitor);
  for (const auto& child : children_) {
    child->Visit(visitor);
  }
}

void PBXGroup::Print(std::ostream& out, unsigned indent) const {
  const std::string indent_str(indent, '\t');
  const IndentRules rules = {false, indent + 1};
  out << indent_str << Reference() << " = {\n";
  PrintProperty(out, rules, "isa", ToString(Class()));
  PrintProperty(out, rules, "children", children_);
  if (!name_.empty())
    PrintProperty(out, rules, "name", name_);
  if (is_source_ && !path_.empty())
    PrintProperty(out, rules, "path", path_);
  PrintProperty(out, rules, "sourceTree", "<group>");
  out << indent_str << "};\n";
}

// PBXNativeTarget ------------------------------------------------------------

PBXNativeTarget::PBXNativeTarget(const std::string& name,
                                 const std::string& shell_script,
                                 const std::string& config_name,
                                 const PBXAttributes& attributes,
                                 const std::string& product_type,
                                 const std::string& product_name,
                                 const PBXFileReference* product_reference)
    : PBXTarget(name, shell_script, config_name, attributes),
      product_reference_(product_reference),
      product_type_(product_type),
      product_name_(product_name) {
  DCHECK(product_reference_);
  build_phases_.push_back(base::MakeUnique<PBXSourcesBuildPhase>());
  source_build_phase_ =
      static_cast<PBXSourcesBuildPhase*>(build_phases_.back().get());

  build_phases_.push_back(base::MakeUnique<PBXFrameworksBuildPhase>());
}

PBXNativeTarget::~PBXNativeTarget() {}

void PBXNativeTarget::AddFileForIndexing(const PBXFileReference* file_reference,
                                         const CompilerFlags compiler_flag) {
  DCHECK(file_reference);
  source_build_phase_->AddBuildFile(base::MakeUnique<PBXBuildFile>(
      file_reference, source_build_phase_, compiler_flag));
}

PBXObjectClass PBXNativeTarget::Class() const {
  return PBXNativeTargetClass;
}

void PBXNativeTarget::Print(std::ostream& out, unsigned indent) const {
  const std::string indent_str(indent, '\t');
  const IndentRules rules = {false, indent + 1};
  out << indent_str << Reference() << " = {\n";
  PrintProperty(out, rules, "isa", ToString(Class()));
  PrintProperty(out, rules, "buildConfigurationList", configurations_);
  PrintProperty(out, rules, "buildPhases", build_phases_);
  PrintProperty(out, rules, "buildRules", EmptyPBXObjectVector());
  PrintProperty(out, rules, "dependencies", dependencies_);
  PrintProperty(out, rules, "name", name_);
  PrintProperty(out, rules, "productName", product_name_);
  PrintProperty(out, rules, "productReference", product_reference_);
  PrintProperty(out, rules, "productType", product_type_);
  out << indent_str << "};\n";
}

// PBXProject -----------------------------------------------------------------

PBXProject::PBXProject(const std::string& name,
                       const std::string& config_name,
                       const std::string& source_path,
                       const PBXAttributes& attributes)
    : name_(name), config_name_(config_name), target_for_indexing_(nullptr) {
  attributes_["BuildIndependentTargetsInParallel"] = "YES";

  main_group_.reset(new PBXGroup);
  sources_ = static_cast<PBXGroup*>(
      main_group_->AddChild(base::MakeUnique<PBXGroup>(source_path, "Source")));
  sources_->set_is_source(true);
  products_ = static_cast<PBXGroup*>(main_group_->AddChild(
      base::MakeUnique<PBXGroup>(std::string(), "Product")));
  main_group_->AddChild(base::MakeUnique<PBXGroup>(std::string(), "Build"));

  configurations_.reset(new XCConfigurationList(config_name, attributes, this));
}

PBXProject::~PBXProject() {}

void PBXProject::AddSourceFileToIndexingTarget(
    const std::string& navigator_path,
    const std::string& source_path,
    const CompilerFlags compiler_flag) {
  if (!target_for_indexing_) {
    AddIndexingTarget();
  }
  AddSourceFile(navigator_path, source_path, compiler_flag,
                target_for_indexing_);
}

void PBXProject::AddSourceFile(const std::string& navigator_path,
                               const std::string& source_path,
                               const CompilerFlags compiler_flag,
                               PBXNativeTarget* target) {
  PBXFileReference* file_reference =
      sources_->AddSourceFile(navigator_path, source_path);
  if (!IsSourceFileForIndexing(SourceFile(source_path)))
    return;

  DCHECK(target);
  target->AddFileForIndexing(file_reference, compiler_flag);
}

void PBXProject::AddAggregateTarget(const std::string& name,
                                    const std::string& shell_script) {
  PBXAttributes attributes;
  attributes["CODE_SIGNING_REQUIRED"] = "NO";
  attributes["CONFIGURATION_BUILD_DIR"] = ".";
  attributes["PRODUCT_NAME"] = name;

  targets_.push_back(base::MakeUnique<PBXAggregateTarget>(
      name, shell_script, config_name_, attributes));
}

void PBXProject::AddIndexingTarget() {
  DCHECK(!target_for_indexing_);
  PBXAttributes attributes;
  attributes["EXECUTABLE_PREFIX"] = "";
  attributes["HEADER_SEARCH_PATHS"] = sources_->path();
  attributes["PRODUCT_NAME"] = "sources";

  PBXFileReference* product_reference = static_cast<PBXFileReference*>(
      products_->AddChild(base::MakeUnique<PBXFileReference>(
          std::string(), "sources", "compiled.mach-o.executable")));

  const char product_type[] = "com.apple.product-type.tool";
  targets_.push_back(base::MakeUnique<PBXNativeTarget>(
      "sources", std::string(), config_name_, attributes, product_type,
      "sources", product_reference));
  target_for_indexing_ = static_cast<PBXNativeTarget*>(targets_.back().get());
}

PBXNativeTarget* PBXProject::AddNativeTarget(
    const std::string& name,
    const std::string& type,
    const std::string& output_name,
    const std::string& output_type,
    const std::string& shell_script,
    const PBXAttributes& extra_attributes) {
  base::StringPiece ext = FindExtension(&output_name);
  PBXFileReference* product = static_cast<PBXFileReference*>(
      products_->AddChild(base::MakeUnique<PBXFileReference>(
          std::string(), output_name,
          type.empty() ? GetSourceType(ext) : type)));

  size_t ext_offset = FindExtensionOffset(output_name);
  std::string product_name = ext_offset != std::string::npos
                                 ? output_name.substr(0, ext_offset - 1)
                                 : output_name;

  PBXAttributes attributes = extra_attributes;
  attributes["CODE_SIGNING_REQUIRED"] = "NO";
  attributes["CONFIGURATION_BUILD_DIR"] = ".";
  attributes["PRODUCT_NAME"] = product_name;

  targets_.push_back(base::MakeUnique<PBXNativeTarget>(
      name, shell_script, config_name_, attributes, output_type, product_name,
      product));
  return static_cast<PBXNativeTarget*>(targets_.back().get());
}

void PBXProject::SetProjectDirPath(const std::string& project_dir_path) {
  DCHECK(!project_dir_path.empty());
  project_dir_path_.assign(project_dir_path);
}

void PBXProject::SetProjectRoot(const std::string& project_root) {
  DCHECK(!project_root.empty());
  project_root_.assign(project_root);
}

void PBXProject::AddTarget(std::unique_ptr<PBXTarget> target) {
  DCHECK(target);
  targets_.push_back(std::move(target));
}

PBXObjectClass PBXProject::Class() const {
  return PBXProjectClass;
}

std::string PBXProject::Name() const {
  return name_;
}

std::string PBXProject::Comment() const {
  return "Project object";
}

void PBXProject::Visit(PBXObjectVisitor& visitor) {
  PBXObject::Visit(visitor);
  configurations_->Visit(visitor);
  main_group_->Visit(visitor);
  for (const auto& target : targets_) {
    target->Visit(visitor);
  }
}

void PBXProject::Print(std::ostream& out, unsigned indent) const {
  const std::string indent_str(indent, '\t');
  const IndentRules rules = {false, indent + 1};
  out << indent_str << Reference() << " = {\n";
  PrintProperty(out, rules, "isa", ToString(Class()));
  PrintProperty(out, rules, "attributes", attributes_);
  PrintProperty(out, rules, "buildConfigurationList", configurations_);
  PrintProperty(out, rules, "compatibilityVersion", "Xcode 3.2");
  PrintProperty(out, rules, "developmentRegion", "English");
  PrintProperty(out, rules, "hasScannedForEncodings", 1u);
  PrintProperty(out, rules, "knownRegions", std::vector<std::string>({"en"}));
  PrintProperty(out, rules, "mainGroup", main_group_);
  PrintProperty(out, rules, "projectDirPath", project_dir_path_);
  PrintProperty(out, rules, "projectRoot", project_root_);
  PrintProperty(out, rules, "targets", targets_);
  out << indent_str << "};\n";
}

// PBXShellScriptBuildPhase ---------------------------------------------------

PBXShellScriptBuildPhase::PBXShellScriptBuildPhase(
    const std::string& name,
    const std::string& shell_script)
    : name_("Action \"Compile and copy " + name + " via ninja\""),
      shell_script_(shell_script) {}

PBXShellScriptBuildPhase::~PBXShellScriptBuildPhase() {}

PBXObjectClass PBXShellScriptBuildPhase::Class() const {
  return PBXShellScriptBuildPhaseClass;
}

std::string PBXShellScriptBuildPhase::Name() const {
  return name_;
}

void PBXShellScriptBuildPhase::Print(std::ostream& out, unsigned indent) const {
  const std::string indent_str(indent, '\t');
  const IndentRules rules = {false, indent + 1};
  out << indent_str << Reference() << " = {\n";
  PrintProperty(out, rules, "isa", ToString(Class()));
  PrintProperty(out, rules, "buildActionMask", 0x7fffffffu);
  PrintProperty(out, rules, "files", EmptyPBXObjectVector());
  PrintProperty(out, rules, "inputPaths", EmptyPBXObjectVector());
  PrintProperty(out, rules, "name", name_);
  PrintProperty(out, rules, "outputPaths", EmptyPBXObjectVector());
  PrintProperty(out, rules, "runOnlyForDeploymentPostprocessing", 0u);
  PrintProperty(out, rules, "shellPath", "/bin/sh");
  PrintProperty(out, rules, "shellScript", shell_script_);
  PrintProperty(out, rules, "showEnvVarsInLog", 0u);
  out << indent_str << "};\n";
}

// PBXSourcesBuildPhase -------------------------------------------------------

PBXSourcesBuildPhase::PBXSourcesBuildPhase() {}

PBXSourcesBuildPhase::~PBXSourcesBuildPhase() {}

void PBXSourcesBuildPhase::AddBuildFile(
    std::unique_ptr<PBXBuildFile> build_file) {
  files_.push_back(std::move(build_file));
}

PBXObjectClass PBXSourcesBuildPhase::Class() const {
  return PBXSourcesBuildPhaseClass;
}

std::string PBXSourcesBuildPhase::Name() const {
  return "Sources";
}

void PBXSourcesBuildPhase::Visit(PBXObjectVisitor& visitor) {
  PBXBuildPhase::Visit(visitor);
  for (const auto& file : files_) {
    file->Visit(visitor);
  }
}

void PBXSourcesBuildPhase::Print(std::ostream& out, unsigned indent) const {
  const std::string indent_str(indent, '\t');
  const IndentRules rules = {false, indent + 1};
  out << indent_str << Reference() << " = {\n";
  PrintProperty(out, rules, "isa", ToString(Class()));
  PrintProperty(out, rules, "buildActionMask", 0x7fffffffu);
  PrintProperty(out, rules, "files", files_);
  PrintProperty(out, rules, "runOnlyForDeploymentPostprocessing", 0u);
  out << indent_str << "};\n";
}

PBXTargetDependency::PBXTargetDependency(
    const PBXTarget* target,
    std::unique_ptr<PBXContainerItemProxy> container_item_proxy)
    : target_(target), container_item_proxy_(std::move(container_item_proxy)) {}

PBXTargetDependency::~PBXTargetDependency() {}

PBXObjectClass PBXTargetDependency::Class() const {
  return PBXTargetDependencyClass;
}
std::string PBXTargetDependency::Name() const {
  return "PBXTargetDependency";
}
void PBXTargetDependency::Visit(PBXObjectVisitor& visitor) {
  PBXObject::Visit(visitor);
  container_item_proxy_->Visit(visitor);
}
void PBXTargetDependency::Print(std::ostream& out, unsigned indent) const {
  const std::string indent_str(indent, '\t');
  const IndentRules rules = {false, indent + 1};
  out << indent_str << Reference() << " = {\n";
  PrintProperty(out, rules, "isa", ToString(Class()));
  PrintProperty(out, rules, "target", target_);
  PrintProperty(out, rules, "targetProxy", container_item_proxy_);
  out << indent_str << "};\n";
}

// XCBuildConfiguration -------------------------------------------------------

XCBuildConfiguration::XCBuildConfiguration(const std::string& name,
                                           const PBXAttributes& attributes)
    : attributes_(attributes), name_(name) {}

XCBuildConfiguration::~XCBuildConfiguration() {}

PBXObjectClass XCBuildConfiguration::Class() const {
  return XCBuildConfigurationClass;
}

std::string XCBuildConfiguration::Name() const {
  return name_;
}

void XCBuildConfiguration::Print(std::ostream& out, unsigned indent) const {
  const std::string indent_str(indent, '\t');
  const IndentRules rules = {false, indent + 1};
  out << indent_str << Reference() << " = {\n";
  PrintProperty(out, rules, "isa", ToString(Class()));
  PrintProperty(out, rules, "buildSettings", attributes_);
  PrintProperty(out, rules, "name", name_);
  out << indent_str << "};\n";
}

// XCConfigurationList --------------------------------------------------------

XCConfigurationList::XCConfigurationList(const std::string& name,
                                         const PBXAttributes& attributes,
                                         const PBXObject* owner_reference)
    : owner_reference_(owner_reference) {
  DCHECK(owner_reference_);
  configurations_.push_back(
      base::MakeUnique<XCBuildConfiguration>(name, attributes));
}

XCConfigurationList::~XCConfigurationList() {}

PBXObjectClass XCConfigurationList::Class() const {
  return XCConfigurationListClass;
}

std::string XCConfigurationList::Name() const {
  std::stringstream buffer;
  buffer << "Build configuration list for "
         << ToString(owner_reference_->Class()) << " \""
         << owner_reference_->Name() << "\"";
  return buffer.str();
}

void XCConfigurationList::Visit(PBXObjectVisitor& visitor) {
  PBXObject::Visit(visitor);
  for (const auto& configuration : configurations_) {
    configuration->Visit(visitor);
  }
}

void XCConfigurationList::Print(std::ostream& out, unsigned indent) const {
  const std::string indent_str(indent, '\t');
  const IndentRules rules = {false, indent + 1};
  out << indent_str << Reference() << " = {\n";
  PrintProperty(out, rules, "isa", ToString(Class()));
  PrintProperty(out, rules, "buildConfigurations", configurations_);
  PrintProperty(out, rules, "defaultConfigurationIsVisible", 1u);
  PrintProperty(out, rules, "defaultConfigurationName",
                configurations_[0]->Name());
  out << indent_str << "};\n";
}
