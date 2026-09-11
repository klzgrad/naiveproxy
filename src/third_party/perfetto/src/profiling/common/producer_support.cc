/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/profiling/common/producer_support.h"

#include <algorithm>
#include <optional>

#include "perfetto/ext/base/android_utils.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/tracing/core/data_source_config.h"
#include "src/traced/probes/packages_list/packages_list_parser.h"

namespace perfetto {
namespace profiling {

namespace {
std::optional<Package> FindInPackagesList(
    uint64_t lookup_uid,
    const std::string& packages_list_path) {
  std::string content;
  if (!base::ReadFile(packages_list_path, &content)) {
    PERFETTO_ELOG("Failed to read %s", packages_list_path.c_str());
    return std::nullopt;
  }
  for (base::StringSplitter ss(std::move(content), '\n'); ss.Next();) {
    Package pkg;
    if (!ReadPackagesListLine(ss.cur_token(), &pkg)) {
      PERFETTO_ELOG("Failed to parse packages.list");
      return std::nullopt;
    }

    if (pkg.uid == lookup_uid) {
      return pkg;
    }
  }
  return std::nullopt;
}

bool AllPackagesProfileableByTrustedInitiator(
    const std::string& packages_list_path) {
  std::string content;
  if (!base::ReadFile(packages_list_path, &content)) {
    PERFETTO_ELOG("Failed to read %s", packages_list_path.c_str());
    return false;
  }
  bool ret = true;
  for (base::StringSplitter ss(std::move(content), '\n'); ss.Next();) {
    Package pkg;
    if (!ReadPackagesListLine(ss.cur_token(), &pkg)) {
      PERFETTO_ELOG("Failed to parse packages.list");
      return false;
    }

    ret = ret && (pkg.profileable || pkg.debuggable);
  }
  return ret;
}

}  // namespace

bool CanProfile(const DataSourceConfig& ds_config,
                uint64_t uid,
                const std::vector<std::string>& installed_by) {
// We restrict by !PERFETTO_BUILDFLAG(PERFETTO_ANDROID_BUILD) because a
// sideloaded heapprofd should not be restricted by this. Do note though that,
// at the moment, there isn't really a way to sideload a functioning heapprofd
// onto user builds.
#if !PERFETTO_BUILDFLAG(PERFETTO_ANDROID_BUILD) || \
    !PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  base::ignore_result(ds_config);
  base::ignore_result(uid);
  base::ignore_result(installed_by);
  return true;
#else
  std::string build_type = base::GetAndroidProp("ro.build.type");
  return CanProfileAndroid(ds_config, uid, installed_by, build_type,
                           "/data/system/packages.list");
#endif
}

bool CanProfileAndroid(const DataSourceConfig& ds_config,
                       uint64_t uid,
                       const std::vector<std::string>& installed_by,
                       const std::string& build_type,
                       const std::string& packages_list_path) {
  // These constants are replicated from libcutils android_filesystem_config.h,
  // to allow for building and testing the profilers outside the android tree.
  constexpr auto kAidUserOffset = 100000;      // AID_USER_OFFSET
  constexpr auto kAidAppStart = 10000;         // AID_APP_START
  constexpr auto kAidAppEnd = 19999;           // AID_APP_END
  constexpr auto kAidSdkSandboxStart = 20000;  // AID_SDK_SANDBOX_PROCESS_START
  constexpr auto kAidSdkSandboxEnd = 29999;    // AID_SDK_SANDBOX_PROCESS_END
  constexpr auto kAidIsolatedStart = 90000;    // AID_ISOLATED_START
  constexpr auto kAidIsolatedEnd = 99999;      // AID_ISOLATED_END

  if (!build_type.empty() && build_type != "user") {
    return true;
  }

  bool trusted_initiator = ds_config.session_initiator() ==
                           DataSourceConfig::SESSION_INITIATOR_TRUSTED_SYSTEM;

  uint64_t uid_without_profile = uid % kAidUserOffset;
  uint64_t uid_for_lookup = 0;
  if (uid_without_profile < kAidAppStart) {
    // Platform processes are considered profileable by the platform itself.
    // This includes platform UIDs from other profiles, e.g. "u10_system".
    // It's possible that this is an app (e.g. com.android.settings runs as
    // AID_SYSTEM), but we will skip checking packages.list for the profileable
    // manifest flags, as running under a platform UID is considered sufficient.
    // Minor consequence: shell cannot profile platform apps, even if their
    // manifest flags opt into profiling from shell. Resolving this would
    // require definitively disambiguating native processes from apps if both
    // can run as the same platform UID.
    return trusted_initiator;

  } else if (uid_without_profile >= kAidAppStart &&
             uid_without_profile <= kAidAppEnd) {
    // normal app
    uid_for_lookup = uid_without_profile;

  } else if (uid_without_profile >= kAidSdkSandboxStart &&
             uid_without_profile <= kAidSdkSandboxEnd) {
    // sdk sandbox process, has deterministic mapping to corresponding app
    uint64_t sdk_sandbox_offset = kAidSdkSandboxStart - kAidAppStart;
    uid_for_lookup = uid_without_profile - sdk_sandbox_offset;

  } else if (uid_without_profile >= kAidIsolatedStart &&
             uid_without_profile <= kAidIsolatedEnd) {
    // Isolated process. Such processes run under random UIDs and have no
    // straightforward link to the original app's UID without consulting
    // system_server. So we have to perform a very conservative check - if *all*
    // packages are profileable, then any isolated process must be profileable
    // as well, regardless of which package it's running for (which might not
    // even be the package in which the service was defined).
    // TODO(rsavitski): find a way for the platform to tell native services
    // about isolated<->app relations.
    return trusted_initiator &&
           AllPackagesProfileableByTrustedInitiator(packages_list_path);

  } else {
    // disallow everything else on release builds
    return false;
  }

  std::optional<Package> pkg =
      FindInPackagesList(uid_for_lookup, packages_list_path);

  if (!pkg)
    return false;

  // check installer constraint if given
  if (!installed_by.empty()) {
    if (pkg->installed_by.empty()) {
      PERFETTO_ELOG("Cannot parse installer from packages.list");
      return false;
    }
    if (std::find(installed_by.cbegin(), installed_by.cend(),
                  pkg->installed_by) == installed_by.cend()) {
      // not installed by one of the requested origins
      return false;
    }
  }

  switch (ds_config.session_initiator()) {
    case DataSourceConfig::SESSION_INITIATOR_UNSPECIFIED:
      return pkg->profileable_from_shell || pkg->debuggable;
    case DataSourceConfig::SESSION_INITIATOR_TRUSTED_SYSTEM:
      return pkg->profileable || pkg->debuggable;
  }
  return false;
}

}  // namespace profiling
}  // namespace perfetto
