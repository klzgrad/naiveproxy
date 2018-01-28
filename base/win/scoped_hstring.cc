// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/win/scoped_hstring.h"

#include <winstring.h>

#include "base/strings/string_piece.h"
#include "base/strings/utf_string_conversions.h"

namespace base {

namespace {

static bool g_load_succeeded = false;

void* LoadComBaseFunction(const char* function_name) {
  static HMODULE const handle = ::LoadLibrary(L"combase.dll");
  return handle ? ::GetProcAddress(handle, function_name) : nullptr;
}

decltype(&::WindowsCreateString) GetWindowsCreateString() {
  static decltype(&::WindowsCreateString) const function =
      reinterpret_cast<decltype(&::WindowsCreateString)>(
          LoadComBaseFunction("WindowsCreateString"));
  return function;
}

decltype(&::WindowsDeleteString) GetWindowsDeleteString() {
  static decltype(&::WindowsDeleteString) const function =
      reinterpret_cast<decltype(&::WindowsDeleteString)>(
          LoadComBaseFunction("WindowsDeleteString"));
  return function;
}

decltype(&::WindowsGetStringRawBuffer) GetWindowsGetStringRawBuffer() {
  static decltype(&::WindowsGetStringRawBuffer) const function =
      reinterpret_cast<decltype(&::WindowsGetStringRawBuffer)>(
          LoadComBaseFunction("WindowsGetStringRawBuffer"));
  return function;
}

HRESULT WindowsCreateString(const base::char16* src,
                            uint32_t len,
                            HSTRING* out_hstr) {
  decltype(&::WindowsCreateString) create_string_func =
      GetWindowsCreateString();
  if (!create_string_func)
    return E_FAIL;
  return create_string_func(src, len, out_hstr);
}

HRESULT WindowsDeleteString(HSTRING hstr) {
  decltype(&::WindowsDeleteString) delete_string_func =
      GetWindowsDeleteString();
  if (!delete_string_func)
    return E_FAIL;
  return delete_string_func(hstr);
}

const base::char16* WindowsGetStringRawBuffer(HSTRING hstr, uint32_t* out_len) {
  decltype(&::WindowsGetStringRawBuffer) get_string_raw_buffer_func =
      GetWindowsGetStringRawBuffer();
  if (!get_string_raw_buffer_func) {
    *out_len = 0;
    return nullptr;
  }
  return get_string_raw_buffer_func(hstr, out_len);
}

}  // namespace

namespace internal {

// static
void ScopedHStringTraits::Free(HSTRING hstr) {
  base::WindowsDeleteString(hstr);
}

}  // namespace internal

namespace win {

// static
ScopedHString ScopedHString::Create(StringPiece16 str) {
  DCHECK(g_load_succeeded);
  HSTRING hstr;
  HRESULT hr = base::WindowsCreateString(str.data(), str.length(), &hstr);
  if (SUCCEEDED(hr))
    return ScopedHString(hstr);
  DLOG(ERROR) << "Failed to create HSTRING" << std::hex << hr;
  return ScopedHString(nullptr);
}

ScopedHString ScopedHString::Create(StringPiece str) {
  return Create(UTF8ToWide(str));
}

ScopedHString::ScopedHString(HSTRING hstr) : ScopedGeneric(hstr) {
  DCHECK(g_load_succeeded);
}

// static
bool ScopedHString::ResolveCoreWinRTStringDelayload() {
  // TODO(finnur): Add AssertIOAllowed once crbug.com/770193 is fixed.

  static const bool load_succeeded = []() {
    bool success = GetWindowsCreateString() && GetWindowsDeleteString() &&
                   GetWindowsGetStringRawBuffer();
    g_load_succeeded = success;
    return success;
  }();
  return load_succeeded;
}

StringPiece16 ScopedHString::Get() const {
  UINT32 length = 0;
  const wchar_t* buffer = base::WindowsGetStringRawBuffer(get(), &length);
  return StringPiece16(buffer, length);
}

std::string ScopedHString::GetAsUTF8() const {
  std::string result;
  const StringPiece16 wide_string = Get();
  WideToUTF8(wide_string.data(), wide_string.length(), &result);
  return result;
}

}  // namespace win
}  // namespace base
