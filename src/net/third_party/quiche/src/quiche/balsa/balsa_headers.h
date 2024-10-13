// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A lightweight implementation for storing HTTP headers.

#ifndef QUICHE_BALSA_BALSA_HEADERS_H_
#define QUICHE_BALSA_BALSA_HEADERS_H_

#include <cstddef>
#include <cstring>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/memory/memory.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "quiche/balsa/balsa_enums.h"
#include "quiche/balsa/header_api.h"
#include "quiche/balsa/http_validation_policy.h"
#include "quiche/balsa/standard_header_map.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_callbacks.h"

namespace gfe2 {
class Http2HeaderValidator;
}  // namespace gfe2

namespace quiche {

namespace test {
class BalsaHeadersTestPeer;
}  // namespace test

// WARNING:
// Note that -no- char* returned by any function in this
// file is null-terminated.

// This class exists to service the specific needs of BalsaHeaders.
//
// Functional goals:
//   1) provide a backing-store for all of the StringPieces that BalsaHeaders
//      returns. Every StringPiece returned from BalsaHeaders should remain
//      valid until the BalsaHeader's object is cleared, or the header-line is
//      erased.
//   2) provide a backing-store for BalsaFrame, which requires contiguous memory
//      for its fast-path parsing functions. Note that the cost of copying is
//      less than the cost of requiring the parser to do slow-path parsing, as
//      it would have to check for bounds every byte, instead of every 16 bytes.
//
// This class is optimized for the case where headers are stored in one of two
// buffers. It doesn't make a lot of effort to densely pack memory-- in fact,
// it -may- be somewhat memory inefficient. This possible inefficiency allows a
// certain simplicity of implementation and speed which makes it worthwhile.
// If, in the future, better memory density is required, it should be possible
// to reuse the abstraction presented by this object to achieve those goals.
//
// In the most common use-case, this memory inefficiency should be relatively
// small.
//
// Alternate implementations of BalsaBuffer may include:
//  - vector of strings, one per header line (similar to HTTPHeaders)
//  - densely packed strings:
//    - keep a sorted array/map of free-space linked lists or numbers.
//      - use the entry that most closely first your needs.
//    - at this point, perhaps just use a vector of strings, and let
//      the allocator do the right thing.
//
class QUICHE_EXPORT BalsaBuffer {
 public:
  static constexpr size_t kDefaultBlocksize = 4096;

  // The BufferBlock is a structure used internally by the
  // BalsaBuffer class to store the base buffer pointers to
  // each block, as well as the important metadata for buffer
  // sizes and bytes free. It *may* be possible to replace this
  // with a vector<char>, but it's unclear whether moving a vector
  // can invalidate pointers into it. LWG issue 2321 proposes to fix this.
  struct QUICHE_EXPORT BufferBlock {
   public:
    std::unique_ptr<char[]> buffer;
    size_t buffer_size = 0;
    size_t bytes_free = 0;

    size_t bytes_used() const { return buffer_size - bytes_free; }
    char* start_of_unused_bytes() const { return buffer.get() + bytes_used(); }

    BufferBlock() {}

    BufferBlock(std::unique_ptr<char[]> buf, size_t size, size_t free)
        : buffer(std::move(buf)), buffer_size(size), bytes_free(free) {}

    BufferBlock(const BufferBlock&) = delete;
    BufferBlock& operator=(const BufferBlock&) = delete;
    BufferBlock(BufferBlock&&) = default;
    BufferBlock& operator=(BufferBlock&&) = default;

    // Note: allocating a fresh buffer even if we could reuse an old one may let
    // us shed memory, and invalidates old StringPieces (making them easier to
    // catch with asan).
    void CopyFrom(const BufferBlock& rhs) {
      QUICHE_DCHECK(this != &rhs);
      buffer_size = rhs.buffer_size;
      bytes_free = rhs.bytes_free;
      if (rhs.buffer == nullptr) {
        buffer = nullptr;
      } else {
        buffer = std::make_unique<char[]>(buffer_size);
        memcpy(buffer.get(), rhs.buffer.get(), rhs.bytes_used());
      }
    }
  };

  typedef std::vector<BufferBlock> Blocks;

  BalsaBuffer()
      : blocksize_(kDefaultBlocksize), can_write_to_contiguous_buffer_(true) {}

  explicit BalsaBuffer(size_t blocksize)
      : blocksize_(blocksize), can_write_to_contiguous_buffer_(true) {}

  BalsaBuffer(const BalsaBuffer&) = delete;
  BalsaBuffer& operator=(const BalsaBuffer&) = delete;
  BalsaBuffer(BalsaBuffer&&) = default;
  BalsaBuffer& operator=(BalsaBuffer&&) = default;

  // Returns the total amount of memory reserved by the buffer blocks.
  size_t GetTotalBufferBlockSize() const {
    size_t buffer_size = 0;
    for (Blocks::const_iterator iter = blocks_.begin(); iter != blocks_.end();
         ++iter) {
      buffer_size += iter->buffer_size;
    }
    return buffer_size;
  }

  // Returns the total amount of memory used by the buffer blocks.
  size_t GetTotalBytesUsed() const {
    size_t bytes_used = 0;
    for (const auto& b : blocks_) {
      bytes_used += b.bytes_used();
    }
    return bytes_used;
  }

  const char* GetPtr(Blocks::size_type block_idx) const {
    QUICHE_DCHECK_LT(block_idx, blocks_.size())
        << block_idx << ", " << blocks_.size();
    return block_idx >= blocks_.size() ? nullptr
                                       : blocks_[block_idx].buffer.get();
  }

  char* GetPtr(Blocks::size_type block_idx) {
    QUICHE_DCHECK_LT(block_idx, blocks_.size())
        << block_idx << ", " << blocks_.size();
    return block_idx >= blocks_.size() ? nullptr
                                       : blocks_[block_idx].buffer.get();
  }

  // This function is different from Reserve(), as it ensures that the data
  // stored via subsequent calls to this function are all contiguous (and in
  // the order in which these writes happened). This is essentially the same
  // as a string append.
  //
  // You may call this function at any time between object
  // construction/Clear(), and the calling of the
  // NoMoreWriteToContiguousBuffer() function.
  //
  // You must not call this function after the NoMoreWriteToContiguousBuffer()
  // function is called, unless a Clear() has been called since.
  // If you do, the program will abort().
  //
  // This condition is placed upon this code so that calls to Reserve() can
  // append to the buffer in the first block safely, and without invaliding
  // the StringPiece which it returns.
  //
  // This function's main intended user is the BalsaFrame class, which,
  // for reasons of efficiency, requires that the buffer from which it parses
  // the headers be contiguous.
  //
  void WriteToContiguousBuffer(absl::string_view sp) {
    if (sp.empty()) {
      return;
    }
    QUICHE_CHECK(can_write_to_contiguous_buffer_);

    if (blocks_.empty()) {
      blocks_.push_back(AllocBlock());
    }

    QUICHE_DCHECK_GE(blocks_.size(), 1u);
    if (blocks_[0].buffer == nullptr && sp.size() <= blocksize_) {
      blocks_[0] = AllocBlock();
      memcpy(blocks_[0].start_of_unused_bytes(), sp.data(), sp.size());
    } else if (blocks_[0].bytes_free < sp.size()) {
      // the first block isn't big enough, resize it.
      const size_t old_storage_size_used = blocks_[0].bytes_used();
      // Increase to at least 2*old_storage_size_used; if sp.size() is larger,
      // we'll increase by that amount.
      const size_t new_storage_size =
          old_storage_size_used + (old_storage_size_used < sp.size()
                                       ? sp.size()
                                       : old_storage_size_used);
      std::unique_ptr<char[]> new_storage{new char[new_storage_size]};
      char* old_storage = blocks_[0].buffer.get();
      if (old_storage_size_used != 0u) {
        memcpy(new_storage.get(), old_storage, old_storage_size_used);
      }
      memcpy(new_storage.get() + old_storage_size_used, sp.data(), sp.size());
      blocks_[0].buffer = std::move(new_storage);
      blocks_[0].bytes_free = new_storage_size - old_storage_size_used;
      blocks_[0].buffer_size = new_storage_size;
    } else {
      memcpy(blocks_[0].start_of_unused_bytes(), sp.data(), sp.size());
    }
    blocks_[0].bytes_free -= sp.size();
  }

  void NoMoreWriteToContiguousBuffer() {
    can_write_to_contiguous_buffer_ = false;
  }

  // Reserves "permanent" storage of the size indicated. Returns a pointer to
  // the beginning of that storage, and assigns the index of the block used to
  // block_buffer_idx. This function uses the first block IFF the
  // NoMoreWriteToContiguousBuffer function has been called since the last
  // Clear/Construction.
  char* Reserve(size_t size, Blocks::size_type* block_buffer_idx) {
    if (blocks_.empty()) {
      blocks_.push_back(AllocBlock());
    }

    // There should always be a 'first_block', even if it
    // contains nothing.
    QUICHE_DCHECK_GE(blocks_.size(), 1u);
    BufferBlock* block = nullptr;
    Blocks::size_type block_idx = can_write_to_contiguous_buffer_ ? 1 : 0;
    for (; block_idx < blocks_.size(); ++block_idx) {
      if (blocks_[block_idx].bytes_free >= size) {
        block = &blocks_[block_idx];
        break;
      }
    }
    if (block == nullptr) {
      if (blocksize_ < size) {
        blocks_.push_back(AllocCustomBlock(size));
      } else {
        blocks_.push_back(AllocBlock());
      }
      block = &blocks_.back();
    }

    char* storage = block->start_of_unused_bytes();
    block->bytes_free -= size;
    if (block_buffer_idx != nullptr) {
      *block_buffer_idx = block_idx;
    }
    return storage;
  }

  void Clear() {
    blocks_.clear();
    blocks_.shrink_to_fit();
    can_write_to_contiguous_buffer_ = true;
  }

  void CopyFrom(const BalsaBuffer& b) {
    blocks_.resize(b.blocks_.size());
    for (Blocks::size_type i = 0; i < blocks_.size(); ++i) {
      blocks_[i].CopyFrom(b.blocks_[i]);
    }
    blocksize_ = b.blocksize_;
    can_write_to_contiguous_buffer_ = b.can_write_to_contiguous_buffer_;
  }

  char* StartOfFirstBlock() const {
    QUICHE_BUG_IF(bug_if_1182_1, blocks_.empty())
        << "First block not allocated yet!";
    return blocks_.empty() ? nullptr : blocks_[0].buffer.get();
  }

  char* EndOfFirstBlock() const {
    QUICHE_BUG_IF(bug_if_1182_2, blocks_.empty())
        << "First block not allocated yet!";
    return blocks_.empty() ? nullptr : blocks_[0].start_of_unused_bytes();
  }

  size_t GetReadableBytesOfFirstBlock() const {
    return blocks_.empty() ? 0 : blocks_[0].bytes_used();
  }

  bool can_write_to_contiguous_buffer() const {
    return can_write_to_contiguous_buffer_;
  }
  size_t blocksize() const { return blocksize_; }
  Blocks::size_type num_blocks() const { return blocks_.size(); }
  size_t buffer_size(size_t idx) const { return blocks_[idx].buffer_size; }
  size_t bytes_used(size_t idx) const { return blocks_[idx].bytes_used(); }

 private:
  BufferBlock AllocBlock() { return AllocCustomBlock(blocksize_); }

  BufferBlock AllocCustomBlock(size_t blocksize) {
    return BufferBlock{std::make_unique<char[]>(blocksize), blocksize,
                       blocksize};
  }

  // A container of BufferBlocks
  Blocks blocks_;

  // The default allocation size for a block.
  // In general, blocksize_ bytes will be allocated for
  // each buffer.
  size_t blocksize_;

  // If set to true, then the first block cannot be used for Reserve() calls as
  // the WriteToContiguous... function will modify the base pointer for this
  // block, and the Reserve() calls need to be sure that the base pointer will
  // not be changing in order to provide the user with StringPieces which
  // continue to be valid.
  bool can_write_to_contiguous_buffer_;
};

////////////////////////////////////////////////////////////////////////////////

// All of the functions in the BalsaHeaders class use string pieces, by either
// using the StringPiece class, or giving an explicit size and char* (as these
// are the native representation for these string pieces).
// This is done for several reasons.
//  1) This minimizes copying/allocation/deallocation as compared to using
//  string parameters
//  2) This reduces the number of strlen() calls done (as the length of any
//  string passed in is relatively likely to be known at compile time, and for
//  those strings passed back we obviate the need for a strlen() to determine
//  the size of new storage allocations if a new allocation is required.
//  3) This class attempts to store all of its data in two linear buffers in
//  order to enhance the speed of parsing and writing out to a buffer. As a
//  result, many string pieces are -not- terminated by '\0', and are not
//  c-strings.  Since this is the case, we must delineate the length of the
//  string explicitly via a length.
//
//  WARNING:  The side effect of using StringPiece is that if the underlying
//  buffer changes (due to modifying the headers) the StringPieces which point
//  to the data which was modified, may now contain "garbage", and should not
//  be dereferenced.
//  For example, If you fetch some component of the first-line, (request or
//  response), and then you modify the first line, the StringPieces you
//  originally received from the original first-line may no longer be valid).
//
//  StringPieces pointing to pieces of header lines which have not been
//  erased() or modified should be valid until the object is cleared or
//  destroyed.
//
//  Key comparisons are case-insensitive.

class QUICHE_EXPORT BalsaHeaders : public HeaderApi {
 public:
  // Each header line is parsed into a HeaderLineDescription, which maintains
  // pointers into the BalsaBuffer.
  //
  // Succinctly describes one header line as indices into a buffer.
  struct QUICHE_EXPORT HeaderLineDescription {
    HeaderLineDescription(size_t first_character_index, size_t key_end_index,
                          size_t value_begin_index, size_t last_character_index,
                          size_t buffer_base_index)
        : first_char_idx(first_character_index),
          key_end_idx(key_end_index),
          value_begin_idx(value_begin_index),
          last_char_idx(last_character_index),
          buffer_base_idx(buffer_base_index),
          skip(false) {}

    HeaderLineDescription()
        : first_char_idx(0),
          key_end_idx(0),
          value_begin_idx(0),
          last_char_idx(0),
          buffer_base_idx(0),
          skip(false) {}

    size_t KeyLength() const {
      QUICHE_DCHECK_GE(key_end_idx, first_char_idx);
      return key_end_idx - first_char_idx;
    }
    size_t ValuesLength() const {
      QUICHE_DCHECK_GE(last_char_idx, value_begin_idx);
      return last_char_idx - value_begin_idx;
    }

    size_t first_char_idx;
    size_t key_end_idx;
    size_t value_begin_idx;
    size_t last_char_idx;
    BalsaBuffer::Blocks::size_type buffer_base_idx;
    bool skip;
  };

  using HeaderTokenList = std::vector<absl::string_view>;

  // An iterator for walking through all the header lines.
  class const_header_lines_iterator;

  // An iterator that only stops at lines with a particular key
  // (case-insensitive).  See also GetIteratorForKey.
  //
  // Check against header_lines_key_end() to determine when iteration is
  // finished. lines().end() will also work.
  class const_header_lines_key_iterator;

  // A simple class that can be used in a range-based for loop.
  template <typename IteratorType>
  class QUICHE_EXPORT iterator_range {
   public:
    using iterator = IteratorType;
    using const_iterator = IteratorType;
    using value_type = typename std::iterator_traits<IteratorType>::value_type;

    iterator_range(IteratorType begin_iterator, IteratorType end_iterator)
        : begin_iterator_(std::move(begin_iterator)),
          end_iterator_(std::move(end_iterator)) {}

    IteratorType begin() const { return begin_iterator_; }
    IteratorType end() const { return end_iterator_; }

   private:
    IteratorType begin_iterator_, end_iterator_;
  };

  // Set of names of headers that might have multiple values.
  // CoalesceOption::kCoalesce can be used to match Envoy behavior in
  // WriteToBuffer().
  using MultivaluedHeadersSet =
      absl::flat_hash_set<absl::string_view, StringPieceCaseHash,
                          StringPieceCaseEqual>;

  // Map of key => vector<value>, where vector contains ordered list of all
  // values for |key| (ignoring the casing).
  using MultivaluedHeadersValuesMap =
      absl::flat_hash_map<absl::string_view, std::vector<absl::string_view>,
                          StringPieceCaseHash, StringPieceCaseEqual>;

  BalsaHeaders()
      : balsa_buffer_(4096),
        content_length_(0),
        content_length_status_(BalsaHeadersEnums::NO_CONTENT_LENGTH),
        parsed_response_code_(0),
        firstline_buffer_base_idx_(0),
        whitespace_1_idx_(0),
        non_whitespace_1_idx_(0),
        whitespace_2_idx_(0),
        non_whitespace_2_idx_(0),
        whitespace_3_idx_(0),
        non_whitespace_3_idx_(0),
        whitespace_4_idx_(0),
        transfer_encoding_is_chunked_(false) {}

  explicit BalsaHeaders(size_t bufsize)
      : balsa_buffer_(bufsize),
        content_length_(0),
        content_length_status_(BalsaHeadersEnums::NO_CONTENT_LENGTH),
        parsed_response_code_(0),
        firstline_buffer_base_idx_(0),
        whitespace_1_idx_(0),
        non_whitespace_1_idx_(0),
        whitespace_2_idx_(0),
        non_whitespace_2_idx_(0),
        whitespace_3_idx_(0),
        non_whitespace_3_idx_(0),
        whitespace_4_idx_(0),
        transfer_encoding_is_chunked_(false) {}

  // Copying BalsaHeaders is expensive, so require that it be visible.
  BalsaHeaders(const BalsaHeaders&) = delete;
  BalsaHeaders& operator=(const BalsaHeaders&) = delete;
  BalsaHeaders(BalsaHeaders&&) = default;
  BalsaHeaders& operator=(BalsaHeaders&&) = default;

  // Returns a range that represents all of the header lines.
  iterator_range<const_header_lines_iterator> lines() const;

  // Returns an iterator range consisting of the header lines matching key.
  // String backing 'key' must remain valid for lifetime of range.
  iterator_range<const_header_lines_key_iterator> lines(
      absl::string_view key) const;

  // Returns a forward-only iterator that only stops at lines matching key.
  // String backing 'key' must remain valid for lifetime of iterator.
  //
  // Check returned iterator against header_lines_key_end() to determine when
  // iteration is finished.
  //
  // Consider calling lines(key)--it may be more readable.
  const_header_lines_key_iterator GetIteratorForKey(
      absl::string_view key) const;

  const_header_lines_key_iterator header_lines_key_end() const;

  void erase(const const_header_lines_iterator& it);

  void Clear();

  // Explicit copy functions to avoid risk of accidental copies.
  BalsaHeaders Copy() const {
    BalsaHeaders copy;
    copy.CopyFrom(*this);
    return copy;
  }
  void CopyFrom(const BalsaHeaders& other);

  // Replaces header entries with key 'key' if they exist, or appends
  // a new header if none exist.  See 'AppendHeader' below for additional
  // comments about ContentLength and TransferEncoding headers. Note that this
  // will allocate new storage every time that it is called.
  void ReplaceOrAppendHeader(absl::string_view key,
                             absl::string_view value) override;

  // Append a new header entry to the header object. Clients who wish to append
  // Content-Length header should use SetContentLength() method instead of
  // adding the content length header using AppendHeader (manually adding the
  // content length header will not update the content_length_ and
  // content_length_status_ values).
  // Similarly, clients who wish to add or remove the transfer encoding header
  // in order to apply or remove chunked encoding should use
  // SetTransferEncodingToChunkedAndClearContentLength() or
  // SetNoTransferEncoding() instead.
  void AppendHeader(absl::string_view key, absl::string_view value) override;

  // Appends ',value' to an existing header named 'key'.  If no header with the
  // correct key exists, it will call AppendHeader(key, value).  Calling this
  // function on a key which exists several times in the headers will produce
  // unpredictable results.
  void AppendToHeader(absl::string_view key, absl::string_view value) override;

  // Appends ', value' to an existing header named 'key'.  If no header with the
  // correct key exists, it will call AppendHeader(key, value).  Calling this
  // function on a key which exists several times in the headers will produce
  // unpredictable results.
  void AppendToHeaderWithCommaAndSpace(absl::string_view key,
                                       absl::string_view value) override;

  // Returns the value corresponding to the given header key. Returns an empty
  // string if the header key does not exist. For headers that may consist of
  // multiple lines, use GetAllOfHeader() instead.
  // Make the QuicheLowerCaseString overload visible,
  // and only override the absl::string_view one.
  using HeaderApi::GetHeader;
  absl::string_view GetHeader(absl::string_view key) const override;

  // Iterates over all currently valid header lines, appending their
  // values into the vector 'out', in top-to-bottom order.
  // Header-lines which have been erased are not currently valid, and
  // will not have their values appended. Empty values will be
  // represented as empty string. If 'key' doesn't exist in the headers at
  // all, out will not be changed. We do not clear the vector out
  // before adding new entries. If there are header lines with matching
  // key but empty value then they are also added to the vector out.
  // (Basically empty values are not treated in any special manner).
  //
  // Example:
  // Input header:
  // "GET / HTTP/1.0\r\n"
  //    "key1: v1\r\n"
  //    "key1: \r\n"
  //    "key1:\r\n"
  //    "key1:  v1\r\n"
  //    "key1:v2\r\n"
  //
  //  vector out is initially: ["foo"]
  //  vector out after GetAllOfHeader("key1", &out) is:
  // ["foo", "v1", "", "", "v1", "v2"]
  //
  // See gfe::header_properties::IsMultivaluedHeader() for which headers
  // GFE treats as being multivalued.

  // Make the QuicheLowerCaseString overload visible,
  // and only override the absl::string_view one.
  using HeaderApi::GetAllOfHeader;
  void GetAllOfHeader(absl::string_view key,
                      std::vector<absl::string_view>* out) const override;

  // Same as above, but iterates over all header lines including removed ones.
  // Appends their values into the vector 'out' in top-to-bottom order,
  // first all valid headers then all that were removed.
  void GetAllOfHeaderIncludeRemoved(absl::string_view key,
                                    std::vector<absl::string_view>* out) const;

  // Joins all values for `key` into a comma-separated string.
  // Make the QuicheLowerCaseString overload visible,
  // and only override the absl::string_view one.
  using HeaderApi::GetAllOfHeaderAsString;
  std::string GetAllOfHeaderAsString(absl::string_view key) const override;

  // Determine if a given header is present.  Case-insensitive.
  inline bool HasHeader(absl::string_view key) const override {
    return GetConstHeaderLinesIterator(key) != header_lines_.end();
  }

  // Goes through all headers with key 'key' and checks to see if one of the
  // values is 'value'.  Returns true if there are headers with the desired key
  // and value, false otherwise.  Case-insensitive for the key; case-sensitive
  // for the value.
  bool HeaderHasValue(absl::string_view key,
                      absl::string_view value) const override {
    return HeaderHasValueHelper(key, value, true);
  }
  // Same as above, but also case-insensitive for the value.
  bool HeaderHasValueIgnoreCase(absl::string_view key,
                                absl::string_view value) const override {
    return HeaderHasValueHelper(key, value, false);
  }

  // Returns true iff any header 'key' exists with non-empty value.
  bool HasNonEmptyHeader(absl::string_view key) const override;

  const_header_lines_iterator GetHeaderPosition(absl::string_view key) const;

  // Removes all headers in given set |keys| at once efficiently. Keys
  // are case insensitive.
  //
  // Alternatives considered:
  //
  // 1. Use string_hash_set<>, the caller (such as ClearHopByHopHeaders) lower
  // cases the keys and RemoveAllOfHeaderInList just does lookup. This according
  // to microbenchmark gives the best performance because it does not require
  // an extra copy of the hash table. However, it is not taken because of the
  // possible risk that caller could forget to lowercase the keys.
  //
  // 2. Use flat_hash_set<StringPiece, StringPieceCaseHash,StringPieceCaseEqual>
  // or string_hash_set<StringPieceCaseHash, StringPieceCaseEqual>. Both appear
  // to have (much) worse performance with WithoutDupToken and LongHeader case
  // in microbenchmark.
  void RemoveAllOfHeaderInList(const HeaderTokenList& keys) override;

  void RemoveAllOfHeader(absl::string_view key) override;

  // Removes all headers starting with 'key' [case insensitive]
  void RemoveAllHeadersWithPrefix(absl::string_view prefix) override;

  // Returns true if we have at least one header with given prefix
  // [case insensitive]. Currently for test use only.
  bool HasHeadersWithPrefix(absl::string_view prefix) const override;

  // Returns the key value pairs for all headers where the header key begins
  // with the specified prefix.
  void GetAllOfHeaderWithPrefix(
      absl::string_view prefix,
      std::vector<std::pair<absl::string_view, absl::string_view>>* out)
      const override;

  void GetAllHeadersWithLimit(
      std::vector<std::pair<absl::string_view, absl::string_view>>* out,
      int limit) const override;

  // Removes all values equal to a given value from header lines with given key.
  // All string operations done here are case-sensitive.
  // If a header line has only values matching the given value, the entire
  // line is removed.
  // If the given value is found in a multi-value header line mixed with other
  // values, the line is edited in-place to remove the values.
  // Returns the number of occurrences of value that were removed.
  // This method runs in linear time.
  size_t RemoveValue(absl::string_view key, absl::string_view value);

  // Returns the upper bound on the required buffer space to fully write out
  // the header object (this include the first line, all header lines, and the
  // final line separator that marks the ending of the header).
  size_t GetSizeForWriteBuffer() const override;

  // Indicates if to serialize headers with lower-case header keys.
  enum class CaseOption { kNoModification, kLowercase, kPropercase };

  // Indicates if to coalesce headers with multiple values to match Envoy/GFE3.
  enum class CoalesceOption { kNoCoalesce, kCoalesce };

  // The following WriteHeader* methods are template member functions that
  // place one requirement on the Buffer class: it must implement a Write
  // method that takes a pointer and a length. The buffer passed in is not
  // required to be stretchable. For non-stretchable buffers, the user must
  // call GetSizeForWriteBuffer() to find out the upper bound on the output
  // buffer space required to make sure that the entire header is serialized.
  // BalsaHeaders will not check that there is adequate space in the buffer
  // object during the write.

  // Writes the entire header and the final line separator that marks the end
  // of the HTTP header section to the buffer. After this method returns, no
  // more header data should be written to the buffer.
  template <typename Buffer>
  void WriteHeaderAndEndingToBuffer(Buffer* buffer, CaseOption case_option,
                                    CoalesceOption coalesce_option) const {
    WriteToBuffer(buffer, case_option, coalesce_option);
    WriteHeaderEndingToBuffer(buffer);
  }

  template <typename Buffer>
  void WriteHeaderAndEndingToBuffer(Buffer* buffer) const {
    WriteHeaderAndEndingToBuffer(buffer, CaseOption::kNoModification,
                                 CoalesceOption::kNoCoalesce);
  }

  // Writes the final line separator to the buffer to terminate the HTTP header
  // section.  After this method returns, no more header data should be written
  // to the buffer.
  template <typename Buffer>
  static void WriteHeaderEndingToBuffer(Buffer* buffer) {
    buffer->WriteString("\r\n");
  }

  // Writes the entire header to the buffer without the line separator that
  // terminates the HTTP header. This lets users append additional header lines
  // using WriteHeaderLineToBuffer and then terminate the header with
  // WriteHeaderEndingToBuffer as the header is serialized to the buffer,
  // without having to first copy the header.
  template <typename Buffer>
  void WriteToBuffer(Buffer* buffer, CaseOption case_option,
                     CoalesceOption coalesce_option) const;

  template <typename Buffer>
  void WriteToBuffer(Buffer* buffer) const {
    WriteToBuffer(buffer, CaseOption::kNoModification,
                  CoalesceOption::kNoCoalesce);
  }

  // Used by WriteToBuffer to coalesce multiple values of headers listed in
  // |multivalued_headers| into a single comma-separated value. Public for test.
  template <typename Buffer>
  void WriteToBufferCoalescingMultivaluedHeaders(
      Buffer* buffer, const MultivaluedHeadersSet& multivalued_headers,
      CaseOption case_option) const;

  // Populates |multivalues| with values of |header_lines_| with keys present
  // in |multivalued_headers| set.
  void GetValuesOfMultivaluedHeaders(
      const MultivaluedHeadersSet& multivalued_headers,
      MultivaluedHeadersValuesMap* multivalues) const;

  static std::string ToPropercase(absl::string_view header) {
    std::string copy = std::string(header);
    bool should_uppercase = true;
    for (char& c : copy) {
      if (!absl::ascii_isalnum(c)) {
        should_uppercase = true;
      } else if (should_uppercase) {
        c = absl::ascii_toupper(c);
        should_uppercase = false;
      } else {
        c = absl::ascii_tolower(c);
      }
    }
    return copy;
  }

  template <typename Buffer>
  void WriteHeaderKeyToBuffer(Buffer* buffer, absl::string_view key,
                              CaseOption case_option) const {
    if (case_option == CaseOption::kLowercase) {
      buffer->WriteString(absl::AsciiStrToLower(key));
    } else if (case_option == CaseOption::kPropercase) {
      const auto& header_set = quiche::GetStandardHeaderSet();
      auto it = header_set.find(key);
      if (it != header_set.end()) {
        buffer->WriteString(*it);
      } else {
        buffer->WriteString(ToPropercase(key));
      }
    } else {
      buffer->WriteString(key);
    }
  }

  // Takes a header line in the form of a key/value pair and append it to the
  // buffer. This function should be called after WriteToBuffer to
  // append additional header lines to the header without copying the header.
  // When the user is done with appending to the buffer,
  // WriteHeaderEndingToBuffer must be used to terminate the HTTP
  // header in the buffer. This method is a no-op if key is empty.
  template <typename Buffer>
  void WriteHeaderLineToBuffer(Buffer* buffer, absl::string_view key,
                               absl::string_view value,
                               CaseOption case_option) const {
    // If the key is empty, we don't want to write the rest because it
    // will not be a well-formed header line.
    if (!key.empty()) {
      WriteHeaderKeyToBuffer(buffer, key, case_option);
      buffer->WriteString(": ");
      buffer->WriteString(value);
      buffer->WriteString("\r\n");
    }
  }

  // Takes a header line in the form of a key and vector of values and appends
  // it to the buffer. This function should be called after WriteToBuffer to
  // append additional header lines to the header without copying the header.
  // When the user is done with appending to the buffer,
  // WriteHeaderEndingToBuffer must be used to terminate the HTTP
  // header in the buffer. This method is a no-op if the |key| is empty.
  template <typename Buffer>
  void WriteHeaderLineValuesToBuffer(
      Buffer* buffer, absl::string_view key,
      const std::vector<absl::string_view>& values,
      CaseOption case_option) const {
    // If the key is empty, we don't want to write the rest because it
    // will not be a well-formed header line.
    if (!key.empty()) {
      WriteHeaderKeyToBuffer(buffer, key, case_option);
      buffer->WriteString(": ");
      for (auto it = values.begin();;) {
        buffer->WriteString(*it);
        if (++it == values.end()) {
          break;
        }
        buffer->WriteString(",");
      }
      buffer->WriteString("\r\n");
    }
  }

  // Dump the textural representation of the header object to a string, which
  // is suitable for writing out to logs. All CRLF will be printed out as \n.
  // This function can be called on a header object in any state. Raw header
  // data will be printed out if the header object is not completely parsed,
  // e.g., when there was an error in the middle of parsing.
  // The header content is appended to the string; the original content is not
  // cleared.
  // If used in test cases, WillNotWriteFromFramer() may be of interest.
  void DumpToString(std::string* str) const;
  std::string DebugString() const override;

  bool ForEachHeader(
      quiche::UnretainedCallback<bool(const absl::string_view key,
                                      const absl::string_view value)>
          fn) const override;

  void DumpToPrefixedString(const char* spaces, std::string* str) const;

  absl::string_view first_line() const {
    QUICHE_DCHECK_GE(whitespace_4_idx_, non_whitespace_1_idx_);
    return whitespace_4_idx_ == non_whitespace_1_idx_
               ? ""
               : absl::string_view(
                     BeginningOfFirstLine() + non_whitespace_1_idx_,
                     whitespace_4_idx_ - non_whitespace_1_idx_);
  }
  std::string first_line_of_request() const override {
    return std::string(first_line());
  }

  // Returns the parsed value of the response code if it has been parsed.
  // Guaranteed to return 0 when unparsed (though it is a much better idea to
  // verify that the BalsaFrame had no errors while parsing).
  // This may return response codes which are outside the normal bounds of
  // HTTP response codes-- it is up to the user of this class to ensure that
  // the response code is one which is interpretable.
  size_t parsed_response_code() const override { return parsed_response_code_; }

  absl::string_view request_method() const override {
    QUICHE_DCHECK_GE(whitespace_2_idx_, non_whitespace_1_idx_);
    return whitespace_2_idx_ == non_whitespace_1_idx_
               ? ""
               : absl::string_view(
                     BeginningOfFirstLine() + non_whitespace_1_idx_,
                     whitespace_2_idx_ - non_whitespace_1_idx_);
  }

  absl::string_view response_version() const override {
    // Note: There is no difference between request_method() and
    // response_version(). They both could be called
    // GetFirstTokenFromFirstline()... but that wouldn't be anywhere near as
    // descriptive.
    return request_method();
  }

  absl::string_view request_uri() const override {
    QUICHE_DCHECK_GE(whitespace_3_idx_, non_whitespace_2_idx_);
    return whitespace_3_idx_ == non_whitespace_2_idx_
               ? ""
               : absl::string_view(
                     BeginningOfFirstLine() + non_whitespace_2_idx_,
                     whitespace_3_idx_ - non_whitespace_2_idx_);
  }

  absl::string_view response_code() const override {
    // Note: There is no difference between request_uri() and response_code().
    // They both could be called GetSecondtTokenFromFirstline(), but, as noted
    // in an earlier comment, that wouldn't be as descriptive.
    return request_uri();
  }

  absl::string_view request_version() const override {
    QUICHE_DCHECK_GE(whitespace_4_idx_, non_whitespace_3_idx_);
    return whitespace_4_idx_ == non_whitespace_3_idx_
               ? ""
               : absl::string_view(
                     BeginningOfFirstLine() + non_whitespace_3_idx_,
                     whitespace_4_idx_ - non_whitespace_3_idx_);
  }

  absl::string_view response_reason_phrase() const override {
    // Note: There is no difference between request_version() and
    // response_reason_phrase(). They both could be called
    // GetThirdTokenFromFirstline(), but, as noted in an earlier comment, that
    // wouldn't be as descriptive.
    return request_version();
  }

  void SetRequestFirstlineFromStringPieces(absl::string_view method,
                                           absl::string_view uri,
                                           absl::string_view version) {
    SetFirstlineFromStringPieces(method, uri, version);
  }

  void SetResponseFirstline(absl::string_view version,
                            size_t parsed_response_code,
                            absl::string_view reason_phrase);

  // These functions are exactly the same, except that their names are
  // different. This is done so that the code using this class is more
  // expressive.
  void SetRequestMethod(absl::string_view method) override;
  void SetResponseVersion(absl::string_view version) override;

  void SetRequestUri(absl::string_view uri) override;
  void SetResponseCode(absl::string_view code) override;
  void set_parsed_response_code(size_t parsed_response_code) {
    parsed_response_code_ = parsed_response_code;
  }
  void SetParsedResponseCodeAndUpdateFirstline(
      size_t parsed_response_code) override;

  // These functions are exactly the same, except that their names are
  // different. This is done so that the code using this class is more
  // expressive.
  void SetRequestVersion(absl::string_view version) override;
  void SetResponseReasonPhrase(absl::string_view reason_phrase) override;

  // Simple accessors to some of the internal state
  bool transfer_encoding_is_chunked() const {
    return transfer_encoding_is_chunked_;
  }

  static bool ResponseCodeImpliesNoBody(size_t code) {
    // From HTTP spec section 6.1.1 all 1xx responses must not have a body,
    // as well as 204 No Content and 304 Not Modified.
    return ((code >= 100) && (code <= 199)) || (code == 204) || (code == 304);
  }

  // Note: never check this for requests. Nothing bad will happen if you do,
  // but spec does not allow requests framed by connection close.
  // TODO(vitaliyl): refactor.
  bool is_framed_by_connection_close() const {
    // We declare that response is framed by connection close if it has no
    // content-length, no transfer encoding, and is allowed to have a body by
    // the HTTP spec.
    // parsed_response_code_ is 0 for requests, so ResponseCodeImpliesNoBody
    // will return false.
    return (content_length_status_ == BalsaHeadersEnums::NO_CONTENT_LENGTH) &&
           !transfer_encoding_is_chunked_ &&
           !ResponseCodeImpliesNoBody(parsed_response_code_);
  }

  size_t content_length() const override { return content_length_; }
  BalsaHeadersEnums::ContentLengthStatus content_length_status() const {
    return content_length_status_;
  }
  bool content_length_valid() const override {
    return content_length_status_ == BalsaHeadersEnums::VALID_CONTENT_LENGTH;
  }

  // SetContentLength, SetTransferEncodingToChunkedAndClearContentLength, and
  // SetNoTransferEncoding modifies the header object to use
  // content-length and transfer-encoding headers in a consistent
  // manner. They set all internal flags and status so client can get
  // a consistent view from various accessors.
  void SetContentLength(size_t length) override;
  // Sets transfer-encoding to chunked and updates internal state.
  void SetTransferEncodingToChunkedAndClearContentLength() override;
  // Removes transfer-encoding headers and updates internal state.
  void SetNoTransferEncoding() override;

  // If you have a response that needs framing by connection close, use this
  // method instead of RemoveAllOfHeader("Content-Length"). Has no effect if
  // transfer_encoding_is_chunked().
  void ClearContentLength();

  // This should be called if balsa headers are created entirely manually (not
  // by any of the framer classes) to make sure that function calls like
  // DumpToString will work correctly.
  void WillNotWriteFromFramer() {
    balsa_buffer_.NoMoreWriteToContiguousBuffer();
  }

  // True if DoneWritingFromFramer or WillNotWriteFromFramer is called.
  bool FramerIsDoneWriting() const {
    return !balsa_buffer_.can_write_to_contiguous_buffer();
  }

  bool IsEmpty() const override;

  // From HeaderApi and ConstHeaderApi.
  absl::string_view Authority() const override;
  void ReplaceOrAppendAuthority(absl::string_view value) override;
  void RemoveAuthority() override;
  void ApplyToCookie(quiche::UnretainedCallback<void(absl::string_view cookie)>
                         f) const override;

  void set_enforce_header_policy(bool enforce) override {
    enforce_header_policy_ = enforce;
  }

  // Removes the last token from the header value. In the presence of multiple
  // header lines with given key, will remove the last token of the last line.
  // Can be useful if the last encoding has to be removed.
  void RemoveLastTokenFromHeaderValue(absl::string_view key);

  // Gets the list of names of headers that are multivalued in Envoy.
  static const MultivaluedHeadersSet& multivalued_envoy_headers();

  // Returns true if HTTP responses with this response code have bodies.
  static bool ResponseCanHaveBody(int response_code);

  // Given a pointer to the beginning and the end of the header value
  // in some buffer, populates tokens list with beginning and end indices
  // of all tokens present in the value string.
  static void ParseTokenList(absl::string_view header_value,
                             HeaderTokenList* tokens);

 private:
  typedef std::vector<HeaderLineDescription> HeaderLines;

  class iterator_base;

  friend class BalsaFrame;
  friend class gfe2::Http2HeaderValidator;
  friend class SpdyPayloadFramer;
  friend class HTTPMessage;
  friend class test::BalsaHeadersTestPeer;

  friend bool ParseHTTPFirstLine(
      char* begin, char* end, bool is_request, BalsaHeaders* headers,
      BalsaFrameEnums::ErrorCode* error_code,
      HttpValidationPolicy::FirstLineValidationOption whitespace_option);

  // Reverse iterators have been removed for lack of use, refer to
  // cl/30618773 in case they are needed.

  const char* BeginningOfFirstLine() const {
    return GetPtr(firstline_buffer_base_idx_);
  }

  char* BeginningOfFirstLine() { return GetPtr(firstline_buffer_base_idx_); }

  char* GetPtr(BalsaBuffer::Blocks::size_type block_idx) {
    return balsa_buffer_.GetPtr(block_idx);
  }

  const char* GetPtr(BalsaBuffer::Blocks::size_type block_idx) const {
    return balsa_buffer_.GetPtr(block_idx);
  }

  void WriteFromFramer(const char* ptr, size_t size) {
    balsa_buffer_.WriteToContiguousBuffer(absl::string_view(ptr, size));
  }

  void DoneWritingFromFramer() {
    balsa_buffer_.NoMoreWriteToContiguousBuffer();
  }

  char* OriginalHeaderStreamBegin() const {
    return balsa_buffer_.StartOfFirstBlock();
  }

  char* OriginalHeaderStreamEnd() const {
    return balsa_buffer_.EndOfFirstBlock();
  }

  size_t GetReadableBytesFromHeaderStream() const {
    return balsa_buffer_.GetReadableBytesOfFirstBlock();
  }

  absl::string_view GetReadablePtrFromHeaderStream() {
    return {OriginalHeaderStreamBegin(), GetReadableBytesFromHeaderStream()};
  }

  absl::string_view GetValueFromHeaderLineDescription(
      const HeaderLineDescription& line) const;

  void AddAndMakeDescription(absl::string_view key, absl::string_view value,
                             HeaderLineDescription* d);

  void AppendAndMakeDescription(absl::string_view key, absl::string_view value,
                                HeaderLineDescription* d);

  // Removes all header lines with the given key starting at start.
  void RemoveAllOfHeaderStartingAt(absl::string_view key,
                                   HeaderLines::iterator start);

  HeaderLines::const_iterator GetConstHeaderLinesIterator(
      absl::string_view key) const;

  HeaderLines::iterator GetHeaderLinesIterator(absl::string_view key,
                                               HeaderLines::iterator start);

  HeaderLines::iterator GetHeaderLinesIteratorForLastMultivaluedHeader(
      absl::string_view key);

  template <typename IteratorType>
  const IteratorType HeaderLinesBeginHelper() const;

  template <typename IteratorType>
  const IteratorType HeaderLinesEndHelper() const;

  // Helper function for HeaderHasValue and HeaderHasValueIgnoreCase that
  // does most of the work.
  bool HeaderHasValueHelper(absl::string_view key, absl::string_view value,
                            bool case_sensitive) const;

  // Called by header removal methods to reset internal values for transfer
  // encoding or content length if we're removing the corresponding headers.
  void MaybeClearSpecialHeaderValues(absl::string_view key);

  void SetFirstlineFromStringPieces(absl::string_view firstline_a,
                                    absl::string_view firstline_b,
                                    absl::string_view firstline_c);
  BalsaBuffer balsa_buffer_;

  size_t content_length_;
  BalsaHeadersEnums::ContentLengthStatus content_length_status_;
  size_t parsed_response_code_;
  // HTTP firstlines all have the following structure:
  //  LWS         NONWS  LWS    NONWS   LWS    NONWS   NOTCRLF  CRLF
  //  [\t \r\n]+ [^\t ]+ [\t ]+ [^\t ]+ [\t ]+ [^\t ]+ [^\r\n]+ "\r\n"
  //  ws1        nws1    ws2    nws2    ws3    nws3             ws4
  //  |          [-------)      [-------)      [----------------)
  //    REQ:     method         request_uri    version
  //   RESP:     version        statuscode     reason
  //
  //   The first NONWS->LWS component we'll call firstline_a.
  //   The second firstline_b, and the third firstline_c.
  //
  //   firstline_a goes from nws1 to (but not including) ws2
  //   firstline_b goes from nws2 to (but not including) ws3
  //   firstline_c goes from nws3 to (but not including) ws4
  //
  // In the code:
  //    ws1 == whitespace_1_idx_
  //   nws1 == non_whitespace_1_idx_
  //    ws2 == whitespace_2_idx_
  //   nws2 == non_whitespace_2_idx_
  //    ws3 == whitespace_3_idx_
  //   nws3 == non_whitespace_3_idx_
  //    ws4 == whitespace_4_idx_
  BalsaBuffer::Blocks::size_type firstline_buffer_base_idx_;
  size_t whitespace_1_idx_;
  size_t non_whitespace_1_idx_;
  size_t whitespace_2_idx_;
  size_t non_whitespace_2_idx_;
  size_t whitespace_3_idx_;
  size_t non_whitespace_3_idx_;
  size_t whitespace_4_idx_;

  bool transfer_encoding_is_chunked_;

  // If true, QUICHE_BUG if a header that starts with an invalid prefix is
  // explicitly set.
  bool enforce_header_policy_ = true;

  HeaderLines header_lines_;
};

// Base class for iterating the headers in a BalsaHeaders object, returning a
// pair of string_view's for each header.
class QUICHE_EXPORT BalsaHeaders::iterator_base
    : public std::iterator<std::forward_iterator_tag,
                           std::pair<absl::string_view, absl::string_view>> {
 public:
  iterator_base() : headers_(nullptr), idx_(0) {}

  std::pair<absl::string_view, absl::string_view>& operator*() const {
    return Lookup(idx_);
  }

  std::pair<absl::string_view, absl::string_view>* operator->() const {
    return &(this->operator*());
  }

  bool operator==(const BalsaHeaders::iterator_base& it) const {
    return idx_ == it.idx_;
  }

  bool operator<(const BalsaHeaders::iterator_base& it) const {
    return idx_ < it.idx_;
  }

  bool operator<=(const BalsaHeaders::iterator_base& it) const {
    return idx_ <= it.idx_;
  }

  bool operator!=(const BalsaHeaders::iterator_base& it) const {
    return !(*this == it);
  }

  bool operator>(const BalsaHeaders::iterator_base& it) const {
    return it < *this;
  }

  bool operator>=(const BalsaHeaders::iterator_base& it) const {
    return it <= *this;
  }

  // This mainly exists so that we can have interesting output for
  // unittesting. The EXPECT_EQ, EXPECT_NE functions require that
  // operator<< work for the classes it sees.  It would be better if there
  // was an additional traits-like system for the gUnit output... but oh
  // well.
  friend QUICHE_EXPORT std::ostream& operator<<(std::ostream& os,
                                                const iterator_base& it) {
    os << "[" << it.headers_ << ", " << it.idx_ << "]";
    return os;
  }

 private:
  friend class BalsaHeaders;

  iterator_base(const BalsaHeaders* headers, HeaderLines::size_type index)
      : headers_(headers), idx_(index) {}

  void increment() {
    value_.reset();
    const HeaderLines& header_lines = headers_->header_lines_;
    const HeaderLines::size_type header_lines_size = header_lines.size();
    const HeaderLines::size_type original_idx = idx_;
    do {
      ++idx_;
    } while (idx_ < header_lines_size && header_lines[idx_].skip == true);
    // The condition below exists so that ++(end() - 1) == end(), even
    // if there are only 'skip == true' elements between the end() iterator
    // and the end of the vector of HeaderLineDescriptions.
    if (idx_ == header_lines_size) {
      idx_ = original_idx + 1;
    }
  }

  std::pair<absl::string_view, absl::string_view>& Lookup(
      HeaderLines::size_type index) const {
    QUICHE_DCHECK_LT(index, headers_->header_lines_.size());
    if (!value_.has_value()) {
      const HeaderLineDescription& line = headers_->header_lines_[index];
      const char* stream_begin = headers_->GetPtr(line.buffer_base_idx);
      value_ =
          std::make_pair(absl::string_view(stream_begin + line.first_char_idx,
                                           line.KeyLength()),
                         absl::string_view(stream_begin + line.value_begin_idx,
                                           line.ValuesLength()));
    }
    return *value_;
  }

  const BalsaHeaders* headers_;
  HeaderLines::size_type idx_;
  mutable std::optional<std::pair<absl::string_view, absl::string_view>> value_;
};

// A const iterator for all the header lines.
class QUICHE_EXPORT BalsaHeaders::const_header_lines_iterator
    : public BalsaHeaders::iterator_base {
 public:
  const_header_lines_iterator() : iterator_base() {}

  const_header_lines_iterator& operator++() {
    iterator_base::increment();
    return *this;
  }

 private:
  friend class BalsaHeaders;

  const_header_lines_iterator(const BalsaHeaders* headers,
                              HeaderLines::size_type index)
      : iterator_base(headers, index) {}
};

// A const iterator that stops only on header lines for a particular key.
class QUICHE_EXPORT BalsaHeaders::const_header_lines_key_iterator
    : public BalsaHeaders::iterator_base {
 public:
  const_header_lines_key_iterator& operator++() {
    do {
      iterator_base::increment();
    } while (!AtEnd() && !absl::EqualsIgnoreCase(key_, (**this).first));
    return *this;
  }

  // Only forward-iteration makes sense, so no operator-- defined.

 private:
  friend class BalsaHeaders;

  const_header_lines_key_iterator(const BalsaHeaders* headers,
                                  HeaderLines::size_type index,
                                  absl::string_view key)
      : iterator_base(headers, index), key_(key) {}

  // Should only be used for creating an end iterator.
  const_header_lines_key_iterator(const BalsaHeaders* headers,
                                  HeaderLines::size_type index)
      : iterator_base(headers, index) {}

  bool AtEnd() const { return *this >= headers_->lines().end(); }

  absl::string_view key_;
};

inline BalsaHeaders::iterator_range<BalsaHeaders::const_header_lines_iterator>
BalsaHeaders::lines() const {
  return {HeaderLinesBeginHelper<const_header_lines_iterator>(),
          HeaderLinesEndHelper<const_header_lines_iterator>()};
}

inline BalsaHeaders::iterator_range<
    BalsaHeaders::const_header_lines_key_iterator>
BalsaHeaders::lines(absl::string_view key) const {
  return {GetIteratorForKey(key), header_lines_key_end()};
}

inline BalsaHeaders::const_header_lines_key_iterator
BalsaHeaders::header_lines_key_end() const {
  return HeaderLinesEndHelper<const_header_lines_key_iterator>();
}

inline void BalsaHeaders::erase(const const_header_lines_iterator& it) {
  QUICHE_DCHECK_EQ(it.headers_, this);
  QUICHE_DCHECK_LT(it.idx_, header_lines_.size());
  header_lines_[it.idx_].skip = true;
}

template <typename Buffer>
void BalsaHeaders::WriteToBuffer(Buffer* buffer, CaseOption case_option,
                                 CoalesceOption coalesce_option) const {
  // write the first line.
  const absl::string_view firstline = first_line();
  if (!firstline.empty()) {
    buffer->WriteString(firstline);
  }
  buffer->WriteString("\r\n");
  if (coalesce_option != CoalesceOption::kCoalesce) {
    const HeaderLines::size_type end = header_lines_.size();
    for (HeaderLines::size_type i = 0; i < end; ++i) {
      const HeaderLineDescription& line = header_lines_[i];
      if (line.skip) {
        continue;
      }
      const char* line_ptr = GetPtr(line.buffer_base_idx);
      WriteHeaderLineToBuffer(
          buffer,
          absl::string_view(line_ptr + line.first_char_idx, line.KeyLength()),
          absl::string_view(line_ptr + line.value_begin_idx,
                            line.ValuesLength()),
          case_option);
    }
  } else {
    WriteToBufferCoalescingMultivaluedHeaders(
        buffer, multivalued_envoy_headers(), case_option);
  }
}

inline void BalsaHeaders::GetValuesOfMultivaluedHeaders(
    const MultivaluedHeadersSet& multivalued_headers,
    MultivaluedHeadersValuesMap* multivalues) const {
  multivalues->reserve(header_lines_.capacity());

  // Find lines that need to be coalesced and store them in |multivalues|.
  for (const auto& line : header_lines_) {
    if (line.skip) {
      continue;
    }
    const char* line_ptr = GetPtr(line.buffer_base_idx);
    absl::string_view header_key =
        absl::string_view(line_ptr + line.first_char_idx, line.KeyLength());
    // If this is multivalued header, it may need to be coalesced.
    if (multivalued_headers.contains(header_key)) {
      absl::string_view header_value = absl::string_view(
          line_ptr + line.value_begin_idx, line.ValuesLength());
      // Add |header_value| to the vector of values for this |header_key|,
      // therefore preserving the order of values for the same key.
      (*multivalues)[header_key].push_back(header_value);
    }
  }
}

template <typename Buffer>
void BalsaHeaders::WriteToBufferCoalescingMultivaluedHeaders(
    Buffer* buffer, const MultivaluedHeadersSet& multivalued_headers,
    CaseOption case_option) const {
  MultivaluedHeadersValuesMap multivalues;
  GetValuesOfMultivaluedHeaders(multivalued_headers, &multivalues);

  // Write out header lines while coalescing those that need to be coalesced.
  for (const auto& line : header_lines_) {
    if (line.skip) {
      continue;
    }
    const char* line_ptr = GetPtr(line.buffer_base_idx);
    absl::string_view header_key =
        absl::string_view(line_ptr + line.first_char_idx, line.KeyLength());
    auto header_multivalue = multivalues.find(header_key);
    // If current line doesn't need to be coalesced (as it is either not
    // multivalue, or has just a single value so it equals to current line),
    // then just write it out.
    if (header_multivalue == multivalues.end() ||
        header_multivalue->second.size() == 1) {
      WriteHeaderLineToBuffer(buffer, header_key,
                              absl::string_view(line_ptr + line.value_begin_idx,
                                                line.ValuesLength()),
                              case_option);
    } else {
      // If this line needs to be coalesced, then write all its values and clear
      // them, so the subsequent same header keys will not be written.
      if (!header_multivalue->second.empty()) {
        WriteHeaderLineValuesToBuffer(buffer, header_key,
                                      header_multivalue->second, case_option);
        // Clear the multivalue list as it is already written out, so subsequent
        // same header keys will not be written.
        header_multivalue->second.clear();
      }
    }
  }
}

template <typename IteratorType>
const IteratorType BalsaHeaders::HeaderLinesBeginHelper() const {
  if (header_lines_.empty()) {
    return IteratorType(this, 0);
  }
  const HeaderLines::size_type header_lines_size = header_lines_.size();
  for (HeaderLines::size_type i = 0; i < header_lines_size; ++i) {
    if (header_lines_[i].skip == false) {
      return IteratorType(this, i);
    }
  }
  return IteratorType(this, 0);
}

template <typename IteratorType>
const IteratorType BalsaHeaders::HeaderLinesEndHelper() const {
  if (header_lines_.empty()) {
    return IteratorType(this, 0);
  }
  const HeaderLines::size_type header_lines_size = header_lines_.size();
  HeaderLines::size_type i = header_lines_size;
  do {
    --i;
    if (header_lines_[i].skip == false) {
      return IteratorType(this, i + 1);
    }
  } while (i != 0);
  return IteratorType(this, 0);
}

}  // namespace quiche

#endif  // QUICHE_BALSA_BALSA_HEADERS_H_
