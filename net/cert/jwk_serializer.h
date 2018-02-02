// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_JWK_SERIALIZER_H_
#define NET_CERT_JWK_SERIALIZER_H_

#include "base/strings/string_piece.h"
#include "net/base/net_export.h"

namespace base {
class DictionaryValue;
}

namespace net {

namespace JwkSerializer {

// Converts a subject public key info from DER to JWK.
// See http://tools.ietf.org/html/draft-ietf-jose-json-web-algorithms-17 for
// the output format.
NET_EXPORT_PRIVATE bool ConvertSpkiFromDerToJwk(
    const base::StringPiece& spki_der,
    base::DictionaryValue* public_key_jwk);

} // namespace JwkSerializer

} // namespace net

#endif  // NET_CERT_JWK_SERIALIZER_H_
