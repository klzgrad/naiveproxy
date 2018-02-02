// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_XCODE_OBJECT_H_
#define TOOLS_GN_XCODE_OBJECT_H_

#include <iosfwd>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/macros.h"

// Helper classes to generate Xcode project files.
//
// This code is based on gyp xcodeproj_file.py generator. It does not support
// all features of Xcode project but instead just enough to implement a hybrid
// mode where Xcode uses external scripts to perform the compilation steps.
//
// See https://chromium.googlesource.com/external/gyp/+/master/pylib/gyp/xcodeproj_file.py
// for more information on Xcode project file format.

enum class CompilerFlags {
  NONE,
  HELP,
};

// PBXObjectClass -------------------------------------------------------------

enum PBXObjectClass {
  // Those values needs to stay sorted in alphabetic order.
  PBXAggregateTargetClass,
  PBXBuildFileClass,
  PBXContainerItemProxyClass,
  PBXFileReferenceClass,
  PBXFrameworksBuildPhaseClass,
  PBXGroupClass,
  PBXNativeTargetClass,
  PBXProjectClass,
  PBXShellScriptBuildPhaseClass,
  PBXSourcesBuildPhaseClass,
  PBXTargetDependencyClass,
  XCBuildConfigurationClass,
  XCConfigurationListClass,
};

const char* ToString(PBXObjectClass cls);

// Forward-declarations -------------------------------------------------------

class PBXAggregateTarget;
class PBXBuildFile;
class PBXBuildPhase;
class PBXContainerItemProxy;
class PBXFileReference;
class PBXFrameworksBuildPhase;
class PBXGroup;
class PBXNativeTarget;
class PBXObject;
class PBXProject;
class PBXShellScriptBuildPhase;
class PBXSourcesBuildPhase;
class PBXTarget;
class PBXTargetDependency;
class XCBuildConfiguration;
class XCConfigurationList;

using PBXAttributes = std::map<std::string, std::string>;

// PBXObjectVisitor -----------------------------------------------------------

class PBXObjectVisitor {
 public:
  PBXObjectVisitor();
  virtual ~PBXObjectVisitor();
  virtual void Visit(PBXObject* object) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(PBXObjectVisitor);
};

// PBXObject ------------------------------------------------------------------

class PBXObject {
 public:
  PBXObject();
  virtual ~PBXObject();

  const std::string id() const { return id_; }
  void SetId(const std::string& id);

  std::string Reference() const;

  virtual PBXObjectClass Class() const = 0;
  virtual std::string Name() const = 0;
  virtual std::string Comment() const;
  virtual void Visit(PBXObjectVisitor& visitor);
  virtual void Print(std::ostream& out, unsigned indent) const = 0;

 private:
  std::string id_;

  DISALLOW_COPY_AND_ASSIGN(PBXObject);
};

// PBXBuildPhase --------------------------------------------------------------

class PBXBuildPhase : public PBXObject {
 public:
  PBXBuildPhase();
  ~PBXBuildPhase() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(PBXBuildPhase);
};

// PBXTarget ------------------------------------------------------------------

class PBXTarget : public PBXObject {
 public:
  PBXTarget(const std::string& name,
            const std::string& shell_script,
            const std::string& config_name,
            const PBXAttributes& attributes);
  ~PBXTarget() override;

  void AddDependency(std::unique_ptr<PBXTargetDependency> dependency);

  // PBXObject implementation.
  std::string Name() const override;
  void Visit(PBXObjectVisitor& visitor) override;

 protected:
  std::unique_ptr<XCConfigurationList> configurations_;
  std::vector<std::unique_ptr<PBXBuildPhase>> build_phases_;
  std::vector<std::unique_ptr<PBXTargetDependency>> dependencies_;
  PBXSourcesBuildPhase* source_build_phase_;
  std::string name_;

 private:
  DISALLOW_COPY_AND_ASSIGN(PBXTarget);
};

// PBXAggregateTarget ---------------------------------------------------------

class PBXAggregateTarget : public PBXTarget {
 public:
  PBXAggregateTarget(const std::string& name,
                     const std::string& shell_script,
                     const std::string& config_name,
                     const PBXAttributes& attributes);
  ~PBXAggregateTarget() override;

  // PBXObject implementation.
  PBXObjectClass Class() const override;
  void Print(std::ostream& out, unsigned indent) const override;

 private:
  DISALLOW_COPY_AND_ASSIGN(PBXAggregateTarget);
};

// PBXBuildFile ---------------------------------------------------------------

class PBXBuildFile : public PBXObject {
 public:
  PBXBuildFile(const PBXFileReference* file_reference,
               const PBXSourcesBuildPhase* build_phase,
               const CompilerFlags compiler_flag);
  ~PBXBuildFile() override;

  // PBXObject implementation.
  PBXObjectClass Class() const override;
  std::string Name() const override;
  void Print(std::ostream& out, unsigned indent) const override;

 private:
  const PBXFileReference* file_reference_;
  const PBXSourcesBuildPhase* build_phase_;
  const CompilerFlags compiler_flag_;

  DISALLOW_COPY_AND_ASSIGN(PBXBuildFile);
};

// PBXContainerItemProxy ------------------------------------------------------
class PBXContainerItemProxy : public PBXObject {
 public:
  PBXContainerItemProxy(const PBXProject* project, const PBXTarget* target);
  ~PBXContainerItemProxy() override;

  // PBXObject implementation.
  PBXObjectClass Class() const override;
  std::string Name() const override;
  void Visit(PBXObjectVisitor& visitor) override;
  void Print(std::ostream& out, unsigned indent) const override;

 private:
  const PBXProject* project_;
  const PBXTarget* target_;

  DISALLOW_COPY_AND_ASSIGN(PBXContainerItemProxy);
};

// PBXFileReference -----------------------------------------------------------

class PBXFileReference : public PBXObject {
 public:
  PBXFileReference(const std::string& name,
                   const std::string& path,
                   const std::string& type);
  ~PBXFileReference() override;

  // PBXObject implementation.
  PBXObjectClass Class() const override;
  std::string Name() const override;
  void Print(std::ostream& out, unsigned indent) const override;

  const std::string& path() const { return path_; }

 private:
  std::string name_;
  std::string path_;
  std::string type_;

  DISALLOW_COPY_AND_ASSIGN(PBXFileReference);
};

// PBXFrameworksBuildPhase ----------------------------------------------------

class PBXFrameworksBuildPhase : public PBXBuildPhase {
 public:
  PBXFrameworksBuildPhase();
  ~PBXFrameworksBuildPhase() override;

  // PBXObject implementation.
  PBXObjectClass Class() const override;
  std::string Name() const override;
  void Print(std::ostream& out, unsigned indent) const override;

 private:
  DISALLOW_COPY_AND_ASSIGN(PBXFrameworksBuildPhase);
};

// PBXGroup -------------------------------------------------------------------

class PBXGroup : public PBXObject {
 public:
  explicit PBXGroup(const std::string& path = std::string(),
                    const std::string& name = std::string());
  ~PBXGroup() override;

  const std::string& path() const { return path_; }

  PBXObject* AddChild(std::unique_ptr<PBXObject> child);
  PBXFileReference* AddSourceFile(const std::string& navigator_path,
                                  const std::string& source_path);
  bool is_source() { return is_source_; }
  void set_is_source(const bool is_source) { is_source_ = is_source; }

  // PBXObject implementation.
  PBXObjectClass Class() const override;
  std::string Name() const override;
  void Visit(PBXObjectVisitor& visitor) override;
  void Print(std::ostream& out, unsigned indent) const override;

 private:
  std::vector<std::unique_ptr<PBXObject>> children_;
  std::string name_;
  std::string path_;
  bool is_source_ = false;

  DISALLOW_COPY_AND_ASSIGN(PBXGroup);
};

// PBXNativeTarget ------------------------------------------------------------

class PBXNativeTarget : public PBXTarget {
 public:
  PBXNativeTarget(const std::string& name,
                  const std::string& shell_script,
                  const std::string& config_name,
                  const PBXAttributes& attributes,
                  const std::string& product_type,
                  const std::string& product_name,
                  const PBXFileReference* product_reference);
  ~PBXNativeTarget() override;

  void AddFileForIndexing(const PBXFileReference* file_reference,
                          const CompilerFlags compiler_flag);

  // PBXObject implementation.
  PBXObjectClass Class() const override;
  void Print(std::ostream& out, unsigned indent) const override;

 private:
  const PBXFileReference* product_reference_;
  std::string product_type_;
  std::string product_name_;

  DISALLOW_COPY_AND_ASSIGN(PBXNativeTarget);
};

// PBXProject -----------------------------------------------------------------

class PBXProject : public PBXObject {
 public:
  PBXProject(const std::string& name,
             const std::string& config_name,
             const std::string& source_path,
             const PBXAttributes& attributes);
  ~PBXProject() override;

  void AddSourceFileToIndexingTarget(const std::string& navigator_path,
                                     const std::string& source_path,
                                     const CompilerFlags compiler_flag);
  void AddSourceFile(const std::string& navigator_path,
                     const std::string& source_path,
                     const CompilerFlags compiler_flag,
                     PBXNativeTarget* target);
  void AddAggregateTarget(const std::string& name,
                          const std::string& shell_script);
  void AddIndexingTarget();
  PBXNativeTarget* AddNativeTarget(
      const std::string& name,
      const std::string& type,
      const std::string& output_name,
      const std::string& output_type,
      const std::string& shell_script,
      const PBXAttributes& extra_attributes = PBXAttributes());

  void SetProjectDirPath(const std::string& project_dir_path);
  void SetProjectRoot(const std::string& project_root);
  void AddTarget(std::unique_ptr<PBXTarget> target);

  // PBXObject implementation.
  PBXObjectClass Class() const override;
  std::string Name() const override;
  std::string Comment() const override;
  void Visit(PBXObjectVisitor& visitor) override;
  void Print(std::ostream& out, unsigned indent) const override;

 private:
  PBXAttributes attributes_;
  std::unique_ptr<XCConfigurationList> configurations_;
  std::unique_ptr<PBXGroup> main_group_;
  std::string project_dir_path_;
  std::string project_root_;
  std::vector<std::unique_ptr<PBXTarget>> targets_;
  std::string name_;
  std::string config_name_;

  PBXGroup* sources_;
  PBXGroup* products_;
  PBXNativeTarget* target_for_indexing_;

  DISALLOW_COPY_AND_ASSIGN(PBXProject);
};

// PBXShellScriptBuildPhase ---------------------------------------------------

class PBXShellScriptBuildPhase : public PBXBuildPhase {
 public:
  PBXShellScriptBuildPhase(const std::string& name,
                           const std::string& shell_script);
  ~PBXShellScriptBuildPhase() override;

  // PBXObject implementation.
  PBXObjectClass Class() const override;
  std::string Name() const override;
  void Print(std::ostream& out, unsigned indent) const override;

 private:
  std::string name_;
  std::string shell_script_;

  DISALLOW_COPY_AND_ASSIGN(PBXShellScriptBuildPhase);
};

// PBXSourcesBuildPhase -------------------------------------------------------

class PBXSourcesBuildPhase : public PBXBuildPhase {
 public:
  PBXSourcesBuildPhase();
  ~PBXSourcesBuildPhase() override;

  void AddBuildFile(std::unique_ptr<PBXBuildFile> build_file);

  // PBXObject implementation.
  PBXObjectClass Class() const override;
  std::string Name() const override;
  void Visit(PBXObjectVisitor& visitor) override;
  void Print(std::ostream& out, unsigned indent) const override;

 private:
  std::vector<std::unique_ptr<PBXBuildFile>> files_;

  DISALLOW_COPY_AND_ASSIGN(PBXSourcesBuildPhase);
};

// PBXTargetDependency -----------------------------------------------------
class PBXTargetDependency : public PBXObject {
 public:
  PBXTargetDependency(
      const PBXTarget* target,
      std::unique_ptr<PBXContainerItemProxy> container_item_proxy);
  ~PBXTargetDependency() override;

  // PBXObject implementation.
  PBXObjectClass Class() const override;
  std::string Name() const override;
  void Visit(PBXObjectVisitor& visitor) override;
  void Print(std::ostream& out, unsigned indent) const override;

 private:
  const PBXTarget* target_;
  std::unique_ptr<PBXContainerItemProxy> container_item_proxy_;

  DISALLOW_COPY_AND_ASSIGN(PBXTargetDependency);
};

// XCBuildConfiguration -------------------------------------------------------

class XCBuildConfiguration : public PBXObject {
 public:
  XCBuildConfiguration(const std::string& name,
                       const PBXAttributes& attributes);
  ~XCBuildConfiguration() override;

  // PBXObject implementation.
  PBXObjectClass Class() const override;
  std::string Name() const override;
  void Print(std::ostream& out, unsigned indent) const override;

 private:
  PBXAttributes attributes_;
  std::string name_;

  DISALLOW_COPY_AND_ASSIGN(XCBuildConfiguration);
};

// XCConfigurationList --------------------------------------------------------

class XCConfigurationList : public PBXObject {
 public:
  XCConfigurationList(const std::string& name,
                      const PBXAttributes& attributes,
                      const PBXObject* owner_reference);
  ~XCConfigurationList() override;

  // PBXObject implementation.
  PBXObjectClass Class() const override;
  std::string Name() const override;
  void Visit(PBXObjectVisitor& visitor) override;
  void Print(std::ostream& out, unsigned indent) const override;

 private:
  std::vector<std::unique_ptr<XCBuildConfiguration>> configurations_;
  const PBXObject* owner_reference_;

  DISALLOW_COPY_AND_ASSIGN(XCConfigurationList);
};

#endif  // TOOLS_GN_XCODE_OBJECT_H_
