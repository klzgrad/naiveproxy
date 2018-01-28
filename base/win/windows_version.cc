// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/win/windows_version.h"

#include <windows.h>

#include <memory>

#include "base/file_version_info_win.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "base/win/registry.h"

#if !defined(__clang__) && _MSC_FULL_VER < 191125507
#error VS 2017 Update 3.2 or higher is required
#endif

#if !defined(NTDDI_WIN10_RS2)
// Windows 10 Creators Update SDK is required to build Chrome. It is important
// to install the 10.0.15063.468 version, released June 2017, because earlier
// versions had bugs and could not build Chrome. See this link for details:
// https://developercommunity.visualstudio.com/content/problem/42961/15063-sdk-is-broken-bitsh-indirectly-references-no.html
#error Creators Update SDK (10.0.15063.468) required.
#endif

namespace {
typedef BOOL (WINAPI *GetProductInfoPtr)(DWORD, DWORD, DWORD, DWORD, PDWORD);
}  // namespace

namespace base {
namespace win {

namespace {

// Helper to map a major.minor.x.build version (e.g. 6.1) to a Windows release.
Version MajorMinorBuildToVersion(int major, int minor, int build) {
  if ((major == 5) && (minor > 0)) {
    // Treat XP Pro x64, Home Server, and Server 2003 R2 as Server 2003.
    return (minor == 1) ? VERSION_XP : VERSION_SERVER_2003;
  } else if (major == 6) {
    switch (minor) {
      case 0:
        // Treat Windows Server 2008 the same as Windows Vista.
        return VERSION_VISTA;
      case 1:
        // Treat Windows Server 2008 R2 the same as Windows 7.
        return VERSION_WIN7;
      case 2:
        // Treat Windows Server 2012 the same as Windows 8.
        return VERSION_WIN8;
      default:
        DCHECK_EQ(minor, 3);
        return VERSION_WIN8_1;
    }
  } else if (major == 10) {
    if (build < 10586) {
      return VERSION_WIN10;
    } else if (build < 14393) {
      return VERSION_WIN10_TH2;
    } else if (build < 15063) {
      return VERSION_WIN10_RS1;
    } else {
      return VERSION_WIN10_RS2;
    }
  } else if (major > 6) {
    NOTREACHED();
    return VERSION_WIN_LAST;
  }

  return VERSION_PRE_XP;
}

// Retrieve a version from kernel32. This is useful because when running in
// compatibility mode for a down-level version of the OS, the file version of
// kernel32 will still be the "real" version.
Version GetVersionFromKernel32() {
  std::unique_ptr<FileVersionInfoWin> file_version_info(
      static_cast<FileVersionInfoWin*>(
          FileVersionInfoWin::CreateFileVersionInfo(
              base::FilePath(FILE_PATH_LITERAL("kernel32.dll")))));
  if (file_version_info) {
    const int major =
        HIWORD(file_version_info->fixed_file_info()->dwFileVersionMS);
    const int minor =
        LOWORD(file_version_info->fixed_file_info()->dwFileVersionMS);
    const int build =
        HIWORD(file_version_info->fixed_file_info()->dwFileVersionLS);
    return MajorMinorBuildToVersion(major, minor, build);
  }

  NOTREACHED();
  return VERSION_WIN_LAST;
}

// Returns the the "UBR" value from the registry. Introduced in Windows 10,
// this undocumented value appears to be similar to a patch number.
// Returns 0 if the value does not exist or it could not be read.
int GetUBR() {
  // The values under the CurrentVersion registry hive are mirrored under
  // the corresponding Wow6432 hive.
  static constexpr wchar_t kRegKeyWindowsNTCurrentVersion[] =
      L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";

  base::win::RegKey key;
  if (key.Open(HKEY_LOCAL_MACHINE, kRegKeyWindowsNTCurrentVersion,
               KEY_QUERY_VALUE) != ERROR_SUCCESS) {
    return 0;
  }

  DWORD ubr = 0;
  key.ReadValueDW(L"UBR", &ubr);

  return static_cast<int>(ubr);
}

}  // namespace

// static
OSInfo* OSInfo::GetInstance() {
  // Note: we don't use the Singleton class because it depends on AtExitManager,
  // and it's convenient for other modules to use this classs without it. This
  // pattern is copied from gurl.cc.
  static OSInfo* info;
  if (!info) {
    OSInfo* new_info = new OSInfo();
    if (InterlockedCompareExchangePointer(
        reinterpret_cast<PVOID*>(&info), new_info, NULL)) {
      delete new_info;
    }
  }
  return info;
}

OSInfo::OSInfo()
    : version_(VERSION_PRE_XP),
      kernel32_version_(VERSION_PRE_XP),
      got_kernel32_version_(false),
      architecture_(OTHER_ARCHITECTURE),
      wow64_status_(GetWOW64StatusForProcess(GetCurrentProcess())) {
  OSVERSIONINFOEX version_info = { sizeof version_info };
  ::GetVersionEx(reinterpret_cast<OSVERSIONINFO*>(&version_info));
  version_number_.major = version_info.dwMajorVersion;
  version_number_.minor = version_info.dwMinorVersion;
  version_number_.build = version_info.dwBuildNumber;
  version_number_.patch = GetUBR();
  version_ = MajorMinorBuildToVersion(
      version_number_.major, version_number_.minor, version_number_.build);
  service_pack_.major = version_info.wServicePackMajor;
  service_pack_.minor = version_info.wServicePackMinor;
  service_pack_str_ = base::WideToUTF8(version_info.szCSDVersion);

  SYSTEM_INFO system_info = {};
  ::GetNativeSystemInfo(&system_info);
  switch (system_info.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_INTEL: architecture_ = X86_ARCHITECTURE; break;
    case PROCESSOR_ARCHITECTURE_AMD64: architecture_ = X64_ARCHITECTURE; break;
    case PROCESSOR_ARCHITECTURE_IA64:  architecture_ = IA64_ARCHITECTURE; break;
  }
  processors_ = system_info.dwNumberOfProcessors;
  allocation_granularity_ = system_info.dwAllocationGranularity;

  GetProductInfoPtr get_product_info;
  DWORD os_type;

  if (version_info.dwMajorVersion == 6 || version_info.dwMajorVersion == 10) {
    // Only present on Vista+.
    get_product_info = reinterpret_cast<GetProductInfoPtr>(
        ::GetProcAddress(::GetModuleHandle(L"kernel32.dll"), "GetProductInfo"));

    get_product_info(version_info.dwMajorVersion, version_info.dwMinorVersion,
                     0, 0, &os_type);
    switch (os_type) {
      case PRODUCT_CLUSTER_SERVER:
      case PRODUCT_DATACENTER_SERVER:
      case PRODUCT_DATACENTER_SERVER_CORE:
      case PRODUCT_ENTERPRISE_SERVER:
      case PRODUCT_ENTERPRISE_SERVER_CORE:
      case PRODUCT_ENTERPRISE_SERVER_IA64:
      case PRODUCT_SMALLBUSINESS_SERVER:
      case PRODUCT_SMALLBUSINESS_SERVER_PREMIUM:
      case PRODUCT_STANDARD_SERVER:
      case PRODUCT_STANDARD_SERVER_CORE:
      case PRODUCT_WEB_SERVER:
        version_type_ = SUITE_SERVER;
        break;
      case PRODUCT_PROFESSIONAL:
      case PRODUCT_ULTIMATE:
        version_type_ = SUITE_PROFESSIONAL;
        break;
      case PRODUCT_ENTERPRISE:
      case PRODUCT_ENTERPRISE_E:
      case PRODUCT_ENTERPRISE_EVALUATION:
      case PRODUCT_ENTERPRISE_N:
      case PRODUCT_ENTERPRISE_N_EVALUATION:
      case PRODUCT_ENTERPRISE_S:
      case PRODUCT_ENTERPRISE_S_EVALUATION:
      case PRODUCT_ENTERPRISE_S_N:
      case PRODUCT_ENTERPRISE_S_N_EVALUATION:
      case PRODUCT_BUSINESS:
      case PRODUCT_BUSINESS_N:
        version_type_ = SUITE_ENTERPRISE;
        break;
      case PRODUCT_EDUCATION:
      case PRODUCT_EDUCATION_N:
        version_type_ = SUITE_EDUCATION;
        break;
      case PRODUCT_HOME_BASIC:
      case PRODUCT_HOME_PREMIUM:
      case PRODUCT_STARTER:
      default:
        version_type_ = SUITE_HOME;
        break;
    }
  } else if (version_info.dwMajorVersion == 5 &&
             version_info.dwMinorVersion == 2) {
    if (version_info.wProductType == VER_NT_WORKSTATION &&
        system_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) {
      version_type_ = SUITE_PROFESSIONAL;
    } else if (version_info.wSuiteMask & VER_SUITE_WH_SERVER) {
      version_type_ = SUITE_HOME;
    } else {
      version_type_ = SUITE_SERVER;
    }
  } else if (version_info.dwMajorVersion == 5 &&
             version_info.dwMinorVersion == 1) {
    if (version_info.wSuiteMask & VER_SUITE_PERSONAL)
      version_type_ = SUITE_HOME;
    else
      version_type_ = SUITE_PROFESSIONAL;
  } else {
    // Windows is pre XP so we don't care but pick a safe default.
    version_type_ = SUITE_HOME;
  }
}

OSInfo::~OSInfo() {
}

Version OSInfo::Kernel32Version() const {
  if (!got_kernel32_version_) {
    kernel32_version_ = GetVersionFromKernel32();
    got_kernel32_version_ = true;
  }
  return kernel32_version_;
}

std::string OSInfo::processor_model_name() {
  if (processor_model_name_.empty()) {
    const wchar_t kProcessorNameString[] =
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0";
    base::win::RegKey key(HKEY_LOCAL_MACHINE, kProcessorNameString, KEY_READ);
    string16 value;
    key.ReadValue(L"ProcessorNameString", &value);
    processor_model_name_ = UTF16ToUTF8(value);
  }
  return processor_model_name_;
}

// static
OSInfo::WOW64Status OSInfo::GetWOW64StatusForProcess(HANDLE process_handle) {
  typedef BOOL (WINAPI* IsWow64ProcessFunc)(HANDLE, PBOOL);
  IsWow64ProcessFunc is_wow64_process = reinterpret_cast<IsWow64ProcessFunc>(
      GetProcAddress(GetModuleHandle(L"kernel32.dll"), "IsWow64Process"));
  if (!is_wow64_process)
    return WOW64_DISABLED;
  BOOL is_wow64 = FALSE;
  if (!(*is_wow64_process)(process_handle, &is_wow64))
    return WOW64_UNKNOWN;
  return is_wow64 ? WOW64_ENABLED : WOW64_DISABLED;
}

Version GetVersion() {
  return OSInfo::GetInstance()->version();
}

}  // namespace win
}  // namespace base
