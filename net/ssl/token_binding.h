// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SSL_TOKEN_BINDING_H_
#define NET_SSL_TOKEN_BINDING_H_

#include <string>
#include <vector>

#include "base/strings/string_piece.h"
#include "net/base/net_errors.h"
#include "net/base/net_export.h"

namespace crypto {
class ECPrivateKey;
}

namespace net {

enum class TokenBindingType {
  PROVIDED = 0,
  REFERRED = 1,
};

// Takes an exported keying material value |ekm| from the TLS layer, the type of
// Token Binding |type|, and a token binding key |key| and concatenates the
// Token Binding type, key type, and ekm. This concatenation is signed with
// |key| in accordance with section 3.3 of draft-ietf-tokbind-protocol-10, with
// the signature written to |*out|. Returns true on success or false if there's
// an error in the signing operations.
bool CreateTokenBindingSignature(base::StringPiece ekm,
                                 TokenBindingType type,
                                 crypto::ECPrivateKey* key,
                                 std::vector<uint8_t>* out);

// Given a vector of serialized TokenBinding structs (as defined in
// draft-ietf-tokbind-protocol-04), this function combines them to form the
// serialized TokenBindingMessage struct in |*out|. This function returns a net
// error.
//
// struct {
//     TokenBinding tokenbindings<0..2^16-1>;
// } TokenBindingMessage;
Error BuildTokenBindingMessageFromTokenBindings(
    const std::vector<base::StringPiece>& token_bindings,
    std::string* out);

// Builds a TokenBinding struct of type |type| with a TokenBindingID created
// from |*key| and a signature of |ekm| using |*key| to sign.
//
// enum {
//     rsa2048_pkcs1.5(0), rsa2048_pss(1), ecdsap256(2), (255)
// } TokenBindingKeyParameters;
//
// struct {
//     opaque modulus<1..2^16-1>;
//     opaque publicexponent<1..2^8-1>;
// } RSAPublicKey;
//
// struct {
//     opaque point <1..2^8-1>;
// } ECPoint;
//
// enum {
//     provided_token_binding(0), referred_token_binding(1), (255)
// } TokenBindingType;
//
// struct {
//     TokenBindingType tokenbinding_type;
//     TokenBindingKeyParameters key_parameters;
//     select (key_parameters) {
//         case rsa2048_pkcs1.5:
//         case rsa2048_pss:
//             RSAPublicKey rsapubkey;
//         case ecdsap256:
//             ECPoint point;
//     }
// } TokenBindingID;
//
// struct {
//     TokenBindingID tokenbindingid;
//     opaque signature<0..2^16-1>;// Signature over the exported keying
//                                 // material value
//     Extension extensions<0..2^16-1>;
// } TokenBinding;
Error BuildTokenBinding(TokenBindingType type,
                        crypto::ECPrivateKey* key,
                        const std::vector<uint8_t>& ekm,
                        std::string* out);

// Represents a parsed TokenBinding from a TokenBindingMessage.
struct TokenBinding {
  TokenBinding();

  TokenBindingType type;
  std::string ec_point;
  std::string signature;
};

// Given a TokenBindingMessage, parses the TokenBinding structs from it, putting
// them into |*token_bindings|. If there is an error parsing the
// TokenBindingMessage or the key parameter for any TokenBinding in the
// TokenBindingMessage is not ecdsap256, then this function returns false.
NET_EXPORT_PRIVATE bool ParseTokenBindingMessage(
    base::StringPiece token_binding_message,
    std::vector<TokenBinding>* token_bindings);

// Takes an ECPoint |ec_point| from a TokenBindingID, |signature| from a
// TokenBinding, and a Token Binding type |type| and verifies that |signature|
// is the signature of |ekm| using |ec_point| as the public key. Returns true if
// the signature verifies and false if it doesn't or some other error occurs in
// verification. This function is only provided for testing.
NET_EXPORT_PRIVATE bool VerifyTokenBindingSignature(base::StringPiece ec_point,
                                                    base::StringPiece signature,
                                                    TokenBindingType type,
                                                    base::StringPiece ekm);

}  // namespace net

#endif  // NET_SSL_TOKEN_BINDING_H_
