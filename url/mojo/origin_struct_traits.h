// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef URL_MOJO_ORIGIN_STRUCT_TRAITS_H_
#define URL_MOJO_ORIGIN_STRUCT_TRAITS_H_

#include "base/strings/string_piece.h"
#include "url/mojo/origin.mojom.h"
#include "url/origin.h"

namespace mojo {

template <>
struct StructTraits<url::mojom::OriginDataView, url::Origin> {
  static const std::string& scheme(const url::Origin& r) { return r.scheme(); }
  static const std::string& host(const url::Origin& r) { return r.host(); }
  static uint16_t port(const url::Origin& r) {
    return r.port();
  }
  static const std::string& suborigin(const url::Origin& r) {
    return r.suborigin();
  }
  static bool unique(const url::Origin& r) {
    return r.unique();
  }
  static bool Read(url::mojom::OriginDataView data, url::Origin* out) {
    if (data.unique()) {
      *out = url::Origin();
    } else {
      base::StringPiece scheme, host, suborigin;
      if (!data.ReadScheme(&scheme) || !data.ReadHost(&host) ||
          !data.ReadSuborigin(&suborigin))
        return false;

      *out = url::Origin::UnsafelyCreateOriginWithoutNormalization(
          scheme, host, data.port(), suborigin);
    }

    // If a unique origin was created, but the unique flag wasn't set, then
    // the values provided to 'UnsafelyCreateOriginWithoutNormalization' were
    // invalid.
    if (!data.unique() && out->unique())
      return false;

    return true;
  }
};

}

#endif  // URL_MOJO_ORIGIN_STRUCT_TRAITS_H_
