// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DER_TAG_H_
#define NET_DER_TAG_H_

#include <stdint.h>

#include "net/base/net_export.h"

namespace net {

namespace der {

// This Tag type represents the identifier for an ASN.1 tag as encoded with DER.
// It follows the same bit-for-bit representation (including the class, tag
// number, and primitive/constructed bit) as DER. Constants are provided for
// universal class types, and functions are provided for building context
// specific tags. Tags can also be built from the provided constants and
// bitmasks.
using Tag = uint8_t;

// Universal class primitive types
const Tag kBool = 0x01;
const Tag kInteger = 0x02;
const Tag kBitString = 0x03;
const Tag kOctetString = 0x04;
const Tag kNull = 0x05;
const Tag kOid = 0x06;
const Tag kEnumerated = 0x0A;
const Tag kUtf8String = 0x0C;
const Tag kPrintableString = 0x13;
const Tag kTeletexString = 0x14;
const Tag kIA5String = 0x16;
const Tag kUtcTime = 0x17;
const Tag kGeneralizedTime = 0x18;
const Tag kUniversalString = 0x1C;
const Tag kBmpString = 0x1E;

// Universal class constructed types
const Tag kSequence = 0x30;
const Tag kSet = 0x31;

// Primitive/constructed bits
const uint8_t kTagPrimitive = 0x00;
const uint8_t kTagConstructed = 0x20;

// Tag classes
const uint8_t kTagUniversal = 0x00;
const uint8_t kTagApplication = 0x40;
const uint8_t kTagContextSpecific = 0x80;
const uint8_t kTagPrivate = 0xC0;

// Masks for the 3 components of a tag (class, primitive/constructed, number)
const uint8_t kTagNumberMask = 0x1F;
const uint8_t kTagConstructionMask = 0x20;
const uint8_t kTagClassMask = 0xC0;

// Creates the value for the outter tag of an explicitly tagged type.
//
// The ASN.1 keyword for this is:
//     [class_number] EXPLICIT
//
// (Note, the EXPLICIT may be omitted if the entire schema is in
// EXPLICIT mode, the default)
NET_EXPORT Tag ContextSpecificConstructed(uint8_t class_number);

NET_EXPORT Tag ContextSpecificPrimitive(uint8_t base);

NET_EXPORT bool IsConstructed(Tag tag);

}  // namespace der

}  // namespace net

#endif  // NET_DER_TAG_H_
