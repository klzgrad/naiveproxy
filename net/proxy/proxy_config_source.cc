// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/proxy/proxy_config_source.h"

#include "base/logging.h"

namespace net {

namespace {

const char* const kSourceNames[] = {
  "UNKNOWN",
  "SYSTEM",
  "SYSTEM FAILED",
  "GCONF",
  "GSETTINGS",
  "KDE",
  "ENV",
  "CUSTOM",
  "TEST"
};
static_assert(arraysize(kSourceNames) == NUM_PROXY_CONFIG_SOURCES,
              "kSourceNames has incorrect size");

}  // namespace

const char* ProxyConfigSourceToString(ProxyConfigSource source) {
  DCHECK_GT(NUM_PROXY_CONFIG_SOURCES, source);
  return kSourceNames[source];
}

}  // namespace net
