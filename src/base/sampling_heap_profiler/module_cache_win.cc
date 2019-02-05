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

// Gets the unique build ID and the corresponding debug path for a module.
// Windows build IDs are created by a concatenation of a GUID and AGE fields
// found in the headers of a module. The GUID is stored in the first 16 bytes
// and the AGE is stored in the last 4 bytes. Returns the empty string if the
// function fails to get the build ID. The debug path (pdb file) can be found
// in the PE file and is the build time path where the debug file was produced.
//
// Example:
// dumpbin chrome.exe /headers | find "Format:"
//   ... Format: RSDS, {16B2A428-1DED-442E-9A36-FCE8CBD29726}, 10, ...
//
// The resulting buildID string of this instance of chrome.exe is
// "16B2A4281DED442E9A36FCE8CBD2972610".
//
// Note that the AGE field is encoded in decimal, not hex.
void GetDebugInfoForModule(HMODULE module_handle,
                           std::string* build_id,
                           FilePath* pdb_name) {
  GUID guid;
  DWORD age;
  LPCSTR pdb_file = nullptr;
  size_t pdb_file_length = 0;
  if (!win::PEImage(module_handle)
           .GetDebugId(&guid, &age, &pdb_file, &pdb_file_length)) {
    return;
  }

  FilePath::StringType pdb_filename;
  if (!base::UTF8ToWide(pdb_file, pdb_file_length, &pdb_filename))
    return;
  *pdb_name = FilePath(std::move(pdb_filename)).BaseName();

  const int kGUIDSize = 39;
  string16 buffer;
  int result =
      ::StringFromGUID2(guid, WriteInto(&buffer, kGUIDSize), kGUIDSize);
  if (result != kGUIDSize)
    return;
  RemoveChars(buffer, L"{}-", &buffer);
  StringAppendF(&buffer, L"%d", age);
  *build_id = UTF16ToUTF8(buffer);
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
  FilePath pdb_name;
  std::string build_id;
  GetDebugInfoForModule(module_handle, &build_id, &pdb_name);
  if (build_id.empty())
    return Module();

  MODULEINFO module_info;
  if (!::GetModuleInformation(GetCurrentProcessHandle(), module_handle,
                              &module_info, sizeof(module_info))) {
    return Module();
  }

  return Module(reinterpret_cast<uintptr_t>(module_info.lpBaseOfDll), build_id,
                pdb_name, module_info.SizeOfImage);
}

}  // namespace base
