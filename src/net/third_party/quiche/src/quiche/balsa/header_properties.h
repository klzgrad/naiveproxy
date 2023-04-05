// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_BALSA_HEADER_PROPERTIES_H_
#define QUICHE_BALSA_HEADER_PROPERTIES_H_

#include <cstdint>

#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quiche::header_properties {

// Returns true if RFC 2616 Section 14 (or other relevant standards or
// practices) indicates that header can have multiple values. Note that nothing
// stops clients from sending multiple values of other headers, so this may not
// be perfectly reliable in practice.
QUICHE_EXPORT bool IsMultivaluedHeader(absl::string_view header);

// An array of characters that are invalid in HTTP header field names.
// These are control characters, including \t, \n, \r, as well as space and
// (),/;<=>?@[\]{} and \x7f (see
// https://tools.ietf.org/html/rfc7230#section-3.2.6).
inline constexpr char kInvalidHeaderKeyCharList[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
    0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13,
    0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D,
    0x1E, 0x1F, ' ',  '(',  ')',  ',',  '/',  ';',  '<',  '=',
    '>',  '?',  '@',  '[',  '\\', ']',  '{',  '}',  0x7f};

// An array of characters that are invalid in HTTP header field values,
// according to RFC 7230 Section 3.2.  Valid low characters not in this array
// are \t (0x09), \n (0x0A), and \r (0x0D).
// Note that HTTP header field names are even more restrictive.
inline constexpr char kInvalidHeaderCharList[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x0B,
    0x0C, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
    0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x7F};

// Returns true if the given `c` is invalid in a header field name.
QUICHE_EXPORT bool IsInvalidHeaderKeyChar(uint8_t c);
// Returns true if the given `c` is invalid in a header field or the `value` has
// invalid characters.
QUICHE_EXPORT bool IsInvalidHeaderChar(uint8_t c);
QUICHE_EXPORT bool HasInvalidHeaderChars(absl::string_view value);

}  // namespace quiche::header_properties

#endif  // QUICHE_BALSA_HEADER_PROPERTIES_H_
