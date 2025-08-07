/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/trace_processor/containers/bit_vector.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <utility>
#include <vector>

#include "perfetto/base/build_config.h"
#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "perfetto/public/compiler.h"

#include "protos/perfetto/trace_processor/serialization.pbzero.h"

#if PERFETTO_BUILDFLAG(PERFETTO_X64_CPU_OPT)
#include <immintrin.h>
#endif

namespace perfetto::trace_processor {
namespace {

// This function implements the PDEP instruction in x64 as a loop.
// See https://www.felixcloutier.com/x86/pdep for details on what PDEP does.
//
// Unfortunately, as we're emulating this in software, it scales with the number
// of set bits in |mask| rather than being a constant time instruction:
// therefore, this should be avoided where real instructions are available.
PERFETTO_ALWAYS_INLINE uint64_t PdepSlow(uint64_t word, uint64_t mask) {
  if (word == 0 || mask == std::numeric_limits<uint64_t>::max())
    return word;

  // This algorithm is for calculating PDEP was found to be the fastest "simple"
  // one among those tested when writing this function.
  uint64_t result = 0;
  for (uint64_t bb = 1; mask; bb += bb) {
    if (word & bb) {
      // MSVC doesn't like -mask so work around this by doing 0 - mask.
      result |= mask & (0ull - mask);
    }
    mask &= mask - 1;
  }
  return result;
}

// See |PdepSlow| for information on PDEP.
PERFETTO_ALWAYS_INLINE uint64_t Pdep(uint64_t word, uint64_t mask) {
#if PERFETTO_BUILDFLAG(PERFETTO_X64_CPU_OPT)
  base::ignore_result(PdepSlow);
  return _pdep_u64(word, mask);
#else
  return PdepSlow(word, mask);
#endif
}

// This function implements the PEXT instruction in x64 as a loop.
// See https://www.felixcloutier.com/x86/pext for details on what PEXT does.
//
// Unfortunately, as we're emulating this in software, it scales with the number
// of set bits in |mask| rather than being a constant time instruction:
// therefore, this should be avoided where real instructions are available.
PERFETTO_ALWAYS_INLINE uint64_t PextSlow(uint64_t word, uint64_t mask) {
  if (word == 0 || mask == std::numeric_limits<uint64_t>::max())
    return word;

  // This algorithm is for calculating PEXT was found to be the fastest "simple"
  // one among those tested when writing this function.
  uint64_t result = 0;
  for (uint64_t bb = 1; mask; bb += bb) {
    // MSVC doesn't like -mask so work around this by doing 0 - mask.
    if (word & mask & (0ull - mask)) {
      result |= bb;
    }
    mask &= mask - 1;
  }
  return result;
}

// See |PextSlow| for information on PEXT.
PERFETTO_ALWAYS_INLINE uint64_t Pext(uint64_t word, uint64_t mask) {
#if PERFETTO_BUILDFLAG(PERFETTO_X64_CPU_OPT)
  base::ignore_result(PextSlow);
  return _pext_u64(word, mask);
#else
  return PextSlow(word, mask);
#endif
}

// This function implements the tzcnt instruction.
// See https://www.felixcloutier.com/x86/tzcnt for details on what tzcnt does.
PERFETTO_ALWAYS_INLINE uint32_t Tzcnt(uint64_t value) {
#if PERFETTO_BUILDFLAG(PERFETTO_X64_CPU_OPT)
  return static_cast<uint32_t>(_tzcnt_u64(value));
#elif defined(__GNUC__) || defined(__clang__)
  return value ? static_cast<uint32_t>(__builtin_ctzll(value)) : 64u;
#else
  unsigned long out;
  return _BitScanForward64(&out, value) ? static_cast<uint32_t>(out) : 64u;
#endif
}

}  // namespace

BitVector::BitVector() = default;

BitVector::BitVector(std::initializer_list<bool> init) {
  for (bool x : init) {
    if (x) {
      AppendTrue();
    } else {
      AppendFalse();
    }
  }
}

BitVector::BitVector(uint32_t count, bool value) {
  Resize(count, value);
}

BitVector::BitVector(std::vector<uint64_t> words,
                     std::vector<uint32_t> counts,
                     uint32_t size)
    : size_(size), counts_(std::move(counts)), words_(std::move(words)) {
  PERFETTO_CHECK(words_.size() % Block::kWords == 0);
}

void BitVector::Resize(uint32_t new_size, bool filler) {
  uint32_t old_size = size_;
  if (new_size == old_size)
    return;

  // Empty bitvectors should be memory efficient so we don't keep any data
  // around in the bitvector.
  if (new_size == 0) {
    words_.clear();
    counts_.clear();
    size_ = 0;
    return;
  }

  // Compute the address of the new last bit in the bitvector.
  Address last_addr = IndexToAddress(new_size - 1);
  auto old_blocks_size = static_cast<uint32_t>(counts_.size());
  uint32_t new_blocks_size = last_addr.block_idx + 1;

  // Resize the block and count vectors to have the correct number of entries.
  words_.resize(Block::kWords * new_blocks_size);
  counts_.resize(new_blocks_size);

  if (new_size > old_size) {
    if (filler) {
      // If the new space should be filled with ones, then set all the bits
      // between the address of the old size and the new last address.
      const Address& start_filler = IndexToAddress(old_size);
      Set(start_filler, last_addr);

      // If `start_filler` is at the start of a block, we need to update the
      // last old block `counts_` manually.
      if (old_size % kBitsInBlock == 0) {
        counts_[start_filler.block_idx] = CountSetBits();
      }

      // We then need to update the counts vector to match the changes we
      // made to the blocks.

      // We start by adding the bits we set in the first block to the
      // cumulative count before the range we changed.
      Address end_of_block = {start_filler.block_idx,
                              {Block::kWords - 1, BitWord::kBits - 1}};
      uint32_t count_in_block_after_end =
          AddressToIndex(end_of_block) - AddressToIndex(start_filler) + 1;
      uint32_t set_count = CountSetBits() + count_in_block_after_end;

      for (uint32_t i = start_filler.block_idx + 1; i <= last_addr.block_idx;
           ++i) {
        // Set the count to the cumulative count so far.
        counts_[i] = set_count;

        // Add a full block of set bits to the count.
        set_count += Block::kBits;
      }
    } else {
      // If the newly added bits are false, we just need to update the
      // counts vector with the current size of the bitvector for all
      // the newly added blocks.
      if (new_blocks_size > old_blocks_size) {
        uint32_t count = CountSetBits();
        for (uint32_t i = old_blocks_size; i < new_blocks_size; ++i) {
          counts_[i] = count;
        }
      }
    }
  } else {
    // Throw away all the bits after the new last bit. We do this to make
    // future lookup, append and resize operations not have to worrying about
    // trailing garbage bits in the last block.
    BlockFromIndex(last_addr.block_idx).ClearAfter(last_addr.block_offset);
  }

  // Actually update the size.
  size_ = new_size;
}

BitVector BitVector::Copy() const {
  return {words_, counts_, size_};
}

void BitVector::Not() {
  if (size_ == 0) {
    return;
  }

  for (uint64_t& word : words_) {
    BitWord(&word).Not();
  }

  // Make sure to reset the last block's trailing bits to zero to preserve the
  // invariant of BitVector.
  Address last_addr = IndexToAddress(size_ - 1);
  BlockFromIndex(last_addr.block_idx).ClearAfter(last_addr.block_offset);

  for (uint32_t i = 1; i < counts_.size(); ++i) {
    counts_[i] = kBitsInBlock * i - counts_[i];
  }
}

void BitVector::Or(const BitVector& sec) {
  PERFETTO_CHECK(size_ == sec.size());
  for (uint32_t i = 0; i < words_.size(); ++i) {
    BitWord(&words_[i]).Or(sec.words_[i]);
  }
  UpdateCounts(words_, counts_);
}

void BitVector::And(const BitVector& sec) {
  Resize(std::min(size_, sec.size_));
  for (uint32_t i = 0; i < words_.size(); ++i) {
    BitWord(&words_[i]).And(sec.words_[i]);
  }
  UpdateCounts(words_, counts_);
}

void BitVector::UpdateSetBits(const BitVector& update) {
  if (update.CountSetBits() == 0 || CountSetBits() == 0) {
    *this = BitVector();
    return;
  }
  PERFETTO_DCHECK(update.size() <= CountSetBits());

  // Get the start and end ptrs for the current bitvector.
  // Safe because of the static_assert above.
  uint64_t* ptr = words_.data();
  const uint64_t* ptr_end = ptr + WordCount(size());

  // Get the start and end ptrs for the update bitvector.
  // Safe because of the static_assert above.
  const uint64_t* update_ptr = update.words_.data();
  const uint64_t* update_ptr_end = update_ptr + WordCount(update.size());

  // |update_unused_bits| contains |unused_bits_count| bits at the bottom
  // which indicates how the next |unused_bits_count| set bits in |this|
  // should be changed. This is necessary because word boundaries in |this| will
  // almost always *not* match the word boundaries in |update|.
  uint64_t update_unused_bits = 0;
  uint8_t unused_bits_count = 0;

  // The basic premise of this loop is, for each word in |this| we find
  // enough bits from |update| to cover every set bit in the word. We then use
  // the PDEP x64 instruction (or equivalent instructions/software emulation) to
  // update the word and store it back in |this|.
  for (; ptr != ptr_end; ++ptr) {
    uint64_t current = *ptr;

    // If the current value is all zeros, there's nothing to update.
    if (PERFETTO_UNLIKELY(current == 0))
      continue;

    auto popcount = static_cast<uint8_t>(PERFETTO_POPCOUNT(current));
    PERFETTO_DCHECK(popcount >= 1);

    // Check if we have enough unused bits from the previous iteration - if so,
    // we don't need to read anything from |update|.
    uint64_t update_for_current = update_unused_bits;
    if (unused_bits_count >= popcount) {
      // We have enough bits so just do the accounting to not reuse these bits
      // for the future.
      unused_bits_count -= popcount;
      update_unused_bits = popcount == 64 ? 0 : update_unused_bits >> popcount;
    } else {
      // We don't have enough bits so we need to read the next word of bits from
      // |current|.
      uint64_t next_update = update_ptr == update_ptr_end ? 0 : *update_ptr++;

      // Bitwise or |64 - unused_bits_count| bits from the bottom of
      // |next_update| to the top of |update_for_current|. Only |popcount| bits
      // will actually be used by PDEP but masking off the unused bits takes
      // *more* instructions than not doing anything.
      update_for_current |= next_update << unused_bits_count;

      // PDEP will use |popcount| bits from update: this means it will use
      // |unused_bits_count| from |update_for_current| and |popcount -
      // unused_bits_count| from |next_update|
      uint8_t used_next_bits = popcount - unused_bits_count;

      // Shift off any bits which will be used by current and store the
      // remainder for use in the next iteration.
      update_unused_bits =
          used_next_bits == 64 ? 0 : next_update >> used_next_bits;
      unused_bits_count = 64 - used_next_bits;
    }

    // We should never end up with more than 64 bits available.
    PERFETTO_CHECK(unused_bits_count <= 64);

    // PDEP precisely captures the notion of "updating set bits" for a single
    // word.
    *ptr = Pdep(update_for_current, current);
  }

  // We shouldn't have any non-zero unused bits and we should have consumed the
  // whole |update| bitvector. Note that we cannot really say anything about
  // |unused_bits_count| because it's possible for the above algorithm to use
  // some bits which are "past the end" of |update|; as long as these bits are
  // zero, it meets the pre-condition of this function.
  PERFETTO_DCHECK(update_unused_bits == 0);
  PERFETTO_DCHECK(update_ptr == update_ptr_end);

  UpdateCounts(words_, counts_);

  // After the loop, we should have precisely the same number of bits
  // set as |update|.
  PERFETTO_DCHECK(update.CountSetBits() == CountSetBits());
}

void BitVector::SelectBits(const BitVector& mask_bv) {
  // Verify the precondition on the function: the algorithm relies on this
  // being the case.
  PERFETTO_DCHECK(size() <= mask_bv.size());

  // Get the set bits in the mask up to the end of |this|: this will precisely
  // equal the number of bits in |this| at the end of this function.
  uint32_t set_bits_in_mask = mask_bv.CountSetBits(size());

  const uint64_t* cur_word = words_.data();
  const uint64_t* end_word = words_.data() + WordCount(size());
  const uint64_t* cur_mask = mask_bv.words_.data();

  // Used to track the number of bits already set (i.e. by a previous loop
  // iteration) in |out_word|.
  uint32_t out_word_bits = 0;
  uint64_t* out_word = words_.data();
  for (; cur_word != end_word; ++cur_word, ++cur_mask) {
    // Loop invariant: we should always have out_word and out_word_bits set
    // such that there is room for at least one more bit.
    PERFETTO_DCHECK(out_word_bits < 64);

    // The crux of this function: efficient parallel extract all bits in |this|
    // which correspond to set bit positions in |this|.
    uint64_t ext = Pext(*cur_word, *cur_mask);

    // If there are no bits in |out_word| from a previous iteration, set it to
    // |ext|. Otherwise, concat the newly added bits to the top of the existing
    // bits.
    *out_word = out_word_bits == 0 ? ext : *out_word | (ext << out_word_bits);

    // Update the number of bits used in |out_word| by adding the number of set
    // bit positions in |mask|.
    auto popcount = static_cast<uint32_t>(PERFETTO_POPCOUNT(*cur_mask));
    out_word_bits += popcount;

    // The below is a branch-free way to increment |out_word| pointer when we've
    // packed 64 bits into it.
    bool spillover = out_word_bits > 64;
    out_word += out_word_bits >= 64;
    out_word_bits %= 64;

    // If there was any "spillover" bits (i.e. bits which did not fit in the
    // previous word), add them into the new out_word. Important: we *must* not
    // change out_word if there was no spillover as |out_word| could be pointing
    // to |data + 1| which needs to be preserved for the next loop iteration.
    if (spillover) {
      *out_word = ext >> (popcount - out_word_bits);
    }
  }

  // Loop post-condition: we must have written as many words as is required
  // to store |set_bits_in_mask|.
  PERFETTO_DCHECK(static_cast<uint32_t>(out_word - words_.data()) <=
                  WordCount(set_bits_in_mask));

  // Resize the BitVector to equal to the number of elements in the  mask we
  // calculated at the start of the loop.
  Resize(set_bits_in_mask);

  // Fix up the counts to match the new values. The Resize above should ensure
  // that a) the counts vector is correctly sized, b) the bits after
  // |set_bits_in_mask| are cleared (allowing this count algorithm to be
  // accurate).
  UpdateCounts(words_, counts_);
}

BitVector BitVector::FromSortedIndexVector(
    const std::vector<int64_t>& indices) {
  // The rest of the algorithm depends on |indices| being non empty.
  if (indices.empty()) {
    return {};
  }

  // We are creating the smallest BitVector that can have all of the values
  // from |indices| set. As we assume that |indices| is sorted, the size would
  // be the last element + 1 and the last bit of the final BitVector will be
  // set.
  auto size = static_cast<uint32_t>(indices.back() + 1);

  uint32_t block_count = BlockCount(size);
  std::vector<uint64_t> words(block_count * Block::kWords);
  for (const int64_t i : indices) {
    auto word_idx = static_cast<uint32_t>(i / kBitsInWord);
    auto in_word_idx = static_cast<uint32_t>(i % kBitsInWord);
    BitVector::BitWord(&words[word_idx]).Set(in_word_idx);
  }

  std::vector<uint32_t> counts(block_count);
  UpdateCounts(words, counts);
  return {words, counts, size};
}

BitVector BitVector::FromUnsortedIndexVector(
    const std::vector<uint32_t>& indices) {
  // The rest of the algorithm depends on |indices| being non empty.
  if (indices.empty()) {
    return {};
  }

  std::vector<uint64_t> words;
  uint32_t max_idx = 0;
  for (const uint32_t i : indices) {
    auto word_idx = static_cast<uint32_t>(i / kBitsInWord);
    max_idx = std::max(max_idx, i);
    if (word_idx >= words.size()) {
      words.resize(word_idx + 1);
    }
    auto in_word_idx = static_cast<uint32_t>(i % kBitsInWord);
    BitVector::BitWord(&words[word_idx]).Set(in_word_idx);
  }

  auto block_count = BlockCount(max_idx + 1);
  words.resize(block_count * Block::kWords);
  std::vector<uint32_t> counts(block_count);
  UpdateCounts(words, counts);
  return {words, counts, max_idx + 1};
}

BitVector BitVector::IntersectRange(uint32_t range_start,
                                    uint32_t range_end) const {
  // We should skip all bits until the index of first set bit bigger than
  // |range_start|.
  uint32_t end_idx = std::min(range_end, size());

  if (range_start >= end_idx)
    return {};

  Builder builder(end_idx, range_start);
  uint32_t front_bits = builder.BitsUntilWordBoundaryOrFull();
  uint32_t cur_index = range_start;
  for (uint32_t i = 0; i < front_bits; ++i, ++cur_index) {
    builder.Append(IsSet(cur_index));
  }

  PERFETTO_DCHECK(cur_index == end_idx || cur_index % BitWord::kBits == 0);
  uint32_t cur_words = cur_index / BitWord::kBits;
  uint32_t full_words = builder.BitsInCompleteWordsUntilFull() / BitWord::kBits;
  uint32_t total_full_words = cur_words + full_words;
  for (; cur_words < total_full_words; ++cur_words) {
    builder.AppendWord(words_[cur_words]);
  }

  uint32_t last_bits = builder.BitsUntilFull();
  cur_index += full_words * BitWord::kBits;
  for (uint32_t i = 0; i < last_bits; ++i, ++cur_index) {
    builder.Append(IsSet(cur_index));
  }

  return std::move(builder).Build();
}

std::vector<uint32_t> BitVector::GetSetBitIndices() const {
  uint32_t set_bits = CountSetBits();
  if (set_bits == 0) {
    return {};
  }
  std::vector<uint32_t> res(set_bits);

  // After measuring we discovered that not doing `push_back` creates a tangible
  // performance improvement due to compiler unrolling the inner loop.
  uint32_t res_idx = 0;
  for (uint32_t i = 0; i < size_; i += BitWord::kBits) {
    for (uint64_t word = words_[i / BitWord::kBits]; word; word &= word - 1) {
      res[res_idx++] = i + Tzcnt(word);
    }
  }
  return res;
}

void BitVector::Serialize(
    protos::pbzero::SerializedColumn::BitVector* msg) const {
  msg->set_size(size_);
  if (!counts_.empty()) {
    msg->set_counts(reinterpret_cast<const uint8_t*>(counts_.data()),
                    sizeof(uint32_t) * counts_.size());
  }
  if (!words_.empty()) {
    msg->set_words(reinterpret_cast<const uint8_t*>(words_.data()),
                   sizeof(uint64_t) * words_.size());
  }
}

// Deserialize BitVector from proto.
void BitVector::Deserialize(
    const protos::pbzero::SerializedColumn::BitVector::Decoder& bv_msg) {
  size_ = bv_msg.size();
  if (bv_msg.has_counts()) {
    counts_.resize(
        static_cast<size_t>(bv_msg.counts().size / sizeof(uint32_t)));
    memcpy(counts_.data(), bv_msg.counts().data, bv_msg.counts().size);
  } else {
    counts_.clear();
  }

  if (bv_msg.has_words()) {
    words_.resize(static_cast<size_t>(bv_msg.words().size / sizeof(uint64_t)));
    memcpy(words_.data(), bv_msg.words().data, bv_msg.words().size);
  } else {
    words_.clear();
  }
}

}  // namespace perfetto::trace_processor
