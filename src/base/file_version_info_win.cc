// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/file_version_info_win.h"

#include <windows.h>
#include <stddef.h>

#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/stl_util.h"
#include "base/strings/string_util.h"
#include "base/threading/scoped_blocking_call.h"
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
std::unique_ptr<FileVersionInfo>
FileVersionInfo::CreateFileVersionInfoForModule(HMODULE module) {
  void* data;
  size_t version_info_length;
  const bool has_version_resource = base::win::GetResourceFromModule(
      module, VS_VERSION_INFO, RT_VERSION, &data, &version_info_length);
  if (!has_version_resource)
    return nullptr;

  const LanguageAndCodePage* translate = GetTranslate(data);
  if (!translate)
    return nullptr;

  return base::WrapUnique(
      new FileVersionInfoWin(data, translate->language, translate->code_page));
}

// static
std::unique_ptr<FileVersionInfo> FileVersionInfo::CreateFileVersionInfo(
    const FilePath& file_path) {
  return FileVersionInfoWin::CreateFileVersionInfoWin(file_path);
}

// static
std::unique_ptr<FileVersionInfoWin>
FileVersionInfoWin::CreateFileVersionInfoWin(const FilePath& file_path) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);

  DWORD dummy;
  const wchar_t* path = base::as_wcstr(file_path.value());
  const DWORD length = ::GetFileVersionInfoSize(path, &dummy);
  if (length == 0)
    return nullptr;

  std::vector<uint8_t> data(length, 0);

  if (!::GetFileVersionInfo(path, dummy, length, data.data()))
    return nullptr;

  const LanguageAndCodePage* translate = GetTranslate(data.data());
  if (!translate)
    return nullptr;

  return base::WrapUnique(new FileVersionInfoWin(
      std::move(data), translate->language, translate->code_page));
}

base::string16 FileVersionInfoWin::company_name() {
  return GetStringValue(STRING16_LITERAL("CompanyName"));
}

base::string16 FileVersionInfoWin::company_short_name() {
  return GetStringValue(STRING16_LITERAL("CompanyShortName"));
}

base::string16 FileVersionInfoWin::internal_name() {
  return GetStringValue(STRING16_LITERAL("InternalName"));
}

base::string16 FileVersionInfoWin::product_name() {
  return GetStringValue(STRING16_LITERAL("ProductName"));
}

base::string16 FileVersionInfoWin::product_short_name() {
  return GetStringValue(STRING16_LITERAL("ProductShortName"));
}

base::string16 FileVersionInfoWin::comments() {
  return GetStringValue(STRING16_LITERAL("Comments"));
}

base::string16 FileVersionInfoWin::legal_copyright() {
  return GetStringValue(STRING16_LITERAL("LegalCopyright"));
}

base::string16 FileVersionInfoWin::product_version() {
  return GetStringValue(STRING16_LITERAL("ProductVersion"));
}

base::string16 FileVersionInfoWin::file_description() {
  return GetStringValue(STRING16_LITERAL("FileDescription"));
}

base::string16 FileVersionInfoWin::legal_trademarks() {
  return GetStringValue(STRING16_LITERAL("LegalTrademarks"));
}

base::string16 FileVersionInfoWin::private_build() {
  return GetStringValue(STRING16_LITERAL("PrivateBuild"));
}

base::string16 FileVersionInfoWin::file_version() {
  return GetStringValue(STRING16_LITERAL("FileVersion"));
}

base::string16 FileVersionInfoWin::original_filename() {
  return GetStringValue(STRING16_LITERAL("OriginalFilename"));
}

base::string16 FileVersionInfoWin::special_build() {
  return GetStringValue(STRING16_LITERAL("SpecialBuild"));
}

base::string16 FileVersionInfoWin::last_change() {
  return GetStringValue(STRING16_LITERAL("LastChange"));
}

bool FileVersionInfoWin::is_official_build() {
  return GetStringValue(STRING16_LITERAL("Official Build")) ==
         STRING16_LITERAL("1");
}

bool FileVersionInfoWin::GetValue(const base::char16* name,
                                  base::string16* value_str) {
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
  while (i < base::size(lang_codepage)) {
    wchar_t sub_block[MAX_PATH];
    WORD language = lang_codepage[i++];
    WORD code_page = lang_codepage[i++];
    _snwprintf_s(sub_block, MAX_PATH, MAX_PATH,
                 L"\\StringFileInfo\\%04x%04x\\%ls", language, code_page,
                 base::as_wcstr(name));
    LPVOID value = NULL;
    uint32_t size;
    BOOL r = ::VerQueryValue(data_, sub_block, &value, &size);
    if (r && value) {
      value_str->assign(static_cast<base::char16*>(value));
      return true;
    }
  }
  return false;
}

base::string16 FileVersionInfoWin::GetStringValue(const base::char16* name) {
  base::string16 str;
  if (GetValue(name, &str))
    return str;
  else
    return base::string16();
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
