// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/file_version_info_win.h"

#include <windows.h>
#include <stddef.h>

#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/threading/thread_restrictions.h"
#include "base/win/resource_util.h"

using base::FilePath;

namespace {

struct LanguageAndCodePage {
  WORD language;
  WORD code_page;
};

// Returns the \\VarFileInfo\\Translation value extracted from the
// VS_VERSION_INFO resource in |data|.
LanguageAndCodePage* GetTranslate(const void* data) {
  LanguageAndCodePage* translate = nullptr;
  UINT length;
  if (::VerQueryValue(data, L"\\VarFileInfo\\Translation",
                      reinterpret_cast<void**>(&translate), &length)) {
    return translate;
  }
  return nullptr;
}

VS_FIXEDFILEINFO* GetVsFixedFileInfo(const void* data) {
  VS_FIXEDFILEINFO* fixed_file_info = nullptr;
  UINT length;
  if (::VerQueryValue(data, L"\\", reinterpret_cast<void**>(&fixed_file_info),
                      &length)) {
    return fixed_file_info;
  }
  return nullptr;
}

}  // namespace

FileVersionInfoWin::~FileVersionInfoWin() = default;

// static
FileVersionInfo* FileVersionInfo::CreateFileVersionInfoForModule(
    HMODULE module) {
  void* data;
  size_t version_info_length;
  const bool has_version_resource = base::win::GetResourceFromModule(
      module, VS_VERSION_INFO, RT_VERSION, &data, &version_info_length);
  if (!has_version_resource)
    return nullptr;

  const LanguageAndCodePage* translate = GetTranslate(data);
  if (!translate)
    return nullptr;

  return new FileVersionInfoWin(data, translate->language,
                                translate->code_page);
}

// static
FileVersionInfo* FileVersionInfo::CreateFileVersionInfo(
    const FilePath& file_path) {
  base::AssertBlockingAllowed();

  DWORD dummy;
  const wchar_t* path = file_path.value().c_str();
  const DWORD length = ::GetFileVersionInfoSize(path, &dummy);
  if (length == 0)
    return nullptr;

  std::vector<uint8_t> data(length, 0);

  if (!::GetFileVersionInfo(path, dummy, length, data.data()))
    return nullptr;

  const LanguageAndCodePage* translate = GetTranslate(data.data());
  if (!translate)
    return nullptr;

  return new FileVersionInfoWin(std::move(data), translate->language,
                                translate->code_page);
}

base::string16 FileVersionInfoWin::company_name() {
  return GetStringValue(L"CompanyName");
}

base::string16 FileVersionInfoWin::company_short_name() {
  return GetStringValue(L"CompanyShortName");
}

base::string16 FileVersionInfoWin::internal_name() {
  return GetStringValue(L"InternalName");
}

base::string16 FileVersionInfoWin::product_name() {
  return GetStringValue(L"ProductName");
}

base::string16 FileVersionInfoWin::product_short_name() {
  return GetStringValue(L"ProductShortName");
}

base::string16 FileVersionInfoWin::comments() {
  return GetStringValue(L"Comments");
}

base::string16 FileVersionInfoWin::legal_copyright() {
  return GetStringValue(L"LegalCopyright");
}

base::string16 FileVersionInfoWin::product_version() {
  return GetStringValue(L"ProductVersion");
}

base::string16 FileVersionInfoWin::file_description() {
  return GetStringValue(L"FileDescription");
}

base::string16 FileVersionInfoWin::legal_trademarks() {
  return GetStringValue(L"LegalTrademarks");
}

base::string16 FileVersionInfoWin::private_build() {
  return GetStringValue(L"PrivateBuild");
}

base::string16 FileVersionInfoWin::file_version() {
  return GetStringValue(L"FileVersion");
}

base::string16 FileVersionInfoWin::original_filename() {
  return GetStringValue(L"OriginalFilename");
}

base::string16 FileVersionInfoWin::special_build() {
  return GetStringValue(L"SpecialBuild");
}

base::string16 FileVersionInfoWin::last_change() {
  return GetStringValue(L"LastChange");
}

bool FileVersionInfoWin::is_official_build() {
  return (GetStringValue(L"Official Build").compare(L"1") == 0);
}

bool FileVersionInfoWin::GetValue(const wchar_t* name,
                                  std::wstring* value_str) {
  WORD lang_codepage[8];
  size_t i = 0;
  // Use the language and codepage from the DLL.
  lang_codepage[i++] = language_;
  lang_codepage[i++] = code_page_;
  // Use the default language and codepage from the DLL.
  lang_codepage[i++] = ::GetUserDefaultLangID();
  lang_codepage[i++] = code_page_;
  // Use the language from the DLL and Latin codepage (most common).
  lang_codepage[i++] = language_;
  lang_codepage[i++] = 1252;
  // Use the default language and Latin codepage (most common).
  lang_codepage[i++] = ::GetUserDefaultLangID();
  lang_codepage[i++] = 1252;

  i = 0;
  while (i < arraysize(lang_codepage)) {
    wchar_t sub_block[MAX_PATH];
    WORD language = lang_codepage[i++];
    WORD code_page = lang_codepage[i++];
    _snwprintf_s(sub_block, MAX_PATH, MAX_PATH,
                 L"\\StringFileInfo\\%04x%04x\\%ls", language, code_page, name);
    LPVOID value = NULL;
    uint32_t size;
    BOOL r = ::VerQueryValue(data_, sub_block, &value, &size);
    if (r && value) {
      value_str->assign(static_cast<wchar_t*>(value));
      return true;
    }
  }
  return false;
}

std::wstring FileVersionInfoWin::GetStringValue(const wchar_t* name) {
  std::wstring str;
  if (GetValue(name, &str))
    return str;
  else
    return L"";
}

FileVersionInfoWin::FileVersionInfoWin(std::vector<uint8_t>&& data,
                                       WORD language,
                                       WORD code_page)
    : owned_data_(std::move(data)),
      data_(owned_data_.data()),
      language_(language),
      code_page_(code_page),
      fixed_file_info_(GetVsFixedFileInfo(data_)) {
  DCHECK(!owned_data_.empty());
}

FileVersionInfoWin::FileVersionInfoWin(void* data,
                                       WORD language,
                                       WORD code_page)
    : data_(data),
      language_(language),
      code_page_(code_page),
      fixed_file_info_(GetVsFixedFileInfo(data)) {
  DCHECK(data_);
}
