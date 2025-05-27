// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_HPACK_HPACK_CONSTANTS_H_
#define QUICHE_HTTP2_HPACK_HPACK_CONSTANTS_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "quiche/common/platform/api/quiche_export.h"

// All section references below are to
// https://httpwg.org/specs/rfc7540.html and
// https://httpwg.org/specs/rfc7541.html.

namespace spdy {

// An HpackPrefix signifies |bits| stored in the top |bit_size| bits
// of an octet.
struct QUICHE_EXPORT HpackPrefix {
  uint8_t bits;
  size_t bit_size;
};

// Represents a symbol and its Huffman code (stored in most-significant bits).
struct QUICHE_EXPORT HpackHuffmanSymbol {
  uint32_t code;
  uint8_t length;
  uint16_t id;
};

// An entry in the static table. Must be a POD in order to avoid static
// initializers, i.e. no user-defined constructors or destructors.
struct QUICHE_EXPORT HpackStaticEntry {
  const char* const name;
  const size_t name_len;
  const char* const value;
  const size_t value_len;
};

class HpackStaticTable;

// RFC 7540, 6.5.2: Initial value for SETTINGS_HEADER_TABLE_SIZE.
inline constexpr uint32_t kDefaultHeaderTableSizeSetting = 4096;

// RFC 7541, 5.2: Flag for a string literal that is stored unmodified (i.e.,
// without Huffman encoding).
inline constexpr HpackPrefix kStringLiteralIdentityEncoded = {0x0, 1};

// RFC 7541, 5.2: Flag for a Huffman-coded string literal.
inline constexpr HpackPrefix kStringLiteralHuffmanEncoded = {0x1, 1};

// RFC 7541, 6.1: Opcode for an indexed header field.
inline constexpr HpackPrefix kIndexedOpcode = {0b1, 1};

// RFC 7541, 6.2.1: Opcode for a literal header field with incremental indexing.
inline constexpr HpackPrefix kLiteralIncrementalIndexOpcode = {0b01, 2};

// RFC 7541, 6.2.2: Opcode for a literal header field without indexing.
inline constexpr HpackPrefix kLiteralNoIndexOpcode = {0b0000, 4};

// RFC 7541, 6.2.3: Opcode for a literal header field which is never indexed.
// Currently unused.
// const HpackPrefix kLiteralNeverIndexOpcode = {0b0001, 4};

// RFC 7541, 6.3: Opcode for maximum header table size update. Begins a
// varint-encoded table size with a 5-bit prefix.
inline constexpr HpackPrefix kHeaderTableSizeUpdateOpcode = {0b001, 3};

// RFC 7541, Appendix B: Huffman Code.
QUICHE_EXPORT const std::vector<HpackHuffmanSymbol>& HpackHuffmanCodeVector();

// RFC 7541, Appendix A: Static Table Definition.
QUICHE_EXPORT const std::vector<HpackStaticEntry>& HpackStaticTableVector();

// Returns a HpackStaticTable instance initialized with |kHpackStaticTable|.
// The instance is read-only, has static lifetime, and is safe to share amoung
// threads. This function is thread-safe.
QUICHE_EXPORT const HpackStaticTable& ObtainHpackStaticTable();

// RFC 7541, 8.1.2.1: Pseudo-headers start with a colon.
inline constexpr char kPseudoHeaderPrefix = ':';

}  // namespace spdy

#endif  // QUICHE_HTTP2_HPACK_HPACK_CONSTANTS_H_
