/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "perfetto/ext/base/android_utils.h"

#include "perfetto/base/build_config.h"

#include <string>

#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
#include <sys/system_properties.h>
#endif

#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/string_utils.h"

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN) &&  \
    !PERFETTO_BUILDFLAG(PERFETTO_OS_NACL) && \
    !PERFETTO_BUILDFLAG(PERFETTO_OS_WASM)
#include <sys/utsname.h>
#include <unistd.h>
#endif

namespace perfetto {
namespace base {

#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)

std::string GetAndroidProp(const char* name) {
  std::string ret;
#if __ANDROID_API__ >= 26
  const prop_info* pi = __system_property_find(name);
  if (!pi) {
    return ret;
  }
  __system_property_read_callback(
      pi,
      [](void* dst_void, const char*, const char* value, uint32_t) {
        std::string& dst = *static_cast<std::string*>(dst_void);
        dst = value;
      },
      &ret);
#else  // __ANDROID_API__ < 26
  char value_buf[PROP_VALUE_MAX];
  int len = __system_property_get(name, value_buf);
  if (len > 0 && static_cast<size_t>(len) < sizeof(value_buf)) {
    ret = std::string(value_buf, static_cast<size_t>(len));
  }
#endif
  return ret;
}

#endif  // PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)

Utsname GetUtsname() {
  Utsname utsname_info;
#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN) &&  \
    !PERFETTO_BUILDFLAG(PERFETTO_OS_NACL) && \
    !PERFETTO_BUILDFLAG(PERFETTO_OS_WASM)
  struct utsname uname_info;
  if (uname(&uname_info) == 0) {
    utsname_info.sysname = uname_info.sysname;
    utsname_info.version = uname_info.version;
    utsname_info.machine = uname_info.machine;
    utsname_info.release = uname_info.release;
  } else {
    PERFETTO_ELOG("Unable to read Utsname information");
  }
#endif  // !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  return utsname_info;
}

SystemInfo GetSystemInfo() {
  SystemInfo info;

  info.timezone_off_mins = GetTimezoneOffsetMins();

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN) &&  \
    !PERFETTO_BUILDFLAG(PERFETTO_OS_NACL) && \
    !PERFETTO_BUILDFLAG(PERFETTO_OS_WASM)
  info.utsname_info = GetUtsname();
  info.page_size = static_cast<uint32_t>(sysconf(_SC_PAGESIZE));
  info.num_cpus = static_cast<uint32_t>(sysconf(_SC_NPROCESSORS_CONF));
#endif  // !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  info.android_build_fingerprint = GetAndroidProp("ro.build.fingerprint");
  if (info.android_build_fingerprint.empty()) {
    PERFETTO_ELOG("Unable to read ro.build.fingerprint");
  }

  info.android_device_manufacturer = GetAndroidProp("ro.product.manufacturer");
  if (info.android_device_manufacturer.empty()) {
    PERFETTO_ELOG("Unable to read ro.product.manufacturer");
  }

  std::string sdk_str_value = GetAndroidProp("ro.build.version.sdk");
  info.android_sdk_version = StringToUInt64(sdk_str_value);
  if (!info.android_sdk_version.has_value()) {
    PERFETTO_ELOG("Unable to read ro.build.version.sdk");
  }

  info.android_soc_model = GetAndroidProp("ro.soc.model");
  if (info.android_soc_model.empty()) {
    PERFETTO_ELOG("Unable to read ro.soc.model");
  }

  // guest_soc model is not always present
  info.android_guest_soc_model = GetAndroidProp("ro.boot.guest_soc.model");

  info.android_hardware_revision = GetAndroidProp("ro.boot.hardware.revision");
  if (info.android_hardware_revision.empty()) {
    PERFETTO_ELOG("Unable to read ro.boot.hardware.revision");
  }

  info.android_storage_model = GetAndroidProp("ro.boot.hardware.ufs");
  if (info.android_storage_model.empty()) {
    PERFETTO_ELOG("Unable to read ro.boot.hardware.ufs");
  }

  info.android_ram_model = GetAndroidProp("ro.boot.hardware.ddr");
  if (info.android_ram_model.empty()) {
    PERFETTO_ELOG("Unable to read ro.boot.hardware.ddr");
  }

  info.android_serial_console = GetAndroidProp("init.svc.console");
  if (info.android_serial_console.empty()) {
    PERFETTO_ELOG("Unable to read init.svc.console");
  }
#endif  // PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)

  return info;
}

std::string GetPerfettoMachineName() {
  const char* env_name = getenv("PERFETTO_MACHINE_NAME");
  if (env_name) {
    return env_name;
  }
#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  std::string name = base::GetAndroidProp("traced.machine_name");
  if (name.empty()) {
    name = GetUtsname().sysname;
  }
  return name;
#else
  return GetUtsname().sysname;
#endif
}

}  // namespace base
}  // namespace perfetto
