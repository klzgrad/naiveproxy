// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_ARGS_H_
#define TOOLS_GN_ARGS_H_

#include <map>
#include <mutex>
#include <set>
#include <unordered_map>

#include "base/macros.h"
#include "tools/gn/scope.h"

class Err;
class SourceFile;

extern const char kBuildArgs_Help[];

// Manages build arguments. It stores the global arguments specified on the
// command line, and sets up the root scope with the proper values.
//
// This class tracks accesses so we can report errors about unused variables.
// The use case is if the user specifies an override on the command line, but
// no buildfile actually uses that variable. We want to be able to report that
// the argument was unused.
class Args {
 public:
  struct ValueWithOverride {
    ValueWithOverride();
    ValueWithOverride(const Value& def_val);
    ~ValueWithOverride();

    Value default_value;  // Default value given in declare_args.

    bool has_override;     // True indicates override_value is valid.
    Value override_value;  // From .gn or the current build's "gn args".
  };
  using ValueWithOverrideMap = std::map<base::StringPiece, ValueWithOverride>;

  Args();
  Args(const Args& other);
  ~Args();

  // Specifies overrides of the build arguments. These are normally specified
  // on the command line.
  void AddArgOverride(const char* name, const Value& value);
  void AddArgOverrides(const Scope::KeyValueMap& overrides);

  // Specifies default overrides of the build arguments. These are normally
  // specified in the .gn file.
  void AddDefaultArgOverrides(const Scope::KeyValueMap& overrides);

  // Returns the value corresponding to the given argument name, or NULL if no
  // argument is set.
  const Value* GetArgOverride(const char* name) const;

  // Sets up the root scope for a toolchain. This applies the default system
  // flags and saves the toolchain overrides so they can be applied to
  // declare_args blocks that appear when loading files in that toolchain.
  void SetupRootScope(Scope* dest,
                      const Scope::KeyValueMap& toolchain_overrides) const;

  // Sets up the given scope with arguments passed in.
  //
  // If the values specified in the args are not already set, the values in
  // the args list will be used (which are assumed to be the defaults), but
  // they will not override the system defaults or the current overrides.
  //
  // All args specified in the input will be marked as "used".
  //
  // On failure, the err will be set and it will return false.
  bool DeclareArgs(const Scope::KeyValueMap& args,
                   Scope* scope_to_set,
                   Err* err) const;

  // Checks to see if any of the overrides ever used were never declared as
  // arguments. If there are, this returns false and sets the error.
  bool VerifyAllOverridesUsed(Err* err) const;

  // Returns information about all arguments, both defaults and overrides.
  // This is used for the help system which is not performance critical. Use a
  // map instead of a hash map so the arguments are sorted alphabetically.
  ValueWithOverrideMap GetAllArguments() const;

  // Returns the set of build files that may affect the build arguments, please
  // refer to Scope for how this is determined.
  const std::set<SourceFile>& build_args_dependency_files() const {
    return build_args_dependency_files_;
  }

  void set_build_args_dependency_files(
      const std::set<SourceFile>& build_args_dependency_files) {
    build_args_dependency_files_ = build_args_dependency_files;
  }

 private:
  using ArgumentsPerToolchain =
      std::unordered_map<const Settings*, Scope::KeyValueMap>;

  // Sets the default config based on the current system.
  void SetSystemVarsLocked(Scope* scope) const;

  // Sets the given already declared vars on the given scope.
  void ApplyOverridesLocked(const Scope::KeyValueMap& values,
                            Scope* scope) const;

  void SaveOverrideRecordLocked(const Scope::KeyValueMap& values) const;

  // Returns the KeyValueMap used for arguments declared for the specified
  // toolchain.
  Scope::KeyValueMap& DeclaredArgumentsForToolchainLocked(Scope* scope) const;

  // Returns the KeyValueMap used for overrides for the specified
  // toolchain.
  Scope::KeyValueMap& OverridesForToolchainLocked(Scope* scope) const;

  // Since this is called during setup which we assume is single-threaded,
  // this is not protected by the lock. It should be set only during init.
  Scope::KeyValueMap overrides_;

  mutable std::mutex lock_;

  // Maintains a list of all overrides we've ever seen. This is the main
  // |overrides_| as well as toolchain overrides. Tracking this allows us to
  // check for overrides that were specified but never used.
  mutable Scope::KeyValueMap all_overrides_;

  // Maps from Settings (which corresponds to a toolchain) to the map of
  // declared variables. This is used to tracks all variables declared in any
  // buildfile. This is so we can see if the user set variables on the command
  // line that are not used anywhere. Each map is toolchain specific as each
  // toolchain may define variables in different locations.
  mutable ArgumentsPerToolchain declared_arguments_per_toolchain_;

  // Overrides for individual toolchains. This is necessary so we
  // can apply the correct override for the current toolchain, once
  // we see an argument declaration.
  mutable ArgumentsPerToolchain toolchain_overrides_;

  std::set<SourceFile> build_args_dependency_files_;

  DISALLOW_ASSIGN(Args);
};

#endif  // TOOLS_GN_ARGS_H_
