// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef URL_MOJO_URL_GURL_STRUCT_TRAITS_H_
#define URL_MOJO_URL_GURL_STRUCT_TRAITS_H_

#include "base/strings/string_piece.h"
#include "url/gurl.h"
#include "url/mojo/url.mojom.h"
#include "url/url_constants.h"

namespace mojo {

template <>
struct StructTraits<url::mojom::UrlDataView, GURL> {
  static base::StringPiece url(const GURL& r) {
    if (r.possibly_invalid_spec().length() > url::kMaxURLChars ||
        !r.is_valid()) {
      return base::StringPiece();
    }

    return base::StringPiece(r.possibly_invalid_spec().c_str(),
                             r.possibly_invalid_spec().length());
  }
  static bool Read(url::mojom::UrlDataView data, GURL* out) {
    base::StringPiece url_string;
    if (!data.ReadUrl(&url_string))
      return false;

    if (url_string.length() > url::kMaxURLChars)
      return false;

    *out = GURL(url_string);
    if (!url_string.empty() && !out->is_valid())
      return false;

    return true;
  }
};

}

#endif  // URL_MOJO_URL_GURL_STRUCT_TRAITS_H_
