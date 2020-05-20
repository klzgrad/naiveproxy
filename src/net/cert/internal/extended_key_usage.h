// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_INTERNAL_EXTENDED_KEY_USAGE_H_
#define NET_CERT_INTERNAL_EXTENDED_KEY_USAGE_H_

#include <vector>

#include "net/base/net_export.h"
#include "net/der/input.h"

namespace net {

// The following set of methods return the DER-encoded OID, without tag or
// length, of the extended key usage purposes defined in RFC 5280 section
// 4.2.1.12.
NET_EXPORT const der::Input AnyEKU();
NET_EXPORT const der::Input ServerAuth();
NET_EXPORT const der::Input ClientAuth();
NET_EXPORT const der::Input CodeSigning();
NET_EXPORT const der::Input EmailProtection();
NET_EXPORT const der::Input TimeStamping();
NET_EXPORT const der::Input OCSPSigning();

// Netscape Server Gated Crypto (2.16.840.1.113730.4.1) is a deprecated OID
// which in some situations is considered equivalent to the serverAuth key
// purpose.
NET_EXPORT const der::Input NetscapeServerGatedCrypto();

// Parses |extension_value|, which contains the extnValue field of an X.509v3
// Extended Key Usage extension, and populates |eku_oids| with the list of
// DER-encoded OID values (that is, without tag and length). Returns false if
// |extension_value| is improperly encoded.
//
// Note: The returned OIDs are only as valid as long as the data pointed to by
// |extension_value| is valid.
NET_EXPORT bool ParseEKUExtension(const der::Input& extension_value,
                                  std::vector<der::Input>* eku_oids);

}  // namespace net

#endif  // NET_CERT_INTERNAL_EXTENDED_KEY_USAGE_H_
