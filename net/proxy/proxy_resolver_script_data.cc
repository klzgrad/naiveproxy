// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/proxy/proxy_resolver_script_data.h"

#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"

namespace net {

// static
scoped_refptr<ProxyResolverScriptData> ProxyResolverScriptData::FromUTF8(
    const std::string& utf8) {
  return new ProxyResolverScriptData(TYPE_SCRIPT_CONTENTS,
                                     GURL(),
                                     base::UTF8ToUTF16(utf8));
}

// static
scoped_refptr<ProxyResolverScriptData> ProxyResolverScriptData::FromUTF16(
    const base::string16& utf16) {
  return new ProxyResolverScriptData(TYPE_SCRIPT_CONTENTS, GURL(), utf16);
}

// static
scoped_refptr<ProxyResolverScriptData> ProxyResolverScriptData::FromURL(
    const GURL& url) {
  return new ProxyResolverScriptData(TYPE_SCRIPT_URL, url, base::string16());
}

// static
scoped_refptr<ProxyResolverScriptData>
ProxyResolverScriptData::ForAutoDetect() {
  return new ProxyResolverScriptData(TYPE_AUTO_DETECT, GURL(),
                                     base::string16());
}

const base::string16& ProxyResolverScriptData::utf16() const {
  DCHECK_EQ(TYPE_SCRIPT_CONTENTS, type_);
  return utf16_;
}

const GURL& ProxyResolverScriptData::url() const {
  DCHECK_EQ(TYPE_SCRIPT_URL, type_);
  return url_;
}

bool ProxyResolverScriptData::Equals(
    const ProxyResolverScriptData* other) const {
  if (type() != other->type())
    return false;

  switch (type()) {
    case TYPE_SCRIPT_CONTENTS:
      return utf16() == other->utf16();
    case TYPE_SCRIPT_URL:
      return url() == other->url();
    case TYPE_AUTO_DETECT:
      return true;
  }

  return false;  // Shouldn't be reached.
}

ProxyResolverScriptData::ProxyResolverScriptData(Type type,
                                                 const GURL& url,
                                                 const base::string16& utf16)
    : type_(type),
      url_(url),
      utf16_(utf16) {
}

ProxyResolverScriptData::~ProxyResolverScriptData() {}

}  // namespace net
