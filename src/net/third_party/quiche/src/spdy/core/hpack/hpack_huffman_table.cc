// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/spdy/core/hpack/hpack_huffman_table.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

#include "net/third_party/quiche/src/spdy/core/hpack/hpack_output_stream.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_estimate_memory_usage.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_logging.h"

namespace spdy {

namespace {

bool SymbolLengthAndIdCompare(const HpackHuffmanSymbol& a,
                              const HpackHuffmanSymbol& b) {
  if (a.length == b.length) {
    return a.id < b.id;
  }
  return a.length < b.length;
}
bool SymbolIdCompare(const HpackHuffmanSymbol& a, const HpackHuffmanSymbol& b) {
  return a.id < b.id;
}

}  // namespace

HpackHuffmanTable::HpackHuffmanTable() : pad_bits_(0), failed_symbol_id_(0) {}

HpackHuffmanTable::~HpackHuffmanTable() = default;

bool HpackHuffmanTable::Initialize(const HpackHuffmanSymbol* input_symbols,
                                   size_t symbol_count) {
  CHECK(!IsInitialized());
  DCHECK_LE(symbol_count, std::numeric_limits<uint16_t>::max());

  std::vector<Symbol> symbols(symbol_count);
  // Validate symbol id sequence, and copy into |symbols|.
  for (uint16_t i = 0; i < symbol_count; i++) {
    if (i != input_symbols[i].id) {
      failed_symbol_id_ = i;
      return false;
    }
    symbols[i] = input_symbols[i];
  }
  // Order on length and ID ascending, to verify symbol codes are canonical.
  std::sort(symbols.begin(), symbols.end(), SymbolLengthAndIdCompare);
  if (symbols[0].code != 0) {
    failed_symbol_id_ = 0;
    return false;
  }
  for (size_t i = 1; i != symbols.size(); i++) {
    unsigned code_shift = 32 - symbols[i - 1].length;
    uint32_t code = symbols[i - 1].code + (1 << code_shift);

    if (code != symbols[i].code) {
      failed_symbol_id_ = symbols[i].id;
      return false;
    }
    if (code < symbols[i - 1].code) {
      // An integer overflow occurred. This implies the input
      // lengths do not represent a valid Huffman code.
      failed_symbol_id_ = symbols[i].id;
      return false;
    }
  }
  if (symbols.back().length < 8) {
    // At least one code (such as an EOS symbol) must be 8 bits or longer.
    // Without this, some inputs will not be encodable in a whole number
    // of bytes.
    return false;
  }
  pad_bits_ = static_cast<uint8_t>(symbols.back().code >> 24);

  // Order on symbol ID ascending.
  std::sort(symbols.begin(), symbols.end(), SymbolIdCompare);
  BuildEncodeTable(symbols);
  return true;
}

void HpackHuffmanTable::BuildEncodeTable(const std::vector<Symbol>& symbols) {
  for (size_t i = 0; i != symbols.size(); i++) {
    const Symbol& symbol = symbols[i];
    CHECK_EQ(i, symbol.id);
    code_by_id_.push_back(symbol.code);
    length_by_id_.push_back(symbol.length);
  }
}

bool HpackHuffmanTable::IsInitialized() const {
  return !code_by_id_.empty();
}

void HpackHuffmanTable::EncodeString(quiche::QuicheStringPiece in,
                                     HpackOutputStream* out) const {
  size_t bit_remnant = 0;
  for (size_t i = 0; i != in.size(); i++) {
    uint16_t symbol_id = static_cast<uint8_t>(in[i]);
    CHECK_GT(code_by_id_.size(), symbol_id);

    // Load, and shift code to low bits.
    unsigned length = length_by_id_[symbol_id];
    uint32_t code = code_by_id_[symbol_id] >> (32 - length);

    bit_remnant = (bit_remnant + length) % 8;

    if (length > 24) {
      out->AppendBits(static_cast<uint8_t>(code >> 24), length - 24);
      length = 24;
    }
    if (length > 16) {
      out->AppendBits(static_cast<uint8_t>(code >> 16), length - 16);
      length = 16;
    }
    if (length > 8) {
      out->AppendBits(static_cast<uint8_t>(code >> 8), length - 8);
      length = 8;
    }
    out->AppendBits(static_cast<uint8_t>(code), length);
  }
  if (bit_remnant != 0) {
    // Pad current byte as required.
    out->AppendBits(pad_bits_ >> bit_remnant, 8 - bit_remnant);
  }
}

size_t HpackHuffmanTable::EncodedSize(quiche::QuicheStringPiece in) const {
  size_t bit_count = 0;
  for (size_t i = 0; i != in.size(); i++) {
    uint16_t symbol_id = static_cast<uint8_t>(in[i]);
    CHECK_GT(code_by_id_.size(), symbol_id);

    bit_count += length_by_id_[symbol_id];
  }
  if (bit_count % 8 != 0) {
    bit_count += 8 - bit_count % 8;
  }
  return bit_count / 8;
}

size_t HpackHuffmanTable::EstimateMemoryUsage() const {
  return SpdyEstimateMemoryUsage(code_by_id_) +
         SpdyEstimateMemoryUsage(length_by_id_);
}

}  // namespace spdy
