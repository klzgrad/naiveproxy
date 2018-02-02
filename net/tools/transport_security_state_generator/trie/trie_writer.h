// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_TRANSPORT_SECURITY_STATE_GENERATOR_TRIE_TRIE_WRITER_H_
#define NET_TOOLS_TRANSPORT_SECURITY_STATE_GENERATOR_TRIE_TRIE_WRITER_H_

#include <string>
#include <vector>

#include "net/tools/transport_security_state_generator/bit_writer.h"
#include "net/tools/transport_security_state_generator/huffman/huffman_builder.h"
#include "net/tools/transport_security_state_generator/transport_security_state_entry.h"

namespace net {

namespace transport_security_state {

struct TransportSecurityStateEntry;
class TrieBitBuffer;

// Maps a name to an index. This is used to track the index of several values
// in the C++ code. The trie refers to the array index of the values. For
// example; the pinsets are outputted as a C++ array and the index for the
// pinset in that array is placed in the trie.
using NameIDMap = std::map<std::string, uint32_t>;
using NameIDPair = std::pair<std::string, uint32_t>;

class TrieWriter {
 public:
  enum : uint8_t { kTerminalValue = 0, kEndOfTableValue = 127 };

  TrieWriter(const HuffmanRepresentationTable& huffman_table,
             const NameIDMap& expect_ct_report_uri_map,
             const NameIDMap& expect_staple_report_uri_map,
             const NameIDMap& pinsets_map,
             HuffmanBuilder* huffman_builder);
  ~TrieWriter();

  // Constructs a trie containing all |entries|. The output is written to
  // |buffer_| and |*position| is set to the position of the trie root. Returns
  // true on success and false on failure.
  bool WriteEntries(const TransportSecurityStateEntries& entries,
                    uint32_t* position);

  // Returns the position |buffer_| is currently at. The returned value
  // represents the number of bits.
  uint32_t position() const;

  // Flushes |buffer_|.
  void Flush();

  // Returns the trie bytes. Call Flush() first to ensure the buffer is
  // complete.
  const std::vector<uint8_t>& bytes() const { return buffer_.bytes(); }

 private:
  bool WriteDispatchTables(ReversedEntries::iterator start,
                           ReversedEntries::iterator end,
                           uint32_t* position);

  // Serializes |*entry| and writes it to |*writer|.
  bool WriteEntry(const TransportSecurityStateEntry* entry,
                  TrieBitBuffer* writer);

  // Removes the first |length| characters from all entries between |start| and
  // |end|.
  void RemovePrefix(size_t length,
                    ReversedEntries::iterator start,
                    ReversedEntries::iterator end);

  // Searches for the longest common prefix for all entries between |start| and
  // |end|.
  std::vector<uint8_t> LongestCommonPrefix(
      ReversedEntries::const_iterator start,
      ReversedEntries::const_iterator end) const;

  // Returns the reversed |hostname| as a vector of bytes. The reversed hostname
  // will be terminated by |kTerminalValue|.
  std::vector<uint8_t> ReverseName(const std::string& hostname) const;

  BitWriter buffer_;
  const HuffmanRepresentationTable& huffman_table_;
  const NameIDMap& expect_ct_report_uri_map_;
  const NameIDMap& expect_staple_report_uri_map_;
  const NameIDMap& pinsets_map_;
  HuffmanBuilder* huffman_builder_;
};

}  // namespace transport_security_state

}  // namespace net

#endif  // NET_TOOLS_TRANSPORT_SECURITY_STATE_GENERATOR_TRIE_TRIE_WRITER_H_
