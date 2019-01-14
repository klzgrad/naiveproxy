// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/http2/hpack/varint/hpack_varint_decoder.h"

// Benchmarks of decoding HPACK variable length integers.

// clang-format off
/*
Results from 2016-04-13 Perflab runs on arch=ixion_haswell, averaged over 10
trials. Times are in picoseconds, which indicates how small a component of the
overall decoding time is taken up by varints, even though each HPACK entry has
between 1 and 3 of them.

In the table, RSD means Relative Standard Deviation, i.e. the standard deviation
of the trial values as a percentage of the mean. A large RSD indicates that the
benchmark isn't very stable.

SB# is the number of bytes in the encoding of the varint, where SBX means a
variable number of bytes was used based on a population model.

PL# is the number of bits of the first byte that make up the prefix of the
varint. PLX won't be available until the HpackEntryTypeDecoder benchmarks.

Inline(Both|None)(Extended)? indicate whether the Start and Resume calls
were (Both) or were not (None) inlined, and Extended indicates whether the
caller skipped calling the decoder if the varint was encoded in only one byte
(as we can expect is common for strings).

The rows are sorted by PL#, SB#, and finally cpu ps.

My conclusion is that InlineBoth is the best choice for how to call the
HpackVarintDecoder (i.e. leave the Start and Resume methods in the headers,
and don't special case 1 byte varints in the caller).

Benchmark                                      wall ps (RSD%)   cpu ps (RSD%)  Throughput (RSD%)  Trials
---------------------------------------------  --------------   -------------  -----------------  ------
SB1_PL4_InlineBoth_HpackVarintDecoder           1936.5 ( 0.4)   1932.3 ( 0.5)   471 MiB/s ( 0.5)      10
SB1_PL4_InlineNoneExtended_HpackVarintDecoder   2009.4 ( 0.4)   2006.1 ( 0.6)   453 MiB/s ( 0.6)      10
SB1_PL4_InlineBothExtended_HpackVarintDecoder   2010.6 ( 0.7)   2008.1 ( 0.8)   453 MiB/s ( 0.8)      10
SB1_PL4_InlineNone_HpackVarintDecoder           2258.3 ( 0.6)   2257.8 ( 0.6)   403 MiB/s ( 0.6)      10

SB2_PL4_InlineBothExtended_HpackVarintDecoder   3472.9 ( 0.5)   3468.8 ( 0.4)   524 MiB/s ( 0.4)      10
SB2_PL4_InlineBoth_HpackVarintDecoder           3933.3 ( 0.5)   3926.2 ( 0.5)   463 MiB/s ( 0.5)      10
SB2_PL4_InlineNoneExtended_HpackVarintDecoder   4338.7 ( 0.8)   4328.9 ( 0.6)   420 MiB/s ( 0.6)      10
SB2_PL4_InlineNone_HpackVarintDecoder           4416.7 ( 0.5)   4411.7 ( 0.6)   412 MiB/s ( 0.6)      10

SB3_PL4_InlineBothExtended_HpackVarintDecoder   5369.1 ( 0.6)   5347.5 ( 0.7)   510 MiB/s ( 0.7)      10
SB3_PL4_InlineNoneExtended_HpackVarintDecoder   5749.8 ( 0.6)   5744.9 ( 0.8)   475 MiB/s ( 0.8)      10
SB3_PL4_InlineBoth_HpackVarintDecoder           5775.7 ( 0.9)   5760.3 ( 1.0)   474 MiB/s ( 1.0)      10
SB3_PL4_InlineNone_HpackVarintDecoder           6003.6 ( 0.5)   5991.0 ( 0.5)   455 MiB/s ( 0.5)      10

SB4_PL4_InlineBothExtended_HpackVarintDecoder   7237.1 ( 0.2)   7229.5 ( 0.2)   503 MiB/s ( 0.2)      10
SB4_PL4_InlineNoneExtended_HpackVarintDecoder   7374.9 ( 0.5)   7353.3 ( 0.5)   495 MiB/s ( 0.5)      10
SB4_PL4_InlineBoth_HpackVarintDecoder           7471.2 ( 0.3)   7462.8 ( 0.6)   487 MiB/s ( 0.6)      10
SB4_PL4_InlineNone_HpackVarintDecoder           7710.5 ( 1.2)   7696.3 ( 1.1)   473 MiB/s ( 1.2)      10

SBX_PL4_InlineBothExtended_HpackVarintDecoder   4586.1 ( 0.2)   4570.9 ( 0.2)   364 MiB/s ( 0.2)      10
SBX_PL4_InlineBoth_HpackVarintDecoder           4769.8 ( 0.2)   4765.1 ( 0.3)   349 MiB/s ( 0.3)      10
SBX_PL4_InlineNoneExtended_HpackVarintDecoder   4961.6 ( 0.3)   4955.3 ( 0.2)   336 MiB/s ( 0.2)      10
SBX_PL4_InlineNone_HpackVarintDecoder           5444.5 ( 0.2)   5438.7 ( 0.4)   306 MiB/s ( 0.4)      10

SB1_PL5_InlineBoth_HpackVarintDecoder           1942.0 ( 0.3)   1938.2 ( 0.3)   469 MiB/s ( 0.4)      10
SB1_PL5_InlineBothExtended_HpackVarintDecoder   2006.6 ( 0.4)   2005.5 ( 0.5)   453 MiB/s ( 0.5)      10
SB1_PL5_InlineNoneExtended_HpackVarintDecoder   2010.2 ( 0.3)   2006.5 ( 0.4)   453 MiB/s ( 0.4)      10
SB1_PL5_InlineNone_HpackVarintDecoder           2266.1 ( 0.6)   2264.5 ( 0.6)   401 MiB/s ( 0.6)      10

SB2_PL5_InlineBothExtended_HpackVarintDecoder   3470.4 ( 0.2)   3463.4 ( 0.3)   525 MiB/s ( 0.3)      10
SB2_PL5_InlineBoth_HpackVarintDecoder           3929.0 ( 0.3)   3920.1 ( 0.4)   464 MiB/s ( 0.4)      10
SB2_PL5_InlineNoneExtended_HpackVarintDecoder   4002.2 ( 0.6)   3996.7 ( 0.7)   455 MiB/s ( 0.7)      10
SB2_PL5_InlineNone_HpackVarintDecoder           4414.5 ( 0.3)   4412.0 ( 0.4)   412 MiB/s ( 0.4)      10

SB3_PL5_InlineBothExtended_HpackVarintDecoder   5368.7 ( 0.4)   5364.0 ( 0.3)   509 MiB/s ( 0.3)      10
SB3_PL5_InlineBoth_HpackVarintDecoder           5754.0 ( 0.4)   5749.6 ( 0.4)   475 MiB/s ( 0.4)      10
SB3_PL5_InlineNoneExtended_HpackVarintDecoder   5767.8 ( 0.5)   5769.9 ( 0.8)   473 MiB/s ( 0.8)      10
SB3_PL5_InlineNone_HpackVarintDecoder           6054.8 ( 0.9)   6053.3 ( 0.7)   451 MiB/s ( 0.7)      10

SB4_PL5_InlineBothExtended_HpackVarintDecoder   7243.2 ( 0.6)   7220.1 ( 0.6)   504 MiB/s ( 0.6)      10
SB4_PL5_InlineNoneExtended_HpackVarintDecoder   7376.1 ( 0.4)   7365.5 ( 0.3)   494 MiB/s ( 0.3)      10
SB4_PL5_InlineBoth_HpackVarintDecoder           7482.2 ( 0.5)   7477.7 ( 0.7)   487 MiB/s ( 0.7)      10
SB4_PL5_InlineNone_HpackVarintDecoder           7707.7 ( 1.0)   7710.3 ( 1.1)   472 MiB/s ( 1.2)      10

SBX_PL5_InlineBothExtended_HpackVarintDecoder   8457.8 ( 0.5)   8443.1 ( 0.5)   345 MiB/s ( 0.5)      10
SBX_PL5_InlineBoth_HpackVarintDecoder           8598.6 ( 0.4)   8580.9 ( 0.7)   339 MiB/s ( 0.7)      10
SBX_PL5_InlineNoneExtended_HpackVarintDecoder   8762.8 ( 0.2)   8736.5 ( 0.2)   333 MiB/s ( 0.2)      10
SBX_PL5_InlineNone_HpackVarintDecoder           9354.8 ( 0.3)   9338.9 ( 0.3)   312 MiB/s ( 0.3)      10

SB1_PL6_InlineBoth_HpackVarintDecoder           1936.5 ( 0.2)   1934.3 ( 0.4)   470 MiB/s ( 0.4)      10
SB1_PL6_InlineNoneExtended_HpackVarintDecoder   2010.3 ( 0.4)   2006.8 ( 0.3)   453 MiB/s ( 0.3)      10
SB1_PL6_InlineBothExtended_HpackVarintDecoder   2014.2 ( 0.6)   2009.5 ( 0.5)   453 MiB/s ( 0.5)      10
SB1_PL6_InlineNone_HpackVarintDecoder           2260.2 ( 0.3)   2257.4 ( 0.2)   403 MiB/s ( 0.2)      10

SB2_PL6_InlineBothExtended_HpackVarintDecoder   3480.1 ( 0.5)   3475.5 ( 0.5)   523 MiB/s ( 0.5)      10
SB2_PL6_InlineBoth_HpackVarintDecoder           3934.1 ( 0.5)   3928.5 ( 0.7)   463 MiB/s ( 0.7)      10
SB2_PL6_InlineNoneExtended_HpackVarintDecoder   3954.3 ( 0.5)   3951.6 ( 0.5)   460 MiB/s ( 0.5)      10
SB2_PL6_InlineNone_HpackVarintDecoder           4422.7 ( 0.4)   4418.2 ( 0.5)   412 MiB/s ( 0.5)      10

SB3_PL6_InlineBothExtended_HpackVarintDecoder   5358.1 ( 0.4)   5355.8 ( 0.4)   509 MiB/s ( 0.4)      10
SB3_PL6_InlineBoth_HpackVarintDecoder           5740.8 ( 0.6)   5722.2 ( 0.3)   477 MiB/s ( 0.3)      10
SB3_PL6_InlineNoneExtended_HpackVarintDecoder   5766.6 ( 0.4)   5762.1 ( 0.6)   474 MiB/s ( 0.6)      10
SB3_PL6_InlineNone_HpackVarintDecoder           6206.9 ( 0.9)   6204.7 ( 0.8)   440 MiB/s ( 0.8)      10

SB4_PL6_InlineBothExtended_HpackVarintDecoder   7262.3 ( 0.8)   7252.2 ( 0.8)   502 MiB/s ( 0.8)      10
SB4_PL6_InlineNoneExtended_HpackVarintDecoder   7398.2 ( 0.5)   7392.7 ( 0.5)   492 MiB/s ( 0.5)      10
SB4_PL6_InlineBoth_HpackVarintDecoder           7478.2 ( 0.4)   7476.4 ( 0.7)   487 MiB/s ( 0.7)      10
SB4_PL6_InlineNone_HpackVarintDecoder           7792.7 ( 0.6)   7772.6 ( 0.4)   468 MiB/s ( 0.4)      10

SBX_PL6_InlineBoth_HpackVarintDecoder           4999.6 ( 0.5)   4987.7 ( 0.5)   251 MiB/s ( 0.5)      10
SBX_PL6_InlineBothExtended_HpackVarintDecoder   5026.8 ( 0.2)   5019.7 ( 0.2)   249 MiB/s ( 0.2)      10
SBX_PL6_InlineNoneExtended_HpackVarintDecoder   5324.5 ( 0.4)   5316.8 ( 0.6)   235 MiB/s ( 0.6)      10
SBX_PL6_InlineNone_HpackVarintDecoder           5645.6 ( 0.3)   5636.3 ( 0.3)   222 MiB/s ( 0.3)      10

SB1_PL7_InlineBoth_HpackVarintDecoder           1944.7 ( 0.5)   1942.0 ( 0.8)   468 MiB/s ( 0.8)      10
SB1_PL7_InlineNoneExtended_HpackVarintDecoder   2006.2 ( 0.1)   2001.8 ( 0.4)   454 MiB/s ( 0.4)      10
SB1_PL7_InlineBothExtended_HpackVarintDecoder   2009.2 ( 0.5)   2004.9 ( 0.6)   454 MiB/s ( 0.6)      10
SB1_PL7_InlineNone_HpackVarintDecoder           2260.9 ( 0.4)   2259.0 ( 0.3)   403 MiB/s ( 0.4)      10

SB2_PL7_InlineBothExtended_HpackVarintDecoder   3473.8 ( 0.3)   3468.7 ( 0.4)   524 MiB/s ( 0.4)      10
SB2_PL7_InlineBoth_HpackVarintDecoder           3935.3 ( 0.5)   3923.9 ( 0.4)   464 MiB/s ( 0.4)      10
SB2_PL7_InlineNoneExtended_HpackVarintDecoder   3936.1 ( 0.2)   3930.9 ( 0.4)   463 MiB/s ( 0.4)      10
SB2_PL7_InlineNone_HpackVarintDecoder           4422.5 ( 0.4)   4418.3 ( 0.3)   412 MiB/s ( 0.3)      10

SB3_PL7_InlineBothExtended_HpackVarintDecoder   5373.6 ( 0.9)   5365.3 ( 1.0)   509 MiB/s ( 0.9)      10
SB3_PL7_InlineBoth_HpackVarintDecoder           5748.5 ( 0.3)   5731.4 ( 0.2)   476 MiB/s ( 0.2)      10
SB3_PL7_InlineNoneExtended_HpackVarintDecoder   5738.0 ( 0.4)   5736.7 ( 0.5)   476 MiB/s ( 0.5)      10
SB3_PL7_InlineNone_HpackVarintDecoder           5997.3 ( 0.4)   5989.5 ( 0.4)   456 MiB/s ( 0.4)      10

SB4_PL7_InlineBothExtended_HpackVarintDecoder   7246.4 ( 0.6)   7240.9 ( 0.4)   502 MiB/s ( 0.5)      10
SB4_PL7_InlineNoneExtended_HpackVarintDecoder   7401.0 ( 0.4)   7389.7 ( 0.5)   492 MiB/s ( 0.5)      10
SB4_PL7_InlineBoth_HpackVarintDecoder           7480.3 ( 0.6)   7471.1 ( 0.4)   487 MiB/s ( 0.4)      10
SB4_PL7_InlineNone_HpackVarintDecoder           7737.9 ( 1.0)   7722.1 ( 0.7)   471 MiB/s ( 0.7)      10

SBX_PL7_InlineBoth_HpackVarintDecoder           2098.0 ( 0.1)   2097.3 ( 0.3)   441 MiB/s ( 0.3)      10
SBX_PL7_InlineNoneExtended_HpackVarintDecoder   2143.9 ( 0.4)   2139.4 ( 0.2)   432 MiB/s ( 0.2)      10
SBX_PL7_InlineBothExtended_HpackVarintDecoder   2161.6 ( 0.4)   2159.4 ( 0.2)   428 MiB/s ( 0.2)      10
SBX_PL7_InlineNone_HpackVarintDecoder           2405.3 ( 0.6)   2406.8 ( 0.5)   384 MiB/s ( 0.5)      10
*/
// clang-format on

#include <math.h>
#include <stddef.h>

#include <algorithm>
#include <map>

#include "base/callback.h"
#include "base/commandlineflags.h"
#include "base/init_google.h"
#include "base/logging.h"
#include "third_party/http2/decoder/decode_buffer.h"
#include "third_party/http2/decoder/decode_status.h"
#include "third_party/http2/platform/api/http2_string_utils.h"
#include "third_party/http2/hpack/tools/base_hpack_benchmark.h"
#include "third_party/http2/hpack/tools/hpack_block_builder.h"
#include "testing/base/public/benchmark.h"
#include "util/functional/to_callback.h"
#include "util/regexp/re2/re2.h"
#include "third_party/absl/strings/util.h"

DEFINE_bool(output_value_histogram,
            false,
            "Output the number of values generated of each size.");

DECLARE_string(benchmarks);

namespace http2 {
namespace test {
namespace {

template <class DECODER>
class VarintDecoderBenchmark : public BaseGenericDecoderBenchmark<DECODER> {
 public:
  typedef BaseGenericDecoderBenchmark<DECODER> Base;
  using Base::Random;

  explicit VarintDecoderBenchmark(uint32_t serialized_bytes)
      : serialized_bytes_(serialized_bytes),
        prefix_length_(DECODER::PrefixLength()) {
    VLOG(1) << "VarintDecoderBenchmark(" << serialized_bytes_ << ", "
            << prefix_length_ << ")";
    CHECK_LE(serialized_bytes_, DECODER::MaxExtensionBytes() + 1);
  }

  VarintDecoderBenchmark()
      : serialized_bytes_(0), prefix_length_(DECODER::PrefixLength()) {
    VLOG(1) << "VarintDecoderBenchmark(" << prefix_length_ << ")";
  }

  ~VarintDecoderBenchmark() override {}

 protected:
  // Adds one variable length integer to the HpackBlockBuilder.
  void GenerateItem(HpackBlockBuilder* hbb) override {
    uint32_t value;
    if (serialized_bytes_ > 0) {
      // This benchmark calls for all items to have the same length.
      value = GenerateValueWithExtensionBytes(serialized_bytes_ - 1);
    } else if (prefix_length_ == 7) {
      // A string length or an Indexed Header. The latter is more common.
      if (Base::Random().OneIn(10)) {
        // Generate a string length. Most are values, some are names (i.e. a
        // Indexed Literal has a name index and a literal value).
        if (Base::Random().OneIn(10)) {
          // Names are shorter than values, essentially never very long.
          value = Base::GenerateNameLength();
        } else {
          value = Base::GenerateValueLength();
        }
      } else {
        // Generate an index into the static or dynamic table (1 to N).
        value = Base::GenerateNonZeroIndex();
      }
    } else if (prefix_length_ == 6) {
      // An Indexed Literal Header, with either an index for the name or a zero
      // to indicate that a literal name follows. The former is more common.
      if (Base::Random().OneIn(20)) {
        value = 0;
      } else {
        value = Base::GenerateNonZeroIndex();
      }
    } else if (prefix_length_ == 5) {
      // kDynamicTableSizeUpdate
      value = Base::GenerateDynamicTableSize();
    } else {
      CHECK_EQ(prefix_length_, 4);
      // An Unindexed or Never Indexed Literal Header, with either an index for
      // the name or a zero to indicate that a literal name follows. The former
      // is more common.
      if (Base::Random().OneIn(20)) {
        value = 0;
      } else {
        value = Base::GenerateNonZeroIndex();
      }
    }
    ++(value_histogram_[value]);
    size_t old_size = hbb->size();
    hbb->AppendHighBitsAndVarint(0xff << prefix_length_, prefix_length_, value);
    size_t item_size = hbb->size() - old_size;
    DCHECK_LE(item_size, DECODER::MaxExtensionBytes() + 1)
        << "item_size=" << item_size
        << ", MaxExtensionBytes=" << DECODER::MaxExtensionBytes();
  }

  void OnAllItemsGenerated(size_t num_items, size_t num_bytes) override {
    if (FLAGS_output_value_histogram) {
      LOG(INFO) << "";
      LOG(INFO) << __PRETTY_FUNCTION__;
      LOG(INFO) << "";
      LOG(INFO)
          << "VALUE   COUNT  HISTOGRAM "
          << "------------------------------------------------------------";
      uint32_t highest_value = 0;
      size_t highest_count = 0;
      for (const auto& entry : value_histogram_) {
        highest_value = std::max(highest_value, entry.first);
        highest_count = std::max(highest_count, entry.second);
      }
      double num_dots = 40.0;
      double steps_per_dot = highest_count / num_dots;
      for (const auto& entry : value_histogram_) {
        size_t num_dots =
            static_cast<size_t>(std::round(entry.second / steps_per_dot));
        Http2String dots(num_dots, '*');
        LOG(INFO) << Http2StringPrintf("%5d : %6zu  %s", entry.first,
                                       entry.second, dots.c_str());
      }
    }
  }

 private:
  // Returns the highest value that can be encoded with the specified number
  // of |extension_bytes| and the indirectly specified prefix length.
  static size_t constexpr HiValueOfExtensionBytes(uint32_t extension_bytes) {
    return (1 << DECODER::PrefixLength()) - 2 +
           (extension_bytes == 0 ? 0 : (1LLU << (extension_bytes * 7)));
  }

  // Generate a value that requires |extension_bytes| to encode (i.e. whose
  // length when serialized is |extension_bytes| + 1).
  uint32_t GenerateValueWithExtensionBytes(int extension_bytes) {
    uint32_t lo = 0, hi = HiValueOfExtensionBytes(extension_bytes);
    if (extension_bytes > 0) {
      lo = HiValueOfExtensionBytes(extension_bytes - 1) + 1;
    }
    DVLOG(2) << "GenerateValueWithExtensionBytes(" << extension_bytes
             << "),   PrefixLength=" << DECODER::PrefixLength() + 0;
    uint32_t value = lo + Random().Uniform(hi - lo + 1);
    DVLOG(2) << "\tlo = " << lo << "\thi = " << hi << "\tvalue=" << value;
    return value;
  }

  std::map<uint32_t, size_t> value_histogram_;
  const uint32_t serialized_bytes_;
  const uint32_t prefix_length_;
};

// Call DECODER::Start and DECODER::Resume, defined in the header file, so
// *probably* inlined. Could go so far as to use the Clang attribute 'flatten'
// on InlineBoth::Start and InlineBoth::Resume to direct the compiler to inline
// methods that those methods call.
template <class DECODER, int PREFIX_LENGTH>
class InlineBoth {
 public:
  typedef DECODER Decoder;
  typedef uint64_t ResultType;

  DecodeStatus Start(DecodeBuffer* b) {
    uint8_t byte = b->DecodeUInt8();
    DecodeStatus status = decoder_.Start(byte, PrefixLength(), b);
    DCHECK(status == DecodeStatus::kDecodeDone ||
           (b->Empty() && status == DecodeStatus::kDecodeInProgress))
        << "status=" << status << ", Remaining=" << b->Remaining()
        << ", PREFIX_LENGTH=" << PREFIX_LENGTH << ", PrefixMask=" << std::hex
        << (PrefixMask() + 0) << "\ndecoder_: " << decoder_.DebugString()
        << "\n"
        << __PRETTY_FUNCTION__;
    MaybeCollectValue(status);
    return status;
  }

  DecodeStatus Resume(DecodeBuffer* b) {
    DecodeStatus status = decoder_.Resume(b);
    DCHECK(status == DecodeStatus::kDecodeDone ||
           (b->Empty() && status == DecodeStatus::kDecodeInProgress))
        << "status=" << status << ", Remaining=" << b->Remaining()
        << ", PREFIX_LENGTH=" << PREFIX_LENGTH << ", PrefixMask=" << std::hex
        << (PrefixMask() + 0) << "\ndecoder_: " << decoder_.DebugString()
        << "\n"
        << __PRETTY_FUNCTION__;
    MaybeCollectValue(status);
    return status;
  }

  ResultType ExtractResult() {
    DCHECK(have_value_);
    sum_ += value_;
    return sum_;
  }

  Http2String DebugString() { return decoder_.DebugString(); }

  static constexpr uint32_t MaxExtensionBytes() {
    return Decoder::MaxExtensionBytes();
  }
  static constexpr uint8_t PrefixLength() { return PREFIX_LENGTH; }
  static constexpr uint8_t PrefixMask() { return (1 << PREFIX_LENGTH) - 1; }
  static_assert(0 == (PrefixMask() & (PrefixMask() + 1)), "Bad Mask");

 private:
  void MaybeCollectValue(DecodeStatus status) {
    if (status == DecodeStatus::kDecodeDone) {
      value_ = decoder_.value();
      have_value_ = true;
    } else {
      have_value_ = false;
    }
  }

  Decoder decoder_;
  ResultType sum_ = 0;
  uint32_t value_;
  bool have_value_;
};

template <class DECODER, int PREFIX_LENGTH>
class InlineNone {
 public:
  typedef DECODER Decoder;
  typedef uint64_t ResultType;

  DecodeStatus Start(DecodeBuffer* b) {
    uint8_t byte = b->DecodeUInt8();
    DecodeStatus status = decoder_.StartForTest(byte, PrefixLength(), b);
    DCHECK(status == DecodeStatus::kDecodeDone ||
           (b->Empty() && status == DecodeStatus::kDecodeInProgress))
        << "status=" << status << ", Remaining=" << b->Remaining()
        << ", PREFIX_LENGTH=" << PREFIX_LENGTH << ", PrefixMask=" << std::hex
        << (PrefixMask() + 0) << "\ndecoder_: " << decoder_.DebugString()
        << "\n"
        << __PRETTY_FUNCTION__;
    MaybeCollectValue(status);
    return status;
  }

  DecodeStatus Resume(DecodeBuffer* b) {
    DecodeStatus status = decoder_.ResumeForTest(b);
    DCHECK(status == DecodeStatus::kDecodeDone ||
           (b->Empty() && status == DecodeStatus::kDecodeInProgress))
        << "status=" << status << ", Remaining=" << b->Remaining()
        << ", PREFIX_LENGTH=" << PREFIX_LENGTH << ", PrefixMask=" << std::hex
        << (PrefixMask() + 0) << "\ndecoder_: " << decoder_.DebugString()
        << "\n"
        << __PRETTY_FUNCTION__;
    MaybeCollectValue(status);
    return status;
  }

  ResultType ExtractResult() {
    DCHECK(have_value_);
    sum_ += value_;
    return sum_;
  }

  Http2String DebugString() { return decoder_.DebugString(); }

  static constexpr uint32_t MaxExtensionBytes() {
    return Decoder::MaxExtensionBytes();
  }
  static constexpr uint8_t PrefixLength() { return PREFIX_LENGTH; }
  static constexpr uint8_t PrefixMask() { return (1 << PREFIX_LENGTH) - 1; }
  static_assert(0 == (PrefixMask() & (PrefixMask() + 1)), "Bad Mask");

 private:
  void MaybeCollectValue(DecodeStatus status) {
    if (status == DecodeStatus::kDecodeDone) {
      value_ = decoder_.value();
      have_value_ = true;
    } else {
      have_value_ = false;
    }
  }

  Decoder decoder_;
  ResultType sum_ = 0;
  uint32_t value_;
  bool have_value_;
};

template <class DECODER, int PREFIX_LENGTH>
class InlineBothExtended {
 public:
  typedef DECODER Decoder;
  typedef uint64_t ResultType;
  InlineBothExtended() {
    static_assert(0 == (PrefixMask() & (PrefixMask() + 1)), "Bad Mask");
  }

  DecodeStatus Start(DecodeBuffer* b) {
    uint8_t byte = b->DecodeUInt8();
    DVLOG(1) << Http2StrCat("byte=", byte, " (0x", absl::Hex(byte), ")");
    byte &= PrefixMask();
    if (byte < PrefixMask()) {
      DVLOG(1) << Http2StrCat("single byte encoding ", byte);
      value_ = byte;
      have_value_ = true;
      return DecodeStatus::kDecodeDone;
    }
    DCHECK_EQ(byte, PrefixMask());
    have_value_ = false;
    DecodeStatus status = decoder_.StartExtended(PrefixLength(), b);
    DCHECK(status == DecodeStatus::kDecodeDone ||
           (b->Empty() && status == DecodeStatus::kDecodeInProgress))
        << "status=" << status << ", Remaining=" << b->Remaining()
        << ", PREFIX_LENGTH=" << PREFIX_LENGTH << ", PrefixMask=" << std::hex
        << (PrefixMask() + 0) << "\ndecoder_: " << decoder_.DebugString()
        << "\n"
        << __PRETTY_FUNCTION__;
    MaybeCollectValue(status);
    return status;
  }

  DecodeStatus Resume(DecodeBuffer* b) {
    DecodeStatus status = decoder_.Resume(b);
    DCHECK(status == DecodeStatus::kDecodeDone ||
           (b->Empty() && status == DecodeStatus::kDecodeInProgress))
        << "status=" << status << ", Remaining=" << b->Remaining()
        << ", PREFIX_LENGTH=" << PREFIX_LENGTH << ", PrefixMask=" << std::hex
        << (PrefixMask() + 0) << "\ndecoder_: " << decoder_.DebugString()
        << "\n"
        << __PRETTY_FUNCTION__;
    MaybeCollectValue(status);
    return status;
  }

  ResultType ExtractResult() {
    DCHECK(have_value_);
    sum_ += value_;
    return sum_;
  }

  Http2String DebugString() { return decoder_.DebugString(); }

  static constexpr uint32_t MaxExtensionBytes() {
    return Decoder::MaxExtensionBytes();
  }
  static constexpr uint8_t PrefixLength() { return PREFIX_LENGTH; }
  static constexpr uint8_t PrefixMask() { return (1 << PREFIX_LENGTH) - 1; }

 private:
  void MaybeCollectValue(DecodeStatus status) {
    if (status == DecodeStatus::kDecodeDone) {
      value_ = decoder_.value();
      have_value_ = true;
    } else {
      have_value_ = false;
    }
  }

  Decoder decoder_;
  ResultType sum_ = 0;
  uint32_t value_;
  bool have_value_;
};

template <class DECODER, int PREFIX_LENGTH>
class InlineNoneExtended {
 public:
  typedef DECODER Decoder;
  typedef uint64_t ResultType;

  DecodeStatus Start(DecodeBuffer* b) {
    uint8_t byte = b->DecodeUInt8();
    DVLOG(1) << Http2StrCat("byte=", byte, " (0x", absl::Hex(byte), ")");
    byte &= PrefixMask();
    if (byte < PrefixMask()) {
      DVLOG(1) << Http2StrCat("single byte encoding ", byte);
      value_ = byte;
      have_value_ = true;
      return DecodeStatus::kDecodeDone;
    }
    DCHECK_EQ(byte, PrefixMask());
    have_value_ = false;
    DecodeStatus status = decoder_.StartExtendedForTest(PrefixLength(), b);
    DCHECK(status == DecodeStatus::kDecodeDone ||
           (b->Empty() && status == DecodeStatus::kDecodeInProgress))
        << "status=" << status << ", Remaining=" << b->Remaining()
        << ", PREFIX_LENGTH=" << PREFIX_LENGTH << ", PrefixMask=" << std::hex
        << (PrefixMask() + 0) << "\ndecoder_: " << decoder_.DebugString()
        << "\n"
        << __PRETTY_FUNCTION__;
    MaybeCollectValue(status);
    return status;
  }

  DecodeStatus Resume(DecodeBuffer* b) {
    DecodeStatus status = decoder_.ResumeForTest(b);
    DCHECK(status == DecodeStatus::kDecodeDone ||
           (b->Empty() && status == DecodeStatus::kDecodeInProgress))
        << "status=" << status << ", Remaining=" << b->Remaining()
        << ", PREFIX_LENGTH=" << PREFIX_LENGTH << ", PrefixMask=" << std::hex
        << (PrefixMask() + 0) << "\ndecoder_: " << decoder_.DebugString()
        << "\n"
        << __PRETTY_FUNCTION__;
    MaybeCollectValue(status);
    return status;
  }

  ResultType ExtractResult() {
    DCHECK(have_value_);
    sum_ += value_;
    return sum_;
  }

  Http2String DebugString() { return decoder_.DebugString(); }

  static constexpr uint32_t MaxExtensionBytes() {
    return Decoder::MaxExtensionBytes();
  }
  static constexpr uint8_t PrefixLength() { return PREFIX_LENGTH; }
  static constexpr uint8_t PrefixMask() { return (1 << PREFIX_LENGTH) - 1; }
  static_assert(0 == (PrefixMask() & (PrefixMask() + 1)), "Bad Mask");

 private:
  void MaybeCollectValue(DecodeStatus status) {
    if (status == DecodeStatus::kDecodeDone) {
      value_ = decoder_.value();
      have_value_ = true;
    } else {
      have_value_ = false;
    }
  }

  Decoder decoder_;
  ResultType sum_ = 0;
  uint32_t value_;
  bool have_value_;
};

}  // namespace

// Registers one benchmark of DECODER, where the template class DECODE_METHOD
// controls which methods are called (e.g. inlined or not). Each generated item
// has the same PREFIX_LENGTH (in bits), and if SERIALIZED_BYTES is not zero,
// then each generated item has the same length (in bytes), else a
// population of vaguely realistic sized items is decoded.
template <int SERIALIZED_BYTES,
          int PREFIX_LENGTH,
          template <typename D, int PL> class DECODE_METHOD,
          class DECODER>
void RegisterBenchmarkOfSBPLDMAndDecoder() {
  static_assert(4 <= PREFIX_LENGTH, "PREFIX_LENGTH is too low.");
  static_assert(PREFIX_LENGTH <= 7, "PREFIX_LENGTH is too high.");

  if (SERIALIZED_BYTES > 0 &&
      SERIALIZED_BYTES > DECODER::MaxExtensionBytes() + 1) {
    DLOG(INFO) << "Skipping encoding that is too long to decode.";
    return;
  }

  typedef DECODE_METHOD<DECODER, PREFIX_LENGTH> DecoderWrapper;
  typedef VarintDecoderBenchmark<DecoderWrapper> DecoderBenchmark;

  // "Parse" __PRETTY_FUNCTION__ to determine what types are being used.
  // __PRETTY_FUNCTION__ is approximately (in a debug build):
  //
  //      void http2::test::RegisterBenchmarkOfSBPLDMAndDecoder()
  //     [SERIALIZED_BYTES = 0, PREFIX_LENGTH = 4,
  //      DECODE_METHOD = InlineBoth,
  //      DECODER = http2::HpackVarintDecoder]
  //
  // or in an optimized build:
  //
  //      void http2::test::RegisterBenchmarkOfSBPLDMAndDecoder()
  //      [with int SERIALIZED_BYTES = 0;
  //            int PREFIX_LENGTH = 4;
  //            DECODE_METHOD = http2::test::{anonymous}::InlineBoth;
  //            DECODER = http2::HpackVarintDecoder]

  VLOG(1) << __PRETTY_FUNCTION__;

  Http2String ns_pat = "(?:\\{anonymous\\}|\\w+)::";
  Http2String nss_pat = "(?:" + ns_pat + ")*";
  Http2String sym_pat = nss_pat + "(\\w+)";

  RE2 re("\\bDECODE_METHOD = " + sym_pat + "[;,]\\s+DECODER = " + sym_pat);
  CHECK_EQ(2, re.NumberOfCapturingGroups());

  Http2String decode_method, decoder;

  CHECK(RE2::PartialMatch(__PRETTY_FUNCTION__, re, &decode_method, &decoder))
      << __PRETTY_FUNCTION__;

  Http2String name;
  if (SERIALIZED_BYTES == 0) {
    name =
        Http2StrCat("SBX/PL", PREFIX_LENGTH, "/", decode_method, "/", decoder);
  } else {
    name = Http2StrCat("SB", SERIALIZED_BYTES, "/PL", PREFIX_LENGTH, "/",
                       decode_method, "/", decoder);
  }

  // TODO(jamessynge): Add a "validation" phase here, where the benchmarking
  // code is checked to make sure that the decoding is actually correct in this
  // context. I.e., create an instance of DecoderBenchmark, have it generate
  // a few items, decode those, checking that each matches the expected value.

  auto run_benchmark = [name](int iters) {
    StopBenchmarkTiming();
    VLOG(1) << "Running " << iters << " for benchmark " << name;
    DecoderBenchmark bm(SERIALIZED_BYTES);
    bm.Benchmark(iters);
  };
  Callback1<int>* benchmark_runner =
      util::functional::ToPermanentCallback(run_benchmark);

  LOG(INFO) << "Registering benchmark \"" << name << "\"";
  new ::testing::Benchmark(name, benchmark_runner);
}

template <int SERIALIZED_BYTES,
          int PREFIX_LENGTH,
          template <typename D, int PL> class DECODE_METHOD>
void RegisterBenchmarksOfSBPLAndDecodeMethod() {
  // Register benchmarks of the specified decoder class (currently only
  // HpackVarintDecoder).

  RegisterBenchmarkOfSBPLDMAndDecoder<SERIALIZED_BYTES, PREFIX_LENGTH,
                                      DECODE_METHOD, HpackVarintDecoder>();
  // If there are other HPACK varint decoder classes with the same API to be
  // compared with HpackVarintDecoder, register benchmarks for them here;
  // for example:
  //   RegisterBenchmarkOfSBPLDMAndDecoder<
  //       SERIALIZED_BYTES, PREFIX_LENGTH,
  //       DECODE_METHOD, HpackPeekAheadVarintDecoder>();
}

template <int SERIALIZED_BYTES, int PREFIX_LENGTH>
void RegisterBenchmarksOfSBAndPrefixLength() {
  // Register benchmarks with the specified decoding method (i.e. which methods
  // of the decoder class are called, whether inlining is occurring, etc.).

  RegisterBenchmarksOfSBPLAndDecodeMethod<SERIALIZED_BYTES, PREFIX_LENGTH,
                                          InlineBoth>();
  RegisterBenchmarksOfSBPLAndDecodeMethod<SERIALIZED_BYTES, PREFIX_LENGTH,
                                          InlineBothExtended>();
  RegisterBenchmarksOfSBPLAndDecodeMethod<SERIALIZED_BYTES, PREFIX_LENGTH,
                                          InlineNone>();
  RegisterBenchmarksOfSBPLAndDecodeMethod<SERIALIZED_BYTES, PREFIX_LENGTH,
                                          InlineNoneExtended>();
}

// TODO(jamessynge): Move SERIALIZED_BYTES to be a regular (i.e. runtime)
// parameter rather than a template parameter. Unlike PREFIX_LENGTH, it doesn't
// provide extra info that we'd like the compiler to use.
template <int SERIALIZED_BYTES>
void RegisterBenchmarksOfSerializedBytes() {
  // Register benchmarks of encodings with the specified prefix length (bits)...

  RegisterBenchmarksOfSBAndPrefixLength<SERIALIZED_BYTES, 4>();
  RegisterBenchmarksOfSBAndPrefixLength<SERIALIZED_BYTES, 5>();
  RegisterBenchmarksOfSBAndPrefixLength<SERIALIZED_BYTES, 6>();
  RegisterBenchmarksOfSBAndPrefixLength<SERIALIZED_BYTES, 7>();
}

void RegisterAllBenchmarks() {
  // Register benchmarks of encodings of the specified number of bytes...

  RegisterBenchmarksOfSerializedBytes<1>();
  RegisterBenchmarksOfSerializedBytes<2>();
  RegisterBenchmarksOfSerializedBytes<3>();

  // Skipping 4 and 5 because they're not important in practice.
  //   RegisterBenchmarksOfSerializedBytes<4>();
  //   RegisterBenchmarksOfSerializedBytes<5>();

  // Register benchmarks of various lengths, determined by a population model
  // (not yet an accurate model of production, just a guess).
  RegisterBenchmarksOfSerializedBytes<0>();
}

}  // namespace test
}  // namespace http2

int main(int argc, char** argv) {
  InitGoogle(argv[0], &argc, &argv, true);
  if (FLAGS_benchmarks.empty()) {
    FLAGS_benchmarks = "all";
  }
  http2::test::RegisterAllBenchmarks();
  RunSpecifiedBenchmarks();
  return 0;
}
