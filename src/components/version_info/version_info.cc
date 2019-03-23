// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/version_info/version_info.h"

#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/version.h"
#include "build/build_config.h"
#include "components/version_info/version_info_values.h"

namespace version_info {

std::string GetProductNameAndVersionForUserAgent() {
  return "Chrome/" + GetVersionNumber();
}

std::string GetProductName() {
  return PRODUCT_NAME;
}

std::string GetVersionNumber() {
  return PRODUCT_VERSION;
}

const base::Version& GetVersion() {
  static const base::NoDestructor<base::Version> version(GetVersionNumber());
  return *version;
}

std::string GetLastChange() {
  return LAST_CHANGE;
}

bool IsOfficialBuild() {
  return IS_OFFICIAL_BUILD;
}

std::string GetOSType() {
#if defined(OS_WIN)
  return "Windows";
#elif defined(OS_IOS)
  return "iOS";
#elif defined(OS_MACOSX)
  return "Mac OS X";
#elif defined(OS_CHROMEOS)
# if defined(GOOGLE_CHROME_BUILD)
  return "Chrome OS";
# else
  return "Chromium OS";
# endif
#elif defined(OS_ANDROID)
  return "Android";
#elif defined(OS_LINUX)
  return "Linux";
#elif defined(OS_FREEBSD)
  return "FreeBSD";
#elif defined(OS_OPENBSD)
  return "OpenBSD";
#elif defined(OS_SOLARIS)
  return "Solaris";
#else
  return "Unknown";
#endif
}

std::string GetChannelString(Channel channel) {
  switch (channel) {
    case Channel::STABLE:
      return "stable";
    case Channel::BETA:
      return "beta";
    case Channel::DEV:
      return "dev";
    case Channel::CANARY:
      return "canary";
    case Channel::UNKNOWN:
      return "unknown";
  }
  NOTREACHED();
  return std::string();
}

}  // namespace version_info
