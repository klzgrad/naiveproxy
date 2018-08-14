// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/sampling_heap_profiler/module_cache.h"

#include <objbase.h>
#include <psapi.h>

#include "base/process/process_handle.h"
#include "base/stl_util.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/win/pe_image.h"
#include "base/win/scoped_handle.h"

namespace base {

namespace {

// Gets the unique build ID for a module. Windows build IDs are created by a
// concatenation of a GUID and AGE fields found in the headers of a module. The
// GUID is stored in the first 16 bytes and the AGE is stored in the last 4
// bytes. Returns the empty string if the function fails to get the build ID.
//
// Example:
// dumpbin chrome.exe /headers | find "Format:"
//   ... Format: RSDS, {16B2A428-1DED-442E-9A36-FCE8CBD29726}, 10, ...
//
// The resulting buildID string of this instance of chrome.exe is
// "16B2A4281DED442E9A36FCE8CBD2972610".
//
// Note that the AGE field is encoded in decimal, not hex.
std::string GetBuildIDForModule(HMODULE module_handle) {
  GUID guid;
  DWORD age;
  win::PEImage(module_handle).GetDebugId(&guid, &age, /* pdb_file= */ nullptr);
  const int kGUIDSize = 39;
  string16 build_id;
  int result =
      ::StringFromGUID2(guid, WriteInto(&build_id, kGUIDSize), kGUIDSize);
  if (result != kGUIDSize)
    return std::string();
  RemoveChars(build_id, L"{}-", &build_id);
  build_id += StringPrintf(L"%d", age);
  return UTF16ToUTF8(build_id);
}

}  // namespace

// static
ModuleCache::Module ModuleCache::CreateModuleForAddress(uintptr_t address) {
  HMODULE module_handle = nullptr;
  if (!::GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           reinterpret_cast<LPCTSTR>(address),
                           &module_handle)) {
    DCHECK_EQ(ERROR_MOD_NOT_FOUND, static_cast<int>(::GetLastError()));
    return Module();
  }
  Module module = CreateModuleForHandle(module_handle);
  ::CloseHandle(module_handle);
  return module;
}

// static
ModuleCache::Module ModuleCache::CreateModuleForHandle(HMODULE module_handle) {
  wchar_t module_name[MAX_PATH];
  DWORD result_length =
      ::GetModuleFileName(module_handle, module_name, size(module_name));
  if (result_length == 0)
    return Module();
  const std::string& module_id = GetBuildIDForModule(module_handle);
  if (module_id.empty())
    return Module();

  MODULEINFO module_info;
  if (!::GetModuleInformation(GetCurrentProcessHandle(), module_handle,
                              &module_info, sizeof(module_info))) {
    return Module();
  }

  return Module(reinterpret_cast<uintptr_t>(module_info.lpBaseOfDll), module_id,
                FilePath(module_name), module_info.SizeOfImage);
}

}  // namespace base
