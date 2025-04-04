// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_TAG_H_
#define QUICHE_QUIC_CORE_QUIC_TAG_H_

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// A QuicTag is a 32-bit used as identifiers in the QUIC handshake.  The use of
// a uint32_t seeks to provide a balance between the tyranny of magic number
// registries and the verbosity of strings. As far as the wire protocol is
// concerned, these are opaque, 32-bit values.
//
// Tags will often be referred to by their ASCII equivalent, e.g. EXMP. This is
// just a mnemonic for the value 0x504d5845 (little-endian version of the ASCII
// string E X M P).
using QuicTag = uint32_t;
using QuicTagValueMap = std::map<QuicTag, std::string>;
using QuicTagVector = std::vector<QuicTag>;

// MakeQuicTag returns a value given the four bytes. For example:
//   MakeQuicTag('C', 'H', 'L', 'O');
QUICHE_EXPORT QuicTag MakeQuicTag(uint8_t a, uint8_t b, uint8_t c, uint8_t d);

// Returns true if |tag_vector| contains |tag|.
QUICHE_EXPORT bool ContainsQuicTag(const QuicTagVector& tag_vector,
                                   QuicTag tag);

// Sets |out_result| to the first tag in |our_tags| that is also in |their_tags|
// and returns true. If there is no intersection it returns false.
//
// If |out_index| is non-nullptr and a match is found then the index of that
// match in |their_tags| is written to |out_index|.
QUICHE_EXPORT bool FindMutualQuicTag(const QuicTagVector& our_tags,
                                     const QuicTagVector& their_tags,
                                     QuicTag* out_result, size_t* out_index);

// A utility function that converts a tag to a string. It will try to maintain
// the human friendly name if possible (i.e. kABCD -> "ABCD"), or will just
// treat it as a number if not.
QUICHE_EXPORT std::string QuicTagToString(QuicTag tag);

// Utility function that converts a string of the form "ABCD" to its
// corresponding QuicTag. Note that `tag_string` will have leading and trailing
// whitespace removed and will then be converted to a QuicTag as follows:
//  - If the tag string is 8 bytes in length and all bytes are valid hexidecimal
//    ASCII characters, then the returned QuicTag will have a corresponding
//    hexidecimal value.
//  - Otherwise, the QuicTag will be produced using the first four bytes of the
//    tag string, right-padding with zeroes if there are fewer than four bytes.
QUICHE_EXPORT QuicTag ParseQuicTag(absl::string_view tag_string);

// Utility function that converts a string of the form "ABCD,EFGH" to a vector
// of the form {kABCD,kEFGH}. Note the caveats on ParseQuicTag.
QUICHE_EXPORT QuicTagVector ParseQuicTagVector(absl::string_view tags_string);

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_TAG_H_
