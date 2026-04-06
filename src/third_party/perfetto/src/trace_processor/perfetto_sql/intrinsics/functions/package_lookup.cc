// Copyright (C) 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/trace_processor/perfetto_sql/intrinsics/functions/package_lookup.h"

#include <cstdint>

#include "perfetto/ext/base/string_utils.h"
#include "src/trace_processor/containers/null_term_string_view.h"
#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_type.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"

namespace perfetto::trace_processor {
namespace {
const char* ResolveSystemPackage(uint32_t pkgid) {
  // clang-format off
  switch (pkgid) {
    case 0: return "AID_ROOT";
    case 1000: return "AID_SYSTEM_USER";
    case 1001: return "AID_RADIO";
    case 1002: return "AID_BLUETOOTH";
    case 1003: return "AID_GRAPHICS";
    case 1004: return "AID_INPUT";
    case 1005: return "AID_AUDIO";
    case 1006: return "AID_CAMERA";
    case 1007: return "AID_LOG";
    case 1008: return "AID_COMPASS";
    case 1009: return "AID_MOUNT";
    case 1010: return "AID_WIFI";
    case 1011: return "AID_ADB";
    case 1012: return "AID_INSTALL";
    case 1013: return "AID_MEDIA";
    case 1014: return "AID_DHCP";
    case 1015: return "AID_SDCARD_RW";
    case 1016: return "AID_VPN";
    case 1017: return "AID_KEYSTORE";
    case 1018: return "AID_USB";
    case 1019: return "AID_DRM";
    case 1020: return "AID_MDNSR";
    case 1021: return "AID_GPS";
    case 1022: return "AID_UNUSED1";
    case 1023: return "AID_MEDIA_RW";
    case 1024: return "AID_MTP";
    case 1025: return "AID_UNUSED2";
    case 1026: return "AID_DRMRPC";
    case 1027: return "AID_NFC";
    case 1028: return "AID_SDCARD_R";
    case 1029: return "AID_CLAT";
    case 1030: return "AID_LOOP_RADIO";
    case 1031: return "AID_MEDIA_DRM";
    case 1032: return "AID_PACKAGE_INFO";
    case 1033: return "AID_SDCARD_PICS";
    case 1034: return "AID_SDCARD_AV";
    case 1035: return "AID_SDCARD_ALL";
    case 1036: return "AID_LOGD";
    case 1037: return "AID_SHARED_RELRO";
    case 1038: return "AID_DBUS";
    case 1039: return "AID_TLSDATE";
    case 1040: return "AID_MEDIA_EX";
    case 1041: return "AID_AUDIOSERVER";
    case 1042: return "AID_METRICS_COLL";
    case 1043: return "AID_METRICSD";
    case 1044: return "AID_WEBSERV";
    case 1045: return "AID_DEBUGGERD";
    case 1046: return "AID_MEDIA_CODEC";
    case 1047: return "AID_CAMERASERVER";
    case 1048: return "AID_FIREWALL";
    case 1049: return "AID_TRUNKS";
    case 1050: return "AID_NVRAM";
    case 1051: return "AID_DNS";
    case 1052: return "AID_DNS_TETHER";
    case 1053: return "AID_WEBVIEW_ZYGOTE";
    case 1054: return "AID_VEHICLE_NETWORK";
    case 1055: return "AID_MEDIA_AUDIO";
    case 1056: return "AID_MEDIA_VIDEO";
    case 1057: return "AID_MEDIA_IMAGE";
    case 1058: return "AID_TOMBSTONED";
    case 1059: return "AID_MEDIA_OBB";
    case 1060: return "AID_ESE";
    case 1061: return "AID_OTA_UPDATE";
    case 1062: return "AID_AUTOMOTIVE_EVS";
    case 1063: return "AID_LOWPAN";
    case 1064: return "AID_HSM";
    case 1065: return "AID_RESERVED_DISK";
    case 1066: return "AID_STATSD";
    case 1067: return "AID_INCIDENTD";
    case 1068: return "AID_SECURE_ELEMENT";
    case 1069: return "AID_LMKD";
    case 1070: return "AID_LLKD";
    case 1071: return "AID_IORAPD";
    case 1072: return "AID_GPU_SERVICE";
    case 1073: return "AID_NETWORK_STACK";
    case 1074: return "AID_GSID";
    case 1075: return "AID_FSVERITY_CERT";
    case 1076: return "AID_CREDSTORE";
    case 1077: return "AID_EXTERNAL_STORAGE";
    case 1078: return "AID_EXT_DATA_RW";
    case 1079: return "AID_EXT_OBB_RW";
    case 1080: return "AID_CONTEXT_HUB";
    case 1081: return "AID_VIRTMANAGER";
    case 1082: return "AID_ARTD";
    case 1083: return "AID_UWB";
    case 1084: return "AID_THREAD_NETWORK";
    case 1085: return "AID_DICED";
    case 1086: return "AID_DMESGD";
    case 1087: return "AID_JC_WEAVER";
    case 1088: return "AID_JC_STRONGBOX";
    case 1089: return "AID_JC_IDENTITYCRED";
    case 1090: return "AID_SDK_SANDBOX";
    case 1091: return "AID_SECURITY_LOG_WRITER";
    case 1092: return "AID_PRNG_SEEDER";
    case 1093: return "AID_UPROBESTATS";
    case 2000: return "AID_SHELL";
    case 2001: return "AID_CACHE";
    case 2002: return "AID_DIAG";
    case 9999: return "AID_NOBODY";
  }
  // clang-format on

  if (pkgid >= 50000 && pkgid < 60000) {
    return "SHARED_GID";
  }

  if (pkgid >= 90000) {
    return "ISOLATED_UID";
  }

  return nullptr;
}
}  // namespace

// static
void PackageLookup::Step(sqlite3_context* ctx, int, sqlite3_value** argv) {
  sqlite::Type uid_value = sqlite::value::Type(argv[0]);

  if (uid_value == sqlite::Type::kNull) {
    return;
  }

  if (uid_value != sqlite::Type::kInteger) {
    return sqlite::result::Error(ctx, "PACKAGE_LOOKUP: uid must be an integer");
  }

  uint32_t uid = static_cast<uint32_t>(sqlite::value::Int64(argv[0]));
  uint32_t pkgid = uid % 100000;

  auto* storage = GetUserData(ctx)->storage;
  auto& cursor = GetUserData(ctx)->package_list_cursor;

  // Resolve using the package list for installed applications (>= 10000).
  if (pkgid >= 10000) {
    cursor.SetFilterValueUnchecked(0, pkgid);
    cursor.Execute();

    int best_ranking = -1;
    NullTermStringView best_package;
    for (; !cursor.Eof(); cursor.Next()) {
      NullTermStringView package = storage->GetString(cursor.package_name());

      // Prefer the real GMS package, de-prefer providers.
      int ranking = package.StartsWith("com.android.providers.") ? 0
                    : package == "com.google.android.gms"        ? 2
                                                                 : 1;

      if (ranking > best_ranking) {
        best_ranking = ranking;
        best_package = package;
      }
    }

    if (!best_package.empty()) {
      return sqlite::result::StaticString(ctx, best_package.c_str());
    }
  }

  const char* system_pkg = ResolveSystemPackage(pkgid);
  if (system_pkg != nullptr) {
    return sqlite::result::StaticString(ctx, system_pkg);
  }

  base::StackString<64> buf("uid=%" PRIu32, uid);
  return sqlite::result::TransientString(ctx, buf.c_str(),
                                         static_cast<int>(buf.len()));
}

}  // namespace perfetto::trace_processor
