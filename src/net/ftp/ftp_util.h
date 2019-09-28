// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_FTP_FTP_UTIL_H_
#define NET_FTP_FTP_UTIL_H_

#include <string>

#include "base/strings/string16.h"
#include "net/base/net_export.h"

namespace base {
class Time;
}

namespace net {

class NET_EXPORT_PRIVATE FtpUtil {
 public:
  // Converts Unix file path to VMS path (must be a file, and not a directory).
  static std::string UnixFilePathToVMS(const std::string& unix_path);

  // Converts Unix directory path to VMS path (must be a directory).
  static std::string UnixDirectoryPathToVMS(const std::string& unix_path);

  // Converts VMS path to Unix-style path.
  static std::string VMSPathToUnix(const std::string& vms_path);

  // Converts abbreviated month (like Nov) to its number (in range 1-12).
  // Note: in some locales abbreviations are more than three letters long,
  // and this function also handles them correctly.
  static bool AbbreviatedMonthToNumber(const base::string16& text, int* number);

  // Converts a "ls -l" date listing to time. The listing comes in three
  // columns. The first one contains month, the second one contains day
  // of month. The third one is either a time (and then we guess the year based
  // on |current_time|), or is a year (and then we don't know the time).
  static bool LsDateListingToTime(const base::string16& month,
                                  const base::string16& day,
                                  const base::string16& rest,
                                  const base::Time& current_time,
                                  base::Time* result);

  // Converts a Windows date listing to time. Returns true on success.
  static bool WindowsDateListingToTime(const base::string16& date,
                                       const base::string16& time,
                                       base::Time* result);

  // Skips |columns| columns from |text| (whitespace-delimited), and returns the
  // remaining part, without leading/trailing whitespace.
  static base::string16 GetStringPartAfterColumns(const base::string16& text,
                                                  int columns);
};

}  // namespace net

#endif  // NET_FTP_FTP_UTIL_H_
