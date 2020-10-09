// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fontconfig/fontconfig.h>
#include <string.h>
#include <time.h>
#include <utime.h>

#include <string>

#include "base/base_paths.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/test/fontconfig_util_linux.h"

// GIANT WARNING: The point of this file is to front-load construction of the
// font cache [which takes 600ms] from test run time to compile time. This saves
// 600ms on each test shard which uses the font cache into compile time. The
// problem is that fontconfig cache construction is not intended to be
// deterministic. This executable tries to set some external state to ensure
// determinism. We have no way of guaranteeing that this produces correct
// results, or even has the intended effect.
int main() {
  base::FilePath dir_module;
  base::PathService::Get(base::DIR_MODULE, &dir_module);

  // This is the MD5 hash of "/test_fonts", which is used as the key of the
  // fontconfig cache.
  //     $ echo -n /test_fonts | md5sum
  //     fb5c91b2895aa445d23aebf7f9e2189c  -
  static const char kCacheKey[] = "fb5c91b2895aa445d23aebf7f9e2189c";

  // fontconfig writes the mtime of the test_fonts directory into the cache. It
  // presumably checks this later to ensure that the cache is still up to date.
  // We set the mtime to an arbitrary, fixed time in the past.
  base::FilePath test_fonts_file_path = dir_module.Append("test_fonts");
  base::stat_wrapper_t old_times;
  struct utimbuf new_times;

  base::File::Stat(test_fonts_file_path.value().c_str(), &old_times);
  new_times.actime = old_times.st_atime;
  // Use an arbitrary, fixed time.
  new_times.modtime = 123456789;
  utime(test_fonts_file_path.value().c_str(), &new_times);

  base::FilePath fontconfig_caches = dir_module.Append("fontconfig_caches");

  // Delete directory before generating fontconfig caches. This will notify
  // future fontconfig_caches changes.
  CHECK(base::DeletePathRecursively(fontconfig_caches));

  base::SetUpFontconfig();
  FcInit();
  FcFini();

  // Check existence of intended fontconfig cache file.
  CHECK(base::PathExists(
      fontconfig_caches.Append(base::StrCat({kCacheKey, "-le64.cache-7"}))));
  return 0;
}
