// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_QUIC_TAG_H_
#define NET_QUIC_CORE_QUIC_TAG_H_

#include <map>
#include <string>
#include <vector>

#include "net/quic/platform/api/quic_export.h"

namespace net {

// A QuicTag is a 32-bit used as identifiers in the QUIC handshake.  The use of
// a uint32_t seeks to provide a balance between the tyranny of magic number
// registries and the verbosity of std::strings. As far as the wire protocol is
// concerned, these are opaque, 32-bit values.
//
// Tags will often be referred to by their ASCII equivalent, e.g. EXMP. This is
// just a mnemonic for the value 0x504d5845 (little-endian version of the ASCII
// std::string E X M P).
typedef uint32_t QuicTag;
typedef std::map<QuicTag, std::string> QuicTagValueMap;
typedef std::vector<QuicTag> QuicTagVector;

// MakeQuicTag returns a value given the four bytes. For example:
//   MakeQuicTag('C', 'H', 'L', 'O');
QUIC_EXPORT_PRIVATE QuicTag MakeQuicTag(char a, char b, char c, char d);

// Returns true if |tag_vector| contains |tag|.
QUIC_EXPORT_PRIVATE bool ContainsQuicTag(const QuicTagVector& tag_vector,
                                         QuicTag tag);

// Sets |out_result| to the first tag in |our_tags| that is also in |their_tags|
// and returns true. If there is no intersection it returns false.
//
// If |out_index| is non-nullptr and a match is found then the index of that
// match in |their_tags| is written to |out_index|.
QUIC_EXPORT_PRIVATE bool FindMutualQuicTag(const QuicTagVector& our_tags,
                                           const QuicTagVector& their_tags,
                                           QuicTag* out_result,
                                           size_t* out_index);

// A utility function that converts a tag to a string. It will try to maintain
// the human friendly name if possible (i.e. kABCD -> "ABCD"), or will just
// treat it as a number if not.
QUIC_EXPORT_PRIVATE std::string QuicTagToString(QuicTag tag);

}  // namespace net

#endif  // NET_QUIC_CORE_QUIC_TAG_H_
