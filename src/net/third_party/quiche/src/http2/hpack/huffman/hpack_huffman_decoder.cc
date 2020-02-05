// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/hpack/huffman/hpack_huffman_decoder.h"

#include <bitset>
#include <limits>

#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"

// Terminology:
//
// Symbol - a plain text (unencoded) character (uint8), or the End-of-String
//          (EOS) symbol, 256.
//
// Code - the sequence of bits used to encode a symbol, varying in length from
//        5 bits for the most common symbols (e.g. '0', '1', and 'a'), to
//        30 bits for the least common (e.g. the EOS symbol).
//        For those symbols whose codes have the same length, their code values
//        are sorted such that the lower symbol value has a lower code value.
//
// Canonical - a symbol's cardinal value when sorted first by code length, and
//             then by symbol value. For example, canonical 0 is for ASCII '0'
//             (uint8 value 0x30), which is the first of the symbols whose code
//             is 5 bits long, and the last canonical is EOS, which is the last
//             of the symbols whose code is 30 bits long.

// TODO(jamessynge): Remove use of binary literals, that is a C++ 14 feature.

namespace http2 {
namespace {

// HuffmanCode is used to store the codes associated with symbols (a pattern of
// from 5 to 30 bits).
typedef uint32_t HuffmanCode;

// HuffmanCodeBitCount is used to store a count of bits in a code.
typedef uint16_t HuffmanCodeBitCount;

// HuffmanCodeBitSet is used for producing a string version of a code because
// std::bitset logs nicely.
typedef std::bitset<32> HuffmanCodeBitSet;
typedef std::bitset<64> HuffmanAccumulatorBitSet;

static constexpr HuffmanCodeBitCount kMinCodeBitCount = 5;
static constexpr HuffmanCodeBitCount kMaxCodeBitCount = 30;
static constexpr HuffmanCodeBitCount kHuffmanCodeBitCount =
    std::numeric_limits<HuffmanCode>::digits;

static_assert(std::numeric_limits<HuffmanCode>::digits >= kMaxCodeBitCount,
              "HuffmanCode isn't big enough.");

static_assert(std::numeric_limits<HuffmanAccumulator>::digits >=
                  kMaxCodeBitCount,
              "HuffmanAccumulator isn't big enough.");

static constexpr HuffmanAccumulatorBitCount kHuffmanAccumulatorBitCount =
    std::numeric_limits<HuffmanAccumulator>::digits;
static constexpr HuffmanAccumulatorBitCount kExtraAccumulatorBitCount =
    kHuffmanAccumulatorBitCount - kHuffmanCodeBitCount;

// PrefixInfo holds info about a group of codes that are all of the same length.
struct PrefixInfo {
  // Given the leading bits (32 in this case) of the encoded string, and that
  // they start with a code of length |code_length|, return the corresponding
  // canonical for that leading code.
  uint32_t DecodeToCanonical(HuffmanCode bits) const {
    // What is the position of the canonical symbol being decoded within
    // the canonical symbols of |length|?
    HuffmanCode ordinal_in_length =
        ((bits - first_code) >> (kHuffmanCodeBitCount - code_length));

    // Combined with |canonical| to produce the position of the canonical symbol
    // being decoded within all of the canonical symbols.
    return first_canonical + ordinal_in_length;
  }

  const HuffmanCode first_code;  // First code of this length, left justified in
                                 // the field (i.e. the first bit of the code is
                                 // the high-order bit).
  const uint16_t code_length;    // Length of the prefix code |base|.
  const uint16_t first_canonical;  // First canonical symbol of this length.
};

inline std::ostream& operator<<(std::ostream& out, const PrefixInfo& v) {
  return out << "{first_code: " << HuffmanCodeBitSet(v.first_code)
             << ", code_length: " << v.code_length
             << ", first_canonical: " << v.first_canonical << "}";
}

// Given |value|, a sequence of the leading bits remaining to be decoded,
// figure out which group of canonicals (by code length) that value starts
// with. This function was generated.
PrefixInfo PrefixToInfo(HuffmanCode value) {
  if (value < 0b10111000000000000000000000000000) {
    if (value < 0b01010000000000000000000000000000) {
      return {0b00000000000000000000000000000000, 5, 0};
    } else {
      return {0b01010000000000000000000000000000, 6, 10};
    }
  } else {
    if (value < 0b11111110000000000000000000000000) {
      if (value < 0b11111000000000000000000000000000) {
        return {0b10111000000000000000000000000000, 7, 36};
      } else {
        return {0b11111000000000000000000000000000, 8, 68};
      }
    } else {
      if (value < 0b11111111110000000000000000000000) {
        if (value < 0b11111111101000000000000000000000) {
          if (value < 0b11111111010000000000000000000000) {
            return {0b11111110000000000000000000000000, 10, 74};
          } else {
            return {0b11111111010000000000000000000000, 11, 79};
          }
        } else {
          return {0b11111111101000000000000000000000, 12, 82};
        }
      } else {
        if (value < 0b11111111111111100000000000000000) {
          if (value < 0b11111111111110000000000000000000) {
            if (value < 0b11111111111100000000000000000000) {
              return {0b11111111110000000000000000000000, 13, 84};
            } else {
              return {0b11111111111100000000000000000000, 14, 90};
            }
          } else {
            return {0b11111111111110000000000000000000, 15, 92};
          }
        } else {
          if (value < 0b11111111111111110100100000000000) {
            if (value < 0b11111111111111101110000000000000) {
              if (value < 0b11111111111111100110000000000000) {
                return {0b11111111111111100000000000000000, 19, 95};
              } else {
                return {0b11111111111111100110000000000000, 20, 98};
              }
            } else {
              return {0b11111111111111101110000000000000, 21, 106};
            }
          } else {
            if (value < 0b11111111111111111110101000000000) {
              if (value < 0b11111111111111111011000000000000) {
                return {0b11111111111111110100100000000000, 22, 119};
              } else {
                return {0b11111111111111111011000000000000, 23, 145};
              }
            } else {
              if (value < 0b11111111111111111111101111000000) {
                if (value < 0b11111111111111111111100000000000) {
                  if (value < 0b11111111111111111111011000000000) {
                    return {0b11111111111111111110101000000000, 24, 174};
                  } else {
                    return {0b11111111111111111111011000000000, 25, 186};
                  }
                } else {
                  return {0b11111111111111111111100000000000, 26, 190};
                }
              } else {
                if (value < 0b11111111111111111111111111110000) {
                  if (value < 0b11111111111111111111111000100000) {
                    return {0b11111111111111111111101111000000, 27, 205};
                  } else {
                    return {0b11111111111111111111111000100000, 28, 224};
                  }
                } else {
                  return {0b11111111111111111111111111110000, 30, 253};
                }
              }
            }
          }
        }
      }
    }
  }
}

// Mapping from canonical symbol (0 to 255) to actual symbol.
// clang-format off
constexpr unsigned char kCanonicalToSymbol[] = {
    '0',  '1',  '2',  'a',  'c',  'e',  'i',  'o',
    's',  't',  0x20, '%',  '-',  '.',  '/',  '3',
    '4',  '5',  '6',  '7',  '8',  '9',  '=',  'A',
    '_',  'b',  'd',  'f',  'g',  'h',  'l',  'm',
    'n',  'p',  'r',  'u',  ':',  'B',  'C',  'D',
    'E',  'F',  'G',  'H',  'I',  'J',  'K',  'L',
    'M',  'N',  'O',  'P',  'Q',  'R',  'S',  'T',
    'U',  'V',  'W',  'Y',  'j',  'k',  'q',  'v',
    'w',  'x',  'y',  'z',  '&',  '*',  ',',  ';',
    'X',  'Z',  '!',  '\"', '(',  ')',  '?',  '\'',
    '+',  '|',  '#',  '>',  0x00, '$',  '@',  '[',
    ']',  '~',  '^',  '}',  '<',  '`',  '{',  '\\',
    0xc3, 0xd0, 0x80, 0x82, 0x83, 0xa2, 0xb8, 0xc2,
    0xe0, 0xe2, 0x99, 0xa1, 0xa7, 0xac, 0xb0, 0xb1,
    0xb3, 0xd1, 0xd8, 0xd9, 0xe3, 0xe5, 0xe6, 0x81,
    0x84, 0x85, 0x86, 0x88, 0x92, 0x9a, 0x9c, 0xa0,
    0xa3, 0xa4, 0xa9, 0xaa, 0xad, 0xb2, 0xb5, 0xb9,
    0xba, 0xbb, 0xbd, 0xbe, 0xc4, 0xc6, 0xe4, 0xe8,
    0xe9, 0x01, 0x87, 0x89, 0x8a, 0x8b, 0x8c, 0x8d,
    0x8f, 0x93, 0x95, 0x96, 0x97, 0x98, 0x9b, 0x9d,
    0x9e, 0xa5, 0xa6, 0xa8, 0xae, 0xaf, 0xb4, 0xb6,
    0xb7, 0xbc, 0xbf, 0xc5, 0xe7, 0xef, 0x09, 0x8e,
    0x90, 0x91, 0x94, 0x9f, 0xab, 0xce, 0xd7, 0xe1,
    0xec, 0xed, 0xc7, 0xcf, 0xea, 0xeb, 0xc0, 0xc1,
    0xc8, 0xc9, 0xca, 0xcd, 0xd2, 0xd5, 0xda, 0xdb,
    0xee, 0xf0, 0xf2, 0xf3, 0xff, 0xcb, 0xcc, 0xd3,
    0xd4, 0xd6, 0xdd, 0xde, 0xdf, 0xf1, 0xf4, 0xf5,
    0xf6, 0xf7, 0xf8, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe,
    0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x0b,
    0x0c, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14,
    0x15, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d,
    0x1e, 0x1f, 0x7f, 0xdc, 0xf9, 0x0a, 0x0d, 0x16,
};
// clang-format on

constexpr size_t kShortCodeTableSize = 124;
struct ShortCodeInfo {
  uint8_t symbol;
  uint8_t length;
} kShortCodeTable[kShortCodeTableSize] = {
    {0x30, 5},  // Match: 0b0000000, Symbol: 0
    {0x30, 5},  // Match: 0b0000001, Symbol: 0
    {0x30, 5},  // Match: 0b0000010, Symbol: 0
    {0x30, 5},  // Match: 0b0000011, Symbol: 0
    {0x31, 5},  // Match: 0b0000100, Symbol: 1
    {0x31, 5},  // Match: 0b0000101, Symbol: 1
    {0x31, 5},  // Match: 0b0000110, Symbol: 1
    {0x31, 5},  // Match: 0b0000111, Symbol: 1
    {0x32, 5},  // Match: 0b0001000, Symbol: 2
    {0x32, 5},  // Match: 0b0001001, Symbol: 2
    {0x32, 5},  // Match: 0b0001010, Symbol: 2
    {0x32, 5},  // Match: 0b0001011, Symbol: 2
    {0x61, 5},  // Match: 0b0001100, Symbol: a
    {0x61, 5},  // Match: 0b0001101, Symbol: a
    {0x61, 5},  // Match: 0b0001110, Symbol: a
    {0x61, 5},  // Match: 0b0001111, Symbol: a
    {0x63, 5},  // Match: 0b0010000, Symbol: c
    {0x63, 5},  // Match: 0b0010001, Symbol: c
    {0x63, 5},  // Match: 0b0010010, Symbol: c
    {0x63, 5},  // Match: 0b0010011, Symbol: c
    {0x65, 5},  // Match: 0b0010100, Symbol: e
    {0x65, 5},  // Match: 0b0010101, Symbol: e
    {0x65, 5},  // Match: 0b0010110, Symbol: e
    {0x65, 5},  // Match: 0b0010111, Symbol: e
    {0x69, 5},  // Match: 0b0011000, Symbol: i
    {0x69, 5},  // Match: 0b0011001, Symbol: i
    {0x69, 5},  // Match: 0b0011010, Symbol: i
    {0x69, 5},  // Match: 0b0011011, Symbol: i
    {0x6f, 5},  // Match: 0b0011100, Symbol: o
    {0x6f, 5},  // Match: 0b0011101, Symbol: o
    {0x6f, 5},  // Match: 0b0011110, Symbol: o
    {0x6f, 5},  // Match: 0b0011111, Symbol: o
    {0x73, 5},  // Match: 0b0100000, Symbol: s
    {0x73, 5},  // Match: 0b0100001, Symbol: s
    {0x73, 5},  // Match: 0b0100010, Symbol: s
    {0x73, 5},  // Match: 0b0100011, Symbol: s
    {0x74, 5},  // Match: 0b0100100, Symbol: t
    {0x74, 5},  // Match: 0b0100101, Symbol: t
    {0x74, 5},  // Match: 0b0100110, Symbol: t
    {0x74, 5},  // Match: 0b0100111, Symbol: t
    {0x20, 6},  // Match: 0b0101000, Symbol: (space)
    {0x20, 6},  // Match: 0b0101001, Symbol: (space)
    {0x25, 6},  // Match: 0b0101010, Symbol: %
    {0x25, 6},  // Match: 0b0101011, Symbol: %
    {0x2d, 6},  // Match: 0b0101100, Symbol: -
    {0x2d, 6},  // Match: 0b0101101, Symbol: -
    {0x2e, 6},  // Match: 0b0101110, Symbol: .
    {0x2e, 6},  // Match: 0b0101111, Symbol: .
    {0x2f, 6},  // Match: 0b0110000, Symbol: /
    {0x2f, 6},  // Match: 0b0110001, Symbol: /
    {0x33, 6},  // Match: 0b0110010, Symbol: 3
    {0x33, 6},  // Match: 0b0110011, Symbol: 3
    {0x34, 6},  // Match: 0b0110100, Symbol: 4
    {0x34, 6},  // Match: 0b0110101, Symbol: 4
    {0x35, 6},  // Match: 0b0110110, Symbol: 5
    {0x35, 6},  // Match: 0b0110111, Symbol: 5
    {0x36, 6},  // Match: 0b0111000, Symbol: 6
    {0x36, 6},  // Match: 0b0111001, Symbol: 6
    {0x37, 6},  // Match: 0b0111010, Symbol: 7
    {0x37, 6},  // Match: 0b0111011, Symbol: 7
    {0x38, 6},  // Match: 0b0111100, Symbol: 8
    {0x38, 6},  // Match: 0b0111101, Symbol: 8
    {0x39, 6},  // Match: 0b0111110, Symbol: 9
    {0x39, 6},  // Match: 0b0111111, Symbol: 9
    {0x3d, 6},  // Match: 0b1000000, Symbol: =
    {0x3d, 6},  // Match: 0b1000001, Symbol: =
    {0x41, 6},  // Match: 0b1000010, Symbol: A
    {0x41, 6},  // Match: 0b1000011, Symbol: A
    {0x5f, 6},  // Match: 0b1000100, Symbol: _
    {0x5f, 6},  // Match: 0b1000101, Symbol: _
    {0x62, 6},  // Match: 0b1000110, Symbol: b
    {0x62, 6},  // Match: 0b1000111, Symbol: b
    {0x64, 6},  // Match: 0b1001000, Symbol: d
    {0x64, 6},  // Match: 0b1001001, Symbol: d
    {0x66, 6},  // Match: 0b1001010, Symbol: f
    {0x66, 6},  // Match: 0b1001011, Symbol: f
    {0x67, 6},  // Match: 0b1001100, Symbol: g
    {0x67, 6},  // Match: 0b1001101, Symbol: g
    {0x68, 6},  // Match: 0b1001110, Symbol: h
    {0x68, 6},  // Match: 0b1001111, Symbol: h
    {0x6c, 6},  // Match: 0b1010000, Symbol: l
    {0x6c, 6},  // Match: 0b1010001, Symbol: l
    {0x6d, 6},  // Match: 0b1010010, Symbol: m
    {0x6d, 6},  // Match: 0b1010011, Symbol: m
    {0x6e, 6},  // Match: 0b1010100, Symbol: n
    {0x6e, 6},  // Match: 0b1010101, Symbol: n
    {0x70, 6},  // Match: 0b1010110, Symbol: p
    {0x70, 6},  // Match: 0b1010111, Symbol: p
    {0x72, 6},  // Match: 0b1011000, Symbol: r
    {0x72, 6},  // Match: 0b1011001, Symbol: r
    {0x75, 6},  // Match: 0b1011010, Symbol: u
    {0x75, 6},  // Match: 0b1011011, Symbol: u
    {0x3a, 7},  // Match: 0b1011100, Symbol: :
    {0x42, 7},  // Match: 0b1011101, Symbol: B
    {0x43, 7},  // Match: 0b1011110, Symbol: C
    {0x44, 7},  // Match: 0b1011111, Symbol: D
    {0x45, 7},  // Match: 0b1100000, Symbol: E
    {0x46, 7},  // Match: 0b1100001, Symbol: F
    {0x47, 7},  // Match: 0b1100010, Symbol: G
    {0x48, 7},  // Match: 0b1100011, Symbol: H
    {0x49, 7},  // Match: 0b1100100, Symbol: I
    {0x4a, 7},  // Match: 0b1100101, Symbol: J
    {0x4b, 7},  // Match: 0b1100110, Symbol: K
    {0x4c, 7},  // Match: 0b1100111, Symbol: L
    {0x4d, 7},  // Match: 0b1101000, Symbol: M
    {0x4e, 7},  // Match: 0b1101001, Symbol: N
    {0x4f, 7},  // Match: 0b1101010, Symbol: O
    {0x50, 7},  // Match: 0b1101011, Symbol: P
    {0x51, 7},  // Match: 0b1101100, Symbol: Q
    {0x52, 7},  // Match: 0b1101101, Symbol: R
    {0x53, 7},  // Match: 0b1101110, Symbol: S
    {0x54, 7},  // Match: 0b1101111, Symbol: T
    {0x55, 7},  // Match: 0b1110000, Symbol: U
    {0x56, 7},  // Match: 0b1110001, Symbol: V
    {0x57, 7},  // Match: 0b1110010, Symbol: W
    {0x59, 7},  // Match: 0b1110011, Symbol: Y
    {0x6a, 7},  // Match: 0b1110100, Symbol: j
    {0x6b, 7},  // Match: 0b1110101, Symbol: k
    {0x71, 7},  // Match: 0b1110110, Symbol: q
    {0x76, 7},  // Match: 0b1110111, Symbol: v
    {0x77, 7},  // Match: 0b1111000, Symbol: w
    {0x78, 7},  // Match: 0b1111001, Symbol: x
    {0x79, 7},  // Match: 0b1111010, Symbol: y
    {0x7a, 7},  // Match: 0b1111011, Symbol: z
};

}  // namespace

HuffmanBitBuffer::HuffmanBitBuffer() {
  Reset();
}

void HuffmanBitBuffer::Reset() {
  accumulator_ = 0;
  count_ = 0;
}

size_t HuffmanBitBuffer::AppendBytes(Http2StringPiece input) {
  HuffmanAccumulatorBitCount free_cnt = free_count();
  size_t bytes_available = input.size();
  if (free_cnt < 8 || bytes_available == 0) {
    return 0;
  }

  // Top up |accumulator_| until there isn't room for a whole byte.
  size_t bytes_used = 0;
  auto* ptr = reinterpret_cast<const uint8_t*>(input.data());
  do {
    auto b = static_cast<HuffmanAccumulator>(*ptr++);
    free_cnt -= 8;
    accumulator_ |= (b << free_cnt);
    ++bytes_used;
  } while (free_cnt >= 8 && bytes_used < bytes_available);
  count_ += (bytes_used * 8);
  return bytes_used;
}

HuffmanAccumulatorBitCount HuffmanBitBuffer::free_count() const {
  return kHuffmanAccumulatorBitCount - count_;
}

void HuffmanBitBuffer::ConsumeBits(HuffmanAccumulatorBitCount code_length) {
  DCHECK_LE(code_length, count_);
  accumulator_ <<= code_length;
  count_ -= code_length;
}

bool HuffmanBitBuffer::InputProperlyTerminated() const {
  auto cnt = count();
  if (cnt < 8) {
    if (cnt == 0) {
      return true;
    }
    HuffmanAccumulator expected = ~(~HuffmanAccumulator() >> cnt);
    // We expect all the bits below the high order |cnt| bits of accumulator_
    // to be cleared as we perform left shift operations while decoding.
    DCHECK_EQ(accumulator_ & ~expected, 0u)
        << "\n  expected: " << HuffmanAccumulatorBitSet(expected) << "\n  "
        << *this;
    return accumulator_ == expected;
  }
  return false;
}

std::string HuffmanBitBuffer::DebugString() const {
  std::stringstream ss;
  ss << "{accumulator: " << HuffmanAccumulatorBitSet(accumulator_)
     << "; count: " << count_ << "}";
  return ss.str();
}

HpackHuffmanDecoder::HpackHuffmanDecoder() = default;

HpackHuffmanDecoder::~HpackHuffmanDecoder() = default;

bool HpackHuffmanDecoder::Decode(Http2StringPiece input, std::string* output) {
  HTTP2_DVLOG(1) << "HpackHuffmanDecoder::Decode";

  // Fill bit_buffer_ from input.
  input.remove_prefix(bit_buffer_.AppendBytes(input));

  while (true) {
    HTTP2_DVLOG(3) << "Enter Decode Loop, bit_buffer_: " << bit_buffer_;
    if (bit_buffer_.count() >= 7) {
      // Get high 7 bits of the bit buffer, see if that contains a complete
      // code of 5, 6 or 7 bits.
      uint8_t short_code =
          bit_buffer_.value() >> (kHuffmanAccumulatorBitCount - 7);
      DCHECK_LT(short_code, 128);
      if (short_code < kShortCodeTableSize) {
        ShortCodeInfo info = kShortCodeTable[short_code];
        bit_buffer_.ConsumeBits(info.length);
        output->push_back(static_cast<char>(info.symbol));
        continue;
      }
      // The code is more than 7 bits long. Use PrefixToInfo, etc. to decode
      // longer codes.
    } else {
      // We may have (mostly) drained bit_buffer_. If we can top it up, try
      // using the table decoder above.
      size_t byte_count = bit_buffer_.AppendBytes(input);
      if (byte_count > 0) {
        input.remove_prefix(byte_count);
        continue;
      }
    }

    HuffmanCode code_prefix = bit_buffer_.value() >> kExtraAccumulatorBitCount;
    HTTP2_DVLOG(3) << "code_prefix: " << HuffmanCodeBitSet(code_prefix);

    PrefixInfo prefix_info = PrefixToInfo(code_prefix);
    HTTP2_DVLOG(3) << "prefix_info: " << prefix_info;
    DCHECK_LE(kMinCodeBitCount, prefix_info.code_length);
    DCHECK_LE(prefix_info.code_length, kMaxCodeBitCount);

    if (prefix_info.code_length <= bit_buffer_.count()) {
      // We have enough bits for one code.
      uint32_t canonical = prefix_info.DecodeToCanonical(code_prefix);
      if (canonical < 256) {
        // Valid code.
        char c = kCanonicalToSymbol[canonical];
        output->push_back(c);
        bit_buffer_.ConsumeBits(prefix_info.code_length);
        continue;
      }
      // Encoder is not supposed to explicity encode the EOS symbol.
      HTTP2_DLOG(ERROR) << "EOS explicitly encoded!\n " << bit_buffer_ << "\n "
                        << prefix_info;
      return false;
    }
    // bit_buffer_ doesn't have enough bits in it to decode the next symbol.
    // Append to it as many bytes as are available AND fit.
    size_t byte_count = bit_buffer_.AppendBytes(input);
    if (byte_count == 0) {
      DCHECK_EQ(input.size(), 0u);
      return true;
    }
    input.remove_prefix(byte_count);
  }
}

std::string HpackHuffmanDecoder::DebugString() const {
  return bit_buffer_.DebugString();
}

}  // namespace http2
