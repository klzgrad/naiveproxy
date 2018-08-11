// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/toolchain.h"

#include <stddef.h>
#include <string.h>
#include <utility>

#include "base/logging.h"
#include "tools/gn/target.h"
#include "tools/gn/value.h"

const char* Toolchain::kToolCc = "cc";
const char* Toolchain::kToolCxx = "cxx";
const char* Toolchain::kToolObjC = "objc";
const char* Toolchain::kToolObjCxx = "objcxx";
const char* Toolchain::kToolRc = "rc";
const char* Toolchain::kToolAsm = "asm";
const char* Toolchain::kToolAlink = "alink";
const char* Toolchain::kToolSolink = "solink";
const char* Toolchain::kToolSolinkModule = "solink_module";
const char* Toolchain::kToolLink = "link";
const char* Toolchain::kToolStamp = "stamp";
const char* Toolchain::kToolCopy = "copy";
const char* Toolchain::kToolCopyBundleData = "copy_bundle_data";
const char* Toolchain::kToolCompileXCAssets = "compile_xcassets";
const char* Toolchain::kToolAction = "action";

Toolchain::Toolchain(const Settings* settings,
                     const Label& label,
                     const std::set<SourceFile>& build_dependency_files)
    : Item(settings, label, build_dependency_files), setup_complete_(false) {}

Toolchain::~Toolchain() = default;

Toolchain* Toolchain::AsToolchain() {
  return this;
}

const Toolchain* Toolchain::AsToolchain() const {
  return this;
}

// static
Toolchain::ToolType Toolchain::ToolNameToType(const base::StringPiece& str) {
  if (str == kToolCc) return TYPE_CC;
  if (str == kToolCxx) return TYPE_CXX;
  if (str == kToolObjC) return TYPE_OBJC;
  if (str == kToolObjCxx) return TYPE_OBJCXX;
  if (str == kToolRc) return TYPE_RC;
  if (str == kToolAsm) return TYPE_ASM;
  if (str == kToolAlink) return TYPE_ALINK;
  if (str == kToolSolink) return TYPE_SOLINK;
  if (str == kToolSolinkModule) return TYPE_SOLINK_MODULE;
  if (str == kToolLink) return TYPE_LINK;
  if (str == kToolStamp) return TYPE_STAMP;
  if (str == kToolCopy) return TYPE_COPY;
  if (str == kToolCopyBundleData) return TYPE_COPY_BUNDLE_DATA;
  if (str == kToolCompileXCAssets) return TYPE_COMPILE_XCASSETS;
  if (str == kToolAction) return TYPE_ACTION;
  return TYPE_NONE;
}

// static
std::string Toolchain::ToolTypeToName(ToolType type) {
  switch (type) {
    case TYPE_CC: return kToolCc;
    case TYPE_CXX: return kToolCxx;
    case TYPE_OBJC: return kToolObjC;
    case TYPE_OBJCXX: return kToolObjCxx;
    case TYPE_RC: return kToolRc;
    case TYPE_ASM: return kToolAsm;
    case TYPE_ALINK: return kToolAlink;
    case TYPE_SOLINK: return kToolSolink;
    case TYPE_SOLINK_MODULE: return kToolSolinkModule;
    case TYPE_LINK: return kToolLink;
    case TYPE_STAMP: return kToolStamp;
    case TYPE_COPY: return kToolCopy;
    case TYPE_COPY_BUNDLE_DATA: return kToolCopyBundleData;
    case TYPE_COMPILE_XCASSETS: return kToolCompileXCAssets;
    case TYPE_ACTION: return kToolAction;
    default:
      NOTREACHED();
      return std::string();
  }
}

Tool* Toolchain::GetTool(ToolType type) {
  DCHECK(type != TYPE_NONE);
  return tools_[static_cast<size_t>(type)].get();
}

const Tool* Toolchain::GetTool(ToolType type) const {
  DCHECK(type != TYPE_NONE);
  return tools_[static_cast<size_t>(type)].get();
}

void Toolchain::SetTool(ToolType type, std::unique_ptr<Tool> t) {
  DCHECK(type != TYPE_NONE);
  DCHECK(!tools_[type].get());
  t->SetComplete();
  tools_[type] = std::move(t);
}

void Toolchain::ToolchainSetupComplete() {
  // Collect required bits from all tools.
  for (const auto& tool : tools_) {
    if (tool)
      substitution_bits_.MergeFrom(tool->substitution_bits());
  }

  setup_complete_ = true;
}

// static
Toolchain::ToolType Toolchain::GetToolTypeForSourceType(SourceFileType type) {
  switch (type) {
    case SOURCE_C:
      return TYPE_CC;
    case SOURCE_CPP:
      return TYPE_CXX;
    case SOURCE_M:
      return TYPE_OBJC;
    case SOURCE_MM:
      return TYPE_OBJCXX;
    case SOURCE_ASM:
    case SOURCE_S:
      return TYPE_ASM;
    case SOURCE_RC:
      return TYPE_RC;
    case SOURCE_UNKNOWN:
    case SOURCE_H:
    case SOURCE_O:
    case SOURCE_DEF:
      return TYPE_NONE;
    default:
      NOTREACHED();
      return TYPE_NONE;
  }
}

const Tool* Toolchain::GetToolForSourceType(SourceFileType type) {
  return tools_[GetToolTypeForSourceType(type)].get();
}

// static
Toolchain::ToolType Toolchain::GetToolTypeForTargetFinalOutput(
    const Target* target) {
  // The contents of this list might be suprising (i.e. stamp tool for copy
  // rules). See the header for why.
  switch (target->output_type()) {
    case Target::GROUP:
      return TYPE_STAMP;
    case Target::EXECUTABLE:
      return Toolchain::TYPE_LINK;
    case Target::SHARED_LIBRARY:
      return Toolchain::TYPE_SOLINK;
    case Target::LOADABLE_MODULE:
      return Toolchain::TYPE_SOLINK_MODULE;
    case Target::STATIC_LIBRARY:
      return Toolchain::TYPE_ALINK;
    case Target::SOURCE_SET:
      return TYPE_STAMP;
    case Target::ACTION:
    case Target::ACTION_FOREACH:
    case Target::BUNDLE_DATA:
    case Target::CREATE_BUNDLE:
    case Target::COPY_FILES:
      return TYPE_STAMP;
    default:
      NOTREACHED();
      return Toolchain::TYPE_NONE;
  }
}

const Tool* Toolchain::GetToolForTargetFinalOutput(const Target* target) const {
  return tools_[GetToolTypeForTargetFinalOutput(target)].get();
}
