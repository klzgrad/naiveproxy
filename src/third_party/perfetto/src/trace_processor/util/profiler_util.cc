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

#include "src/trace_processor/util/profiler_util.h"

#include <cstddef>
#include <optional>
#include <string>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"

#include "protos/perfetto/trace/profiling/deobfuscation.pbzero.h"

namespace perfetto::trace_processor {
namespace {

// Try to extract the package name from a path like:
// * /data/app/[packageName]-[randomString]/base.apk
// * /data/app/~~[randomStringA]/[packageName]-[randomStringB]/base.apk
// The latter is newer (R+), and was added to avoid leaking package names via
// mountinfo of incremental apk mounts.
std::optional<base::StringView> PackageFromApp(base::StringView location) {
  location = location.substr(base::StringView("/data/app/").size());
  size_t start = 0;
  if (location.at(0) == '~') {
    size_t slash = location.find('/');
    if (slash == base::StringView::npos) {
      return std::nullopt;
    }
    start = slash + 1;
  }
  size_t end = location.find('/', start + 1);
  if (end == base::StringView::npos) {
    return std::nullopt;
  }
  location = location.substr(start, end);
  size_t minus = location.find('-');
  if (minus == base::StringView::npos) {
    return std::nullopt;
  }
  return location.substr(0, minus);
}

}  // namespace

std::optional<std::string> PackageFromLocation(TraceStorage* storage,
                                               base::StringView location) {
  // List of some hardcoded apps that do not follow the scheme used in
  // PackageFromApp. Ask for yours to be added.
  //
  // TODO(b/153632336): Get rid of the hardcoded list of system apps.
  base::StringView sysui(
      "/system_ext/priv-app/SystemUIGoogle/SystemUIGoogle.apk");
  if (location.size() >= sysui.size() &&
      location.substr(0, sysui.size()) == sysui) {
    return "com.android.systemui";
  }

  base::StringView phonesky("/product/priv-app/Phonesky/Phonesky.apk");
  if (location.size() >= phonesky.size() &&
      location.substr(0, phonesky.size()) == phonesky) {
    return "com.android.vending";
  }

  base::StringView maps("/product/app/Maps/Maps.apk");
  if (location.size() >= maps.size() &&
      location.substr(0, maps.size()) == maps) {
    return "com.google.android.apps.maps";
  }

  base::StringView launcher(
      "/system_ext/priv-app/NexusLauncherRelease/NexusLauncherRelease.apk");
  if (location.size() >= launcher.size() &&
      location.substr(0, launcher.size()) == launcher) {
    return "com.google.android.apps.nexuslauncher";
  }

  base::StringView photos("/product/app/Photos/Photos.apk");
  if (location.size() >= photos.size() &&
      location.substr(0, photos.size()) == photos) {
    return "com.google.android.apps.photos";
  }

  base::StringView wellbeing(
      "/product/priv-app/WellbeingPrebuilt/WellbeingPrebuilt.apk");
  if (location.size() >= wellbeing.size() &&
      location.substr(0, wellbeing.size()) == wellbeing) {
    return "com.google.android.apps.wellbeing";
  }

  if (location.find("DevicePersonalizationPrebuilt") !=
          base::StringView::npos ||
      location.find("MatchMaker") != base::StringView::npos) {
    return "com.google.android.as";
  }

  if (location.find("DeviceIntelligenceNetworkPrebuilt") !=
      base::StringView::npos) {
    return "com.google.android.as.oss";
  }

  if (location.find("SettingsIntelligenceGooglePrebuilt") !=
      base::StringView::npos) {
    return "com.google.android.settings.intelligence";
  }

  base::StringView gm("/product/app/PrebuiltGmail/PrebuiltGmail.apk");
  if (location.size() >= gm.size() && location.substr(0, gm.size()) == gm) {
    return "com.google.android.gm";
  }

  if (location.find("PrebuiltGmsCore") != base::StringView::npos ||
      location.find("com.google.android.gms") != base::StringView::npos) {
    return "com.google.android.gms";
  }

  base::StringView velvet("/product/priv-app/Velvet/Velvet.apk");
  if (location.size() >= velvet.size() &&
      location.substr(0, velvet.size()) == velvet) {
    return "com.google.android.googlequicksearchbox";
  }

  base::StringView inputmethod(
      "/product/app/LatinIMEGooglePrebuilt/LatinIMEGooglePrebuilt.apk");
  if (location.size() >= inputmethod.size() &&
      location.substr(0, inputmethod.size()) == inputmethod) {
    return "com.google.android.inputmethod.latin";
  }

  base::StringView messaging("/product/app/PrebuiltBugle/PrebuiltBugle.apk");
  if (location.size() >= messaging.size() &&
      location.substr(0, messaging.size()) == messaging) {
    return "com.google.android.apps.messaging";
  }

  // Deal with paths to /data/app/...

  auto extract_package =
      [storage](base::StringView path) -> std::optional<std::string> {
    auto package = PackageFromApp(path);
    if (!package) {
      PERFETTO_DLOG("Failed to parse %s", path.ToStdString().c_str());
      storage->IncrementStats(stats::deobfuscate_location_parse_error);
      return std::nullopt;
    }
    return package->ToStdString();
  };

  base::StringView data_app("/data/app/");
  size_t data_app_sz = data_app.size();
  if (location.substr(0, data_app.size()) == data_app) {
    return extract_package(location);
  }

  // Check for in-memory decompressed dexfile, example prefixes:
  // * "[anon:dalvik-classes.dex extracted in memory from"
  // * "/dev/ashmem/dalvik-classes.dex extracted in memory from"
  // The latter form is for older devices (Android P and before).
  // We cannot hardcode the filename since it could be for example
  // "classes2.dex" for multidex apks.
  base::StringView inmem_dex("dex extracted in memory from /data/app/");
  size_t match_pos = location.find(inmem_dex);
  if (match_pos != base::StringView::npos) {
    auto data_app_path =
        location.substr(match_pos + inmem_dex.size() - data_app_sz);
    return extract_package(data_app_path);
  }

  return std::nullopt;
}

std::string FullyQualifiedDeobfuscatedName(
    protos::pbzero::ObfuscatedClass::Decoder& cls,
    protos::pbzero::ObfuscatedMember::Decoder& member) {
  std::string member_deobfuscated_name =
      member.deobfuscated_name().ToStdString();
  if (base::Contains(member_deobfuscated_name, '.')) {
    // Fully qualified name.
    return member_deobfuscated_name;
  } else {
    // Name relative to class.
    return cls.deobfuscated_name().ToStdString() + "." +
           member_deobfuscated_name;
  }
}

}  // namespace perfetto::trace_processor
