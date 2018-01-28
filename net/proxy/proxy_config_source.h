// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_PROXY_PROXY_CONFIG_SOURCE_H_
#define NET_PROXY_PROXY_CONFIG_SOURCE_H_

namespace net {

// Source of the configuration settings encapsulated in a ProxyConfig object.

// The source information is used for determining how credentials are used and
// for logging.  When adding new values, remember to add a string to
// kSourceNames[] in proxy_config_source.cc.
enum ProxyConfigSource {
  PROXY_CONFIG_SOURCE_UNKNOWN,       // The source hasn't been set.
  PROXY_CONFIG_SOURCE_SYSTEM,        // System settings (Win/Mac).
  PROXY_CONFIG_SOURCE_SYSTEM_FAILED, // Default settings after failure to
                                     // determine system settings.
  PROXY_CONFIG_SOURCE_GCONF,         // GConf (Linux)
  PROXY_CONFIG_SOURCE_GSETTINGS,     // GSettings (Linux).
  PROXY_CONFIG_SOURCE_KDE,           // KDE (Linux).
  PROXY_CONFIG_SOURCE_ENV,           // Environment variables.
  PROXY_CONFIG_SOURCE_CUSTOM,        // Custom settings local to the
                                     // application (command line,
                                     // extensions, application
                                     // specific preferences, etc.)
  PROXY_CONFIG_SOURCE_TEST,          // Test settings.
  NUM_PROXY_CONFIG_SOURCES
};

// Returns a textual representation of the source.
const char* ProxyConfigSourceToString(ProxyConfigSource source);

}  // namespace net

#endif  // NET_PROXY_PROXY_CONFIG_SOURCE_H_
