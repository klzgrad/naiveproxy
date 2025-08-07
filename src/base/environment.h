// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_ENVIRONMENT_H_
#define BASE_ENVIRONMENT_H_

#include <map>
#include <memory>
#include <optional>
#include <string>

#include "base/base_export.h"
#include "base/strings/cstring_view.h"
#include "build/build_config.h"

namespace base {

namespace env_vars {

#if BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
// On Posix systems, this variable contains the location of the user's home
// directory. (e.g, /home/username/).
inline constexpr char kHome[] = "HOME";
#endif

}  // namespace env_vars

class BASE_EXPORT Environment {
 public:
  virtual ~Environment();

  // Returns the appropriate platform-specific instance.
  static std::unique_ptr<Environment> Create();

  // Returns an environment variable's value.
  // Returns std::nullopt if the key is unset.
  // Note that the variable may be set to an empty string.
  virtual std::optional<std::string> GetVar(cstring_view variable_name) = 0;

  // Syntactic sugar for GetVar(variable_name).has_value();
  bool HasVar(cstring_view variable_name);

  // Returns true on success, otherwise returns false. This method should not
  // be called in a multi-threaded process.
  virtual bool SetVar(cstring_view variable_name,
                      const std::string& new_value) = 0;

  // Returns true on success, otherwise returns false. This method should not
  // be called in a multi-threaded process.
  virtual bool UnSetVar(cstring_view variable_name) = 0;
};

#if BUILDFLAG(IS_WIN)
using NativeEnvironmentString = std::wstring;
#elif BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
using NativeEnvironmentString = std::string;
#endif
using EnvironmentMap =
    std::map<NativeEnvironmentString, NativeEnvironmentString>;

}  // namespace base

#endif  // BASE_ENVIRONMENT_H_
