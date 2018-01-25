// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/filename_util.h"

#include "base/files/file_path.h"
#include "base/notreached.h"

class GURL;

namespace net {

bool IsSafePortablePathComponent(const base::FilePath& component) {
  NOTREACHED();
  return false;
}

bool IsSafePortableRelativePath(const base::FilePath& path) {
  NOTREACHED();
  return false;
}

std::u16string GetSuggestedFilename(const GURL& url,
                                    const std::string& content_disposition,
                                    const std::string& referrer_charset,
                                    const std::string& suggested_name,
                                    const std::string& mime_type,
                                    const std::string& default_name) {
  NOTREACHED();
  return {};
}

base::FilePath GenerateFileName(const GURL& url,
                                const std::string& content_disposition,
                                const std::string& referrer_charset,
                                const std::string& suggested_name,
                                const std::string& mime_type,
                                const std::string& default_file_name) {
  NOTREACHED();
  return {};
}

}  // namespace net
