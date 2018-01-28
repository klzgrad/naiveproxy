// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_SUBSTITUTION_TYPE_H_
#define TOOLS_GN_SUBSTITUTION_TYPE_H_

#include <vector>

class Err;
class ParseNode;

// Keep kSubstitutionNames, kSubstitutionNinjaNames and the
// IsValid*Substitution functions in sync if you change anything here.
enum SubstitutionType {
  SUBSTITUTION_LITERAL = 0,

  // The index of the first pattern. To loop overal all patterns, go from here
  // until NUM_TYPES.
  SUBSTITUTION_FIRST_PATTERN,

  // These map to Ninja's {in} and {out} variables.
  SUBSTITUTION_SOURCE = SUBSTITUTION_FIRST_PATTERN,  // {{source}}
  SUBSTITUTION_OUTPUT,                               // {{output}}

  // Valid for all compiler tools.
  SUBSTITUTION_SOURCE_NAME_PART,          // {{source_name_part}}
  SUBSTITUTION_SOURCE_FILE_PART,          // {{source_file_part}}
  SUBSTITUTION_SOURCE_DIR,                // {{source_dir}}
  SUBSTITUTION_SOURCE_ROOT_RELATIVE_DIR,  // {{root_relative_dir}}
  SUBSTITUTION_SOURCE_GEN_DIR,            // {{source_gen_dir}}
  SUBSTITUTION_SOURCE_OUT_DIR,            // {{source_out_dir}}
  SUBSTITUTION_SOURCE_TARGET_RELATIVE,    // {{source_target_relative}}

  // Valid for all compiler and linker tools. These depend on the target and
  // do not vary on a per-file basis.
  SUBSTITUTION_LABEL,               // {{label}}
  SUBSTITUTION_LABEL_NAME,          // {{label_name}}
  SUBSTITUTION_ROOT_GEN_DIR,        // {{root_gen_dir}}
  SUBSTITUTION_ROOT_OUT_DIR,        // {{root_out_dir}}
  SUBSTITUTION_TARGET_GEN_DIR,      // {{target_gen_dir}}
  SUBSTITUTION_TARGET_OUT_DIR,      // {{target_out_dir}}
  SUBSTITUTION_TARGET_OUTPUT_NAME,  // {{target_output_name}}

  // Valid for compiler tools.
  SUBSTITUTION_ASMFLAGS,      // {{asmflags}}
  SUBSTITUTION_CFLAGS,        // {{cflags}}
  SUBSTITUTION_CFLAGS_C,      // {{cflags_c}}
  SUBSTITUTION_CFLAGS_CC,     // {{cflags_cc}}
  SUBSTITUTION_CFLAGS_OBJC,   // {{cflags_objc}}
  SUBSTITUTION_CFLAGS_OBJCC,  // {{cflags_objcc}}
  SUBSTITUTION_DEFINES,       // {{defines}}
  SUBSTITUTION_INCLUDE_DIRS,  // {{include_dirs}}

  // Valid for linker tools.
  SUBSTITUTION_LINKER_INPUTS,          // {{inputs}}
  SUBSTITUTION_LINKER_INPUTS_NEWLINE,  // {{inputs_newline}}
  SUBSTITUTION_LDFLAGS,                // {{ldflags}}
  SUBSTITUTION_LIBS,                   // {{libs}}
  SUBSTITUTION_OUTPUT_DIR,             // {{output_dir}}
  SUBSTITUTION_OUTPUT_EXTENSION,       // {{output_extension}}
  SUBSTITUTION_SOLIBS,                 // {{solibs}}

  // Valid for alink only.
  SUBSTITUTION_ARFLAGS,  // {{arflags}}

  // Valid for bundle_data targets.
  SUBSTITUTION_BUNDLE_ROOT_DIR,        // {{bundle_root_dir}}
  SUBSTITUTION_BUNDLE_CONTENTS_DIR,    // {{bundle_contents_dir}}
  SUBSTITUTION_BUNDLE_RESOURCES_DIR,   // {{bundle_resources_dir}}
  SUBSTITUTION_BUNDLE_EXECUTABLE_DIR,  // {{bundle_executable_dir}}
  SUBSTITUTION_BUNDLE_PLUGINS_DIR,     // {{bundle_plugins_dir}}

  // Valid for compile_xcassets tool.
  SUBSTITUTION_BUNDLE_PRODUCT_TYPE,        // {{bundle_product_type}}
  SUBSTITUTION_BUNDLE_PARTIAL_INFO_PLIST,  // {{bundle_partial_info_plist}}

  // Used only for the args of actions.
  SUBSTITUTION_RSP_FILE_NAME,  // {{response_file_name}}

  SUBSTITUTION_NUM_TYPES  // Must be last.
};

// An array of size SUBSTITUTION_NUM_TYPES that lists the names of the
// substitution patterns, including the curly braces. So, for example,
// kSubstitutionNames[SUBSTITUTION_SOURCE] == "{{source}}".
extern const char* kSubstitutionNames[SUBSTITUTION_NUM_TYPES];

// Ninja variables corresponding to each substitution. These do not include
// the dollar sign.
extern const char* kSubstitutionNinjaNames[SUBSTITUTION_NUM_TYPES];

// A wrapper around an array if flags indicating whether a given substitution
// type is required in some context. By convention, the LITERAL type bit is
// not set.
struct SubstitutionBits {
  SubstitutionBits();

  // Merges any bits set in the given "other" to this one. This object will
  // then be the union of all bits in the two lists.
  void MergeFrom(const SubstitutionBits& other);

  // Converts the substitution type bitfield (with a true set for each required
  // item) to a vector of the types listed. Does not include LITERAL.
  void FillVector(std::vector<SubstitutionType>* vect) const;

  bool used[SUBSTITUTION_NUM_TYPES];
};

// Returns true if the given substitution pattern references the output
// directory. This is used to check strings that begin with a substitution to
// verify that they produce a file in the output directory.
bool SubstitutionIsInOutputDir(SubstitutionType type);

// Returns true if the given substitution pattern references the bundle
// directory. This is used to check strings that begin with a substitution to
// verify that they produce a file in the bundle directory.
bool SubstitutionIsInBundleDir(SubstitutionType type);

// Returns true if the given substitution is valid for the named purpose.
bool IsValidBundleDataSubstitution(SubstitutionType type);
bool IsValidSourceSubstitution(SubstitutionType type);
bool IsValidScriptArgsSubstitution(SubstitutionType type);

// Both compiler and linker tools.
bool IsValidToolSubstitution(SubstitutionType type);
bool IsValidCompilerSubstitution(SubstitutionType type);
bool IsValidCompilerOutputsSubstitution(SubstitutionType type);
bool IsValidLinkerSubstitution(SubstitutionType type);
bool IsValidLinkerOutputsSubstitution(SubstitutionType type);
bool IsValidALinkSubstitution(SubstitutionType type);
bool IsValidCopySubstitution(SubstitutionType type);
bool IsValidCompileXCassetsSubstitution(SubstitutionType type);

// Validates that each substitution type in the vector passes the given
// is_valid_subst predicate. Returns true on success. On failure, fills in the
// error object with an appropriate message and returns false.
bool EnsureValidSubstitutions(
    const std::vector<SubstitutionType>& types,
    bool (*is_valid_subst)(SubstitutionType),
    const ParseNode* origin,
    Err* err);

#endif  // TOOLS_GN_SUBSTITUTION_TYPE_H_
