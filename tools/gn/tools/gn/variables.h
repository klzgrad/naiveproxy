// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_VARIABLES_H_
#define TOOLS_GN_VARIABLES_H_

#include <map>

#include "base/strings/string_piece.h"

namespace variables {

// Builtin vars ----------------------------------------------------------------

extern const char kHostCpu[];
extern const char kHostCpu_HelpShort[];
extern const char kHostCpu_Help[];

extern const char kHostOs[];
extern const char kHostOs_HelpShort[];
extern const char kHostOs_Help[];

extern const char kCurrentCpu[];
extern const char kCurrentCpu_HelpShort[];
extern const char kCurrentCpu_Help[];

extern const char kCurrentOs[];
extern const char kCurrentOs_HelpShort[];
extern const char kCurrentOs_Help[];

extern const char kCurrentToolchain[];
extern const char kCurrentToolchain_HelpShort[];
extern const char kCurrentToolchain_Help[];

extern const char kDefaultToolchain[];
extern const char kDefaultToolchain_HelpShort[];
extern const char kDefaultToolchain_Help[];

extern const char kInvoker[];
extern const char kInvoker_HelpShort[];
extern const char kInvoker_Help[];

extern const char kPythonPath[];
extern const char kPythonPath_HelpShort[];
extern const char kPythonPath_Help[];

extern const char kRootBuildDir[];
extern const char kRootBuildDir_HelpShort[];
extern const char kRootBuildDir_Help[];

extern const char kRootGenDir[];
extern const char kRootGenDir_HelpShort[];
extern const char kRootGenDir_Help[];

extern const char kRootOutDir[];
extern const char kRootOutDir_HelpShort[];
extern const char kRootOutDir_Help[];

extern const char kTargetCpu[];
extern const char kTargetCpu_HelpShort[];
extern const char kTargetCpu_Help[];

extern const char kTargetName[];
extern const char kTargetName_HelpShort[];
extern const char kTargetName_Help[];

extern const char kTargetOs[];
extern const char kTargetOs_HelpShort[];
extern const char kTargetOs_Help[];

extern const char kTargetGenDir[];
extern const char kTargetGenDir_HelpShort[];
extern const char kTargetGenDir_Help[];

extern const char kTargetOutDir[];
extern const char kTargetOutDir_HelpShort[];
extern const char kTargetOutDir_Help[];

// Target vars -----------------------------------------------------------------

extern const char kAllDependentConfigs[];
extern const char kAllDependentConfigs_HelpShort[];
extern const char kAllDependentConfigs_Help[];

extern const char kAllowCircularIncludesFrom[];
extern const char kAllowCircularIncludesFrom_HelpShort[];
extern const char kAllowCircularIncludesFrom_Help[];

extern const char kArflags[];
extern const char kArflags_HelpShort[];
extern const char kArflags_Help[];

extern const char kArgs[];
extern const char kArgs_HelpShort[];
extern const char kArgs_Help[];

extern const char kAsmflags[];
extern const char kAsmflags_HelpShort[];
extern const char* kAsmflags_Help;

extern const char kAssertNoDeps[];
extern const char kAssertNoDeps_HelpShort[];
extern const char kAssertNoDeps_Help[];

extern const char kBundleRootDir[];
extern const char kBundleRootDir_HelpShort[];
extern const char kBundleRootDir_Help[];

extern const char kBundleContentsDir[];
extern const char kBundleContentsDir_HelpShort[];
extern const char kBundleContentsDir_Help[];

extern const char kBundleResourcesDir[];
extern const char kBundleResourcesDir_HelpShort[];
extern const char kBundleResourcesDir_Help[];

extern const char kBundleDepsFilter[];
extern const char kBundleDepsFilter_HelpShort[];
extern const char kBundleDepsFilter_Help[];

extern const char kBundleExecutableDir[];
extern const char kBundleExecutableDir_HelpShort[];
extern const char kBundleExecutableDir_Help[];

extern const char kBundlePlugInsDir[];
extern const char kBundlePlugInsDir_HelpShort[];
extern const char kBundlePlugInsDir_Help[];

extern const char kCflags[];
extern const char kCflags_HelpShort[];
extern const char* kCflags_Help;

extern const char kCflagsC[];
extern const char kCflagsC_HelpShort[];
extern const char* kCflagsC_Help;

extern const char kCflagsCC[];
extern const char kCflagsCC_HelpShort[];
extern const char* kCflagsCC_Help;

extern const char kCflagsObjC[];
extern const char kCflagsObjC_HelpShort[];
extern const char* kCflagsObjC_Help;

extern const char kCflagsObjCC[];
extern const char kCflagsObjCC_HelpShort[];
extern const char* kCflagsObjCC_Help;

extern const char kCheckIncludes[];
extern const char kCheckIncludes_HelpShort[];
extern const char kCheckIncludes_Help[];

extern const char kCodeSigningArgs[];
extern const char kCodeSigningArgs_HelpShort[];
extern const char kCodeSigningArgs_Help[];

extern const char kCodeSigningScript[];
extern const char kCodeSigningScript_HelpShort[];
extern const char kCodeSigningScript_Help[];

extern const char kCodeSigningSources[];
extern const char kCodeSigningSources_HelpShort[];
extern const char kCodeSigningSources_Help[];

extern const char kCodeSigningOutputs[];
extern const char kCodeSigningOutputs_HelpShort[];
extern const char kCodeSigningOutputs_Help[];

extern const char kCompleteStaticLib[];
extern const char kCompleteStaticLib_HelpShort[];
extern const char kCompleteStaticLib_Help[];

extern const char kConfigs[];
extern const char kConfigs_HelpShort[];
extern const char kConfigs_Help[];

extern const char kData[];
extern const char kData_HelpShort[];
extern const char kData_Help[];

extern const char kDataDeps[];
extern const char kDataDeps_HelpShort[];
extern const char kDataDeps_Help[];

extern const char kDefines[];
extern const char kDefines_HelpShort[];
extern const char kDefines_Help[];

extern const char kDepfile[];
extern const char kDepfile_HelpShort[];
extern const char kDepfile_Help[];

extern const char kDeps[];
extern const char kDeps_HelpShort[];
extern const char kDeps_Help[];

extern const char kFriend[];
extern const char kFriend_HelpShort[];
extern const char kFriend_Help[];

extern const char kIncludeDirs[];
extern const char kIncludeDirs_HelpShort[];
extern const char kIncludeDirs_Help[];

extern const char kInputs[];
extern const char kInputs_HelpShort[];
extern const char kInputs_Help[];

extern const char kLdflags[];
extern const char kLdflags_HelpShort[];
extern const char kLdflags_Help[];

extern const char kLibDirs[];
extern const char kLibDirs_HelpShort[];
extern const char kLibDirs_Help[];

extern const char kLibs[];
extern const char kLibs_HelpShort[];
extern const char kLibs_Help[];

extern const char kOutputDir[];
extern const char kOutputDir_HelpShort[];
extern const char kOutputDir_Help[];

extern const char kOutputExtension[];
extern const char kOutputExtension_HelpShort[];
extern const char kOutputExtension_Help[];

extern const char kOutputName[];
extern const char kOutputName_HelpShort[];
extern const char kOutputName_Help[];

extern const char kOutputPrefixOverride[];
extern const char kOutputPrefixOverride_HelpShort[];
extern const char kOutputPrefixOverride_Help[];

extern const char kOutputs[];
extern const char kOutputs_HelpShort[];
extern const char kOutputs_Help[];

extern const char kPartialInfoPlist[];
extern const char kPartialInfoPlist_HelpShort[];
extern const char kPartialInfoPlist_Help[];

extern const char kPool[];
extern const char kPool_HelpShort[];
extern const char kPool_Help[];

extern const char kPrecompiledHeader[];
extern const char kPrecompiledHeader_HelpShort[];
extern const char kPrecompiledHeader_Help[];

extern const char kPrecompiledHeaderType[];
extern const char kPrecompiledHeaderType_HelpShort[];
extern const char kPrecompiledHeaderType_Help[];

extern const char kPrecompiledSource[];
extern const char kPrecompiledSource_HelpShort[];
extern const char kPrecompiledSource_Help[];

extern const char kProductType[];
extern const char kProductType_HelpShort[];
extern const char kProductType_Help[];

extern const char kPublic[];
extern const char kPublic_HelpShort[];
extern const char kPublic_Help[];

extern const char kPublicConfigs[];
extern const char kPublicConfigs_HelpShort[];
extern const char kPublicConfigs_Help[];

extern const char kPublicDeps[];
extern const char kPublicDeps_HelpShort[];
extern const char kPublicDeps_Help[];

extern const char kResponseFileContents[];
extern const char kResponseFileContents_HelpShort[];
extern const char kResponseFileContents_Help[];

extern const char kScript[];
extern const char kScript_HelpShort[];
extern const char kScript_Help[];

extern const char kSources[];
extern const char kSources_HelpShort[];
extern const char kSources_Help[];

extern const char kXcodeTestApplicationName[];
extern const char kXcodeTestApplicationName_HelpShort[];
extern const char kXcodeTestApplicationName_Help[];

extern const char kTestonly[];
extern const char kTestonly_HelpShort[];
extern const char kTestonly_Help[];

extern const char kVisibility[];
extern const char kVisibility_HelpShort[];
extern const char kVisibility_Help[];

extern const char kWriteRuntimeDeps[];
extern const char kWriteRuntimeDeps_HelpShort[];
extern const char kWriteRuntimeDeps_Help[];

extern const char kXcodeExtraAttributes[];
extern const char kXcodeExtraAttributes_HelpShort[];
extern const char kXcodeExtraAttributes_Help[];

// -----------------------------------------------------------------------------

struct VariableInfo {
  VariableInfo();
  VariableInfo(const char* in_help_short, const char* in_help);

  const char* help_short;
  const char* help;
};

typedef std::map<base::StringPiece, VariableInfo> VariableInfoMap;

// Returns the built-in readonly variables.
// Note: this is used only for help so this getter is not threadsafe.
const VariableInfoMap& GetBuiltinVariables();

// Returns the variables used by target generators.
// Note: this is used only for help so this getter is not threadsafe.
const VariableInfoMap& GetTargetVariables();

}  // namespace variables

#endif  // TOOLS_GN_VARIABLES_H_
