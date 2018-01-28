// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/sys_info.h"

#include <ApplicationServices/ApplicationServices.h>
#include <CoreServices/CoreServices.h>
#import <Foundation/Foundation.h>
#include <mach/mach_host.h>
#include <mach/mach_init.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/sysctl.h>
#include <sys/types.h>

#include "base/logging.h"
#include "base/mac/mac_util.h"
#include "base/mac/scoped_mach_port.h"
#import "base/mac/sdk_forward_declarations.h"
#include "base/macros.h"
#include "base/process/process_metrics.h"
#include "base/strings/stringprintf.h"

namespace base {

namespace {

// Queries sysctlbyname() for the given key and returns the value from the
// system or the empty string on failure.
std::string GetSysctlValue(const char* key_name) {
  char value[256];
  size_t len = arraysize(value);
  if (sysctlbyname(key_name, &value, &len, nullptr, 0) == 0) {
    DCHECK_GE(len, 1u);
    DCHECK_EQ('\0', value[len - 1]);
    return std::string(value, len - 1);
  }
  return std::string();
}

}  // namespace

// static
std::string SysInfo::OperatingSystemName() {
  return "Mac OS X";
}

// static
std::string SysInfo::OperatingSystemVersion() {
  int32_t major, minor, bugfix;
  OperatingSystemVersionNumbers(&major, &minor, &bugfix);
  return base::StringPrintf("%d.%d.%d", major, minor, bugfix);
}

// static
void SysInfo::OperatingSystemVersionNumbers(int32_t* major_version,
                                            int32_t* minor_version,
                                            int32_t* bugfix_version) {
  NSProcessInfo* processInfo = [NSProcessInfo processInfo];
  // We should try to avoid using Gestalt here because it has been observed to
  // spin up threads among other things. Using an availability check here would
  // prevent us from using the private API in 10.9.2. So use a
  // respondsToSelector check here instead and silence the warning.
  if ([processInfo respondsToSelector:@selector(operatingSystemVersion)]) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunguarded-availability"
    NSOperatingSystemVersion version = [processInfo operatingSystemVersion];
#pragma clang diagnostic pop
    *major_version = version.majorVersion;
    *minor_version = version.minorVersion;
    *bugfix_version = version.patchVersion;
  } else {
    // -[NSProcessInfo operatingSystemVersion] is documented available in 10.10.
    // It's also available via a private API since 10.9.2. For the remaining
    // cases in 10.9, rely on ::Gestalt(..). Since this code is only needed for
    // 10.9.0 and 10.9.1 and uses the recommended replacement thereafter,
    // suppress the warning for this fallback case.
    DCHECK(base::mac::IsOS10_9());
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    Gestalt(gestaltSystemVersionMajor,
            reinterpret_cast<SInt32*>(major_version));
    Gestalt(gestaltSystemVersionMinor,
            reinterpret_cast<SInt32*>(minor_version));
    Gestalt(gestaltSystemVersionBugFix,
            reinterpret_cast<SInt32*>(bugfix_version));
#pragma clang diagnostic pop
  }
}

// static
int64_t SysInfo::AmountOfPhysicalMemoryImpl() {
  struct host_basic_info hostinfo;
  mach_msg_type_number_t count = HOST_BASIC_INFO_COUNT;
  base::mac::ScopedMachSendRight host(mach_host_self());
  int result = host_info(host.get(),
                         HOST_BASIC_INFO,
                         reinterpret_cast<host_info_t>(&hostinfo),
                         &count);
  if (result != KERN_SUCCESS) {
    NOTREACHED();
    return 0;
  }
  DCHECK_EQ(HOST_BASIC_INFO_COUNT, count);
  return static_cast<int64_t>(hostinfo.max_mem);
}

// static
int64_t SysInfo::AmountOfAvailablePhysicalMemoryImpl() {
  SystemMemoryInfoKB info;
  if (!GetSystemMemoryInfo(&info))
    return 0;
  // We should add inactive file-backed memory also but there is no such
  // information from Mac OS unfortunately.
  return static_cast<int64_t>(info.free + info.speculative) * 1024;
}

// static
std::string SysInfo::CPUModelName() {
  return GetSysctlValue("machdep.cpu.brand_string");
}

// static
std::string SysInfo::HardwareModelName() {
  return GetSysctlValue("hw.model");
}

}  // namespace base
