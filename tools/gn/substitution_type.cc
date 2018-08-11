// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/substitution_type.h"

#include <stddef.h>
#include <stdlib.h>

#include "tools/gn/err.h"

const char* kSubstitutionNames[SUBSTITUTION_NUM_TYPES] = {
    "<<literal>>",  // SUBSTITUTION_LITERAL

    "{{source}}",  // SUBSTITUTION_SOURCE
    "{{output}}",  // SUBSTITUTION_OUTPUT

    "{{source_name_part}}",          // SUBSTITUTION_NAME_PART
    "{{source_file_part}}",          // SUBSTITUTION_FILE_PART
    "{{source_dir}}",                // SUBSTITUTION_SOURCE_DIR
    "{{source_root_relative_dir}}",  // SUBSTITUTION_SOURCE_ROOT_RELATIVE_DIR
    "{{source_gen_dir}}",            // SUBSTITUTION_SOURCE_GEN_DIR
    "{{source_out_dir}}",            // SUBSTITUTION_SOURCE_OUT_DIR
    "{{source_target_relative}}",    // SUBSTITUTION_SOURCE_TARGET_RELATIVE

    "{{label}}",               // SUBSTITUTION_LABEL
    "{{label_name}}",          // SUBSTITUTION_LABEL_NAME
    "{{root_gen_dir}}",        // SUBSTITUTION_ROOT_GEN_DIR
    "{{root_out_dir}}",        // SUBSTITUTION_ROOT_OUT_DIR
    "{{target_gen_dir}}",      // SUBSTITUTION_TARGET_GEN_DIR
    "{{target_out_dir}}",      // SUBSTITUTION_TARGET_OUT_DIR
    "{{target_output_name}}",  // SUBSTITUTION_TARGET_OUTPUT_NAME

    "{{asmflags}}",      // SUBSTITUTION_ASMFLAGS
    "{{cflags}}",        // SUBSTITUTION_CFLAGS
    "{{cflags_c}}",      // SUBSTITUTION_CFLAGS_C
    "{{cflags_cc}}",     // SUBSTITUTION_CFLAGS_CC
    "{{cflags_objc}}",   // SUBSTITUTION_CFLAGS_OBJC
    "{{cflags_objcc}}",  // SUBSTITUTION_CFLAGS_OBJCC
    "{{defines}}",       // SUBSTITUTION_DEFINES
    "{{include_dirs}}",  // SUBSTITUTION_INCLUDE_DIRS

    "{{inputs}}",            // SUBSTITUTION_LINKER_INPUTS
    "{{inputs_newline}}",    // SUBSTITUTION_LINKER_INPUTS_NEWLINE
    "{{ldflags}}",           // SUBSTITUTION_LDFLAGS
    "{{libs}}",              // SUBSTITUTION_LIBS
    "{{output_dir}}",        // SUBSTITUTION_OUTPUT_DIR
    "{{output_extension}}",  // SUBSTITUTION_OUTPUT_EXTENSION
    "{{solibs}}",            // SUBSTITUTION_SOLIBS

    "{{arflags}}",  // SUBSTITUTION_ARFLAGS

    "{{bundle_root_dir}}",            // SUBSTITUTION_BUNDLE_ROOT_DIR
    "{{bundle_contents_dir}}",        // SUBSTITUTION_BUNDLE_CONTENTS_DIR
    "{{bundle_resources_dir}}",       // SUBSTITUTION_BUNDLE_RESOURCES_DIR
    "{{bundle_executable_dir}}",      // SUBSTITUTION_BUNDLE_EXECUTABLE_DIR
    "{{bundle_plugins_dir}}",         // SUBSTITUTION_BUNDLE_PLUGINS_DIR
    "{{bundle_product_type}}",        // SUBSTITUTION_BUNDLE_PRODUCT_TYPE
    "{{bundle_partial_info_plist}}",  // SUBSTITUTION_BUNDLE_PARTIAL_INFO_PLIST,

    "{{response_file_name}}",  // SUBSTITUTION_RSP_FILE_NAME
};

const char* kSubstitutionNinjaNames[SUBSTITUTION_NUM_TYPES] = {
    nullptr,  // SUBSTITUTION_LITERAL

    "in",   // SUBSTITUTION_SOURCE
    "out",  // SUBSTITUTION_OUTPUT

    "source_name_part",          // SUBSTITUTION_NAME_PART
    "source_file_part",          // SUBSTITUTION_FILE_PART
    "source_dir",                // SUBSTITUTION_SOURCE_DIR
    "source_root_relative_dir",  // SUBSTITUTION_SOURCE_ROOT_RELATIVE_DIR
    "source_gen_dir",            // SUBSTITUTION_SOURCE_GEN_DIR
    "source_out_dir",            // SUBSTITUTION_SOURCE_OUT_DIR
    "source_target_relative",    // SUBSTITUTION_SOURCE_TARGET_RELATIVE

    "label",               // SUBSTITUTION_LABEL
    "label_name",          // SUBSTITUTION_LABEL_NAME
    "root_gen_dir",        // SUBSTITUTION_ROOT_GEN_DIR
    "root_out_dir",        // SUBSTITUTION_ROOT_OUT_DIR
    "target_gen_dir",      // SUBSTITUTION_TARGET_GEN_DIR
    "target_out_dir",      // SUBSTITUTION_TARGET_OUT_DIR
    "target_output_name",  // SUBSTITUTION_TARGET_OUTPUT_NAME

    "asmflags",      // SUBSTITUTION_ASMFLAGS
    "cflags",        // SUBSTITUTION_CFLAGS
    "cflags_c",      // SUBSTITUTION_CFLAGS_C
    "cflags_cc",     // SUBSTITUTION_CFLAGS_CC
    "cflags_objc",   // SUBSTITUTION_CFLAGS_OBJC
    "cflags_objcc",  // SUBSTITUTION_CFLAGS_OBJCC
    "defines",       // SUBSTITUTION_DEFINES
    "include_dirs",  // SUBSTITUTION_INCLUDE_DIRS

    // LINKER_INPUTS expands to the same Ninja var as SUBSTITUTION_SOURCE. These
    // are used in different contexts and are named differently to keep things
    // clear, but they both expand to the "set of input files" for a build rule.
    "in",                // SUBSTITUTION_LINKER_INPUTS
    "in_newline",        // SUBSTITUTION_LINKER_INPUTS_NEWLINE
    "ldflags",           // SUBSTITUTION_LDFLAGS
    "libs",              // SUBSTITUTION_LIBS
    "output_dir",        // SUBSTITUTION_OUTPUT_DIR
    "output_extension",  // SUBSTITUTION_OUTPUT_EXTENSION
    "solibs",            // SUBSTITUTION_SOLIBS

    "arflags",  // SUBSTITUTION_ARFLAGS

    "bundle_root_dir",        // SUBSTITUTION_BUNDLE_ROOT_DIR
    "bundle_contents_dir",    // SUBSTITUTION_BUNDLE_CONTENTS_DIR
    "bundle_resources_dir",   // SUBSTITUTION_BUNDLE_RESOURCES_DIR
    "bundle_executable_dir",  // SUBSTITUTION_BUNDLE_EXECUTABLE_DIR
    "bundle_plugins_dir",     // SUBSTITUTION_BUNDLE_PLUGINS_DIR
    "product_type",           // SUBSTITUTION_BUNDLE_PRODUCT_TYPE
    "partial_info_plist",     // SUBSTITUTION_BUNDLE_PARTIAL_INFO_PLIST

    "rspfile",  // SUBSTITUTION_RSP_FILE_NAME
};

SubstitutionBits::SubstitutionBits() : used() {
}

void SubstitutionBits::MergeFrom(const SubstitutionBits& other) {
  for (size_t i = 0; i < SUBSTITUTION_NUM_TYPES; i++)
    used[i] |= other.used[i];
}

void SubstitutionBits::FillVector(std::vector<SubstitutionType>* vect) const {
  for (size_t i = SUBSTITUTION_FIRST_PATTERN; i < SUBSTITUTION_NUM_TYPES; i++) {
    if (used[i])
      vect->push_back(static_cast<SubstitutionType>(i));
  }
}

bool SubstitutionIsInOutputDir(SubstitutionType type) {
  return type == SUBSTITUTION_SOURCE_GEN_DIR ||
         type == SUBSTITUTION_SOURCE_OUT_DIR ||
         type == SUBSTITUTION_ROOT_GEN_DIR ||
         type == SUBSTITUTION_ROOT_OUT_DIR ||
         type == SUBSTITUTION_TARGET_GEN_DIR ||
         type == SUBSTITUTION_TARGET_OUT_DIR;
}

bool SubstitutionIsInBundleDir(SubstitutionType type) {
  return type == SUBSTITUTION_BUNDLE_ROOT_DIR ||
         type == SUBSTITUTION_BUNDLE_CONTENTS_DIR ||
         type == SUBSTITUTION_BUNDLE_RESOURCES_DIR ||
         type == SUBSTITUTION_BUNDLE_EXECUTABLE_DIR ||
         type == SUBSTITUTION_BUNDLE_PLUGINS_DIR;
}

bool IsValidBundleDataSubstitution(SubstitutionType type) {
  return type == SUBSTITUTION_LITERAL ||
         type == SUBSTITUTION_SOURCE_NAME_PART ||
         type == SUBSTITUTION_SOURCE_FILE_PART ||
         type == SUBSTITUTION_SOURCE_ROOT_RELATIVE_DIR ||
         type == SUBSTITUTION_BUNDLE_ROOT_DIR ||
         type == SUBSTITUTION_BUNDLE_CONTENTS_DIR ||
         type == SUBSTITUTION_BUNDLE_RESOURCES_DIR ||
         type == SUBSTITUTION_BUNDLE_EXECUTABLE_DIR ||
         type == SUBSTITUTION_BUNDLE_PLUGINS_DIR;
}

bool IsValidSourceSubstitution(SubstitutionType type) {
  return type == SUBSTITUTION_LITERAL ||
         type == SUBSTITUTION_SOURCE ||
         type == SUBSTITUTION_SOURCE_NAME_PART ||
         type == SUBSTITUTION_SOURCE_FILE_PART ||
         type == SUBSTITUTION_SOURCE_DIR ||
         type == SUBSTITUTION_SOURCE_ROOT_RELATIVE_DIR ||
         type == SUBSTITUTION_SOURCE_GEN_DIR ||
         type == SUBSTITUTION_SOURCE_OUT_DIR ||
         type == SUBSTITUTION_SOURCE_TARGET_RELATIVE;
}

bool IsValidScriptArgsSubstitution(SubstitutionType type) {
  return IsValidSourceSubstitution(type) ||
      type == SUBSTITUTION_RSP_FILE_NAME;
}

bool IsValidToolSubstitution(SubstitutionType type) {
  return type == SUBSTITUTION_LITERAL ||
         type == SUBSTITUTION_OUTPUT ||
         type == SUBSTITUTION_LABEL ||
         type == SUBSTITUTION_LABEL_NAME ||
         type == SUBSTITUTION_ROOT_GEN_DIR ||
         type == SUBSTITUTION_ROOT_OUT_DIR ||
         type == SUBSTITUTION_TARGET_GEN_DIR ||
         type == SUBSTITUTION_TARGET_OUT_DIR ||
         type == SUBSTITUTION_TARGET_OUTPUT_NAME;
}

bool IsValidCompilerSubstitution(SubstitutionType type) {
  return IsValidToolSubstitution(type) ||
         IsValidSourceSubstitution(type) ||
         type == SUBSTITUTION_SOURCE ||
         type == SUBSTITUTION_ASMFLAGS ||
         type == SUBSTITUTION_CFLAGS ||
         type == SUBSTITUTION_CFLAGS_C ||
         type == SUBSTITUTION_CFLAGS_CC ||
         type == SUBSTITUTION_CFLAGS_OBJC ||
         type == SUBSTITUTION_CFLAGS_OBJCC ||
         type == SUBSTITUTION_DEFINES ||
         type == SUBSTITUTION_INCLUDE_DIRS;
}

bool IsValidCompilerOutputsSubstitution(SubstitutionType type) {
  // All tool types except "output" (which would be infinitely recursive).
  return (IsValidToolSubstitution(type) && type != SUBSTITUTION_OUTPUT) ||
         IsValidSourceSubstitution(type);
}

bool IsValidLinkerSubstitution(SubstitutionType type) {
  return IsValidToolSubstitution(type) ||
         type == SUBSTITUTION_LINKER_INPUTS ||
         type == SUBSTITUTION_LINKER_INPUTS_NEWLINE ||
         type == SUBSTITUTION_LDFLAGS ||
         type == SUBSTITUTION_LIBS ||
         type == SUBSTITUTION_OUTPUT_DIR ||
         type == SUBSTITUTION_OUTPUT_EXTENSION ||
         type == SUBSTITUTION_SOLIBS;
}

bool IsValidLinkerOutputsSubstitution(SubstitutionType type) {
  // All valid compiler outputs plus the output extension.
  return IsValidCompilerOutputsSubstitution(type) ||
         type == SUBSTITUTION_OUTPUT_DIR ||
         type == SUBSTITUTION_OUTPUT_EXTENSION;
}

bool IsValidALinkSubstitution(SubstitutionType type) {
  return IsValidToolSubstitution(type) ||
         type == SUBSTITUTION_LINKER_INPUTS ||
         type == SUBSTITUTION_LINKER_INPUTS_NEWLINE ||
         type == SUBSTITUTION_ARFLAGS ||
         type == SUBSTITUTION_OUTPUT_DIR ||
         type == SUBSTITUTION_OUTPUT_EXTENSION;
}

bool IsValidCopySubstitution(SubstitutionType type) {
  return IsValidToolSubstitution(type) ||
         type == SUBSTITUTION_SOURCE;
}

bool IsValidCompileXCassetsSubstitution(SubstitutionType type) {
  return IsValidToolSubstitution(type) || type == SUBSTITUTION_LINKER_INPUTS ||
         type == SUBSTITUTION_BUNDLE_PRODUCT_TYPE ||
         type == SUBSTITUTION_BUNDLE_PARTIAL_INFO_PLIST;
}

bool EnsureValidSubstitutions(const std::vector<SubstitutionType>& types,
                              bool (*is_valid_subst)(SubstitutionType),
                              const ParseNode* origin,
                              Err* err) {
  for (SubstitutionType type : types) {
    if (!is_valid_subst(type)) {
      *err = Err(origin, "Invalid substitution type.",
          "The substitution " + std::string(kSubstitutionNames[type]) +
          " isn't valid for something\n"
          "operating on a source file such as this.");
      return false;
    }
  }
  return true;
}
