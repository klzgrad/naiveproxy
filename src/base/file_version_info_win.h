// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_FILE_VERSION_INFO_WIN_H_
#define BASE_FILE_VERSION_INFO_WIN_H_

#include <windows.h>

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "base/base_export.h"
#include "base/file_version_info.h"
#include "base/macros.h"

struct tagVS_FIXEDFILEINFO;
typedef tagVS_FIXEDFILEINFO VS_FIXEDFILEINFO;

class BASE_EXPORT FileVersionInfoWin : public FileVersionInfo {
 public:
  ~FileVersionInfoWin() override;

  // Accessors to the different version properties.
  // Returns an empty string if the property is not found.
  base::string16 company_name() override;
  base::string16 company_short_name() override;
  base::string16 product_name() override;
  base::string16 product_short_name() override;
  base::string16 internal_name() override;
  base::string16 product_version() override;
  base::string16 private_build() override;
  base::string16 special_build() override;
  base::string16 comments() override;
  base::string16 original_filename() override;
  base::string16 file_description() override;
  base::string16 file_version() override;
  base::string16 legal_copyright() override;
  base::string16 legal_trademarks() override;
  base::string16 last_change() override;
  bool is_official_build() override;

  // Lets you access other properties not covered above.
  bool GetValue(const base::char16* name, base::string16* value);

  // Similar to GetValue but returns a string16 (empty string if the property
  // does not exist).
  base::string16 GetStringValue(const base::char16* name);

  // Get the fixed file info if it exists. Otherwise NULL
  const VS_FIXEDFILEINFO* fixed_file_info() const { return fixed_file_info_; }

  // Behaves like CreateFileVersionInfo, but returns a FileVersionInfoWin.
  static std::unique_ptr<FileVersionInfoWin> CreateFileVersionInfoWin(
      const base::FilePath& file_path);

 private:
  friend FileVersionInfo;

  // |data| is a VS_VERSION_INFO resource. |language| and |code_page| are
  // extracted from the \VarFileInfo\Translation value of |data|.
  FileVersionInfoWin(std::vector<uint8_t>&& data,
                     WORD language,
                     WORD code_page);
  FileVersionInfoWin(void* data, WORD language, WORD code_page);

  const std::vector<uint8_t> owned_data_;
  const void* const data_;
  const WORD language_;
  const WORD code_page_;

  // This is a pointer into |data_| if it exists. Otherwise nullptr.
  const VS_FIXEDFILEINFO* const fixed_file_info_;

  DISALLOW_COPY_AND_ASSIGN(FileVersionInfoWin);
};

#endif  // BASE_FILE_VERSION_INFO_WIN_H_
