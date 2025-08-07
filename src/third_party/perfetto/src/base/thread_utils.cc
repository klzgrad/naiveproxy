/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "perfetto/base/thread_utils.h"
#include "perfetto/ext/base/thread_utils.h"

#include "perfetto/base/build_config.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_FUCHSIA)
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#elif PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <Windows.h>
#endif

namespace perfetto {
namespace base {

#if PERFETTO_BUILDFLAG(PERFETTO_OS_FUCHSIA)
static PlatformThreadId ResolveThreadId() {
  zx_info_handle_basic_t basic;
  return (zx_object_get_info(zx_thread_self(), ZX_INFO_HANDLE_BASIC, &basic,
                             sizeof(basic), nullptr, nullptr) == ZX_OK)
             ? basic.koid
             : ZX_KOID_INVALID;
}
PlatformThreadId GetThreadId() {
  thread_local static PlatformThreadId thread_id = ResolveThreadId();
  return thread_id;
}

#elif PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)

// The SetThreadDescription API was brought in version 1607 of Windows 10.
typedef HRESULT(WINAPI* SetThreadDescription)(HANDLE hThread,
                                              PCWSTR lpThreadDescription);

// The SetThreadDescription API was brought in version 1607 of Windows 10.
typedef HRESULT(WINAPI* GetThreadDescription)(HANDLE hThread,
                                              PWSTR* ppszThreadDescription);

bool MaybeSetThreadName(const std::string& name) {
  // The SetThreadDescription API works even if no debugger is attached.
  static auto set_thread_description_func =
      reinterpret_cast<SetThreadDescription>(
          reinterpret_cast<void*>(::GetProcAddress(
              ::GetModuleHandleA("Kernel32.dll"), "SetThreadDescription")));
  if (!set_thread_description_func) {
    return false;
  }
  std::wstring wide_thread_name;
  if (!UTF8ToWide(name, wide_thread_name)) {
    return false;
  }
  HRESULT result = set_thread_description_func(::GetCurrentThread(),
                                               wide_thread_name.c_str());
  return !FAILED(result);
}

bool GetThreadName(std::string& out_result) {
  static auto get_thread_description_func =
      reinterpret_cast<GetThreadDescription>(
          reinterpret_cast<void*>(::GetProcAddress(
              ::GetModuleHandleA("Kernel32.dll"), "GetThreadDescription")));
  if (!get_thread_description_func) {
    return false;
  }
  wchar_t* wide_thread_name;
  HRESULT result =
      get_thread_description_func(::GetCurrentThread(), &wide_thread_name);
  if (SUCCEEDED(result)) {
    bool success = WideToUTF8(std::wstring(wide_thread_name), out_result);
    LocalFree(wide_thread_name);
    return success;
  }
  return false;
}

#endif  // PERFETTO_BUILDFLAG(PERFETTO_OS_FUCHSIA)

}  // namespace base
}  // namespace perfetto
