// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_CRYPTO_HANDSHAKE_MESSAGE_H_
#define QUICHE_QUIC_CORE_CRYPTO_CRYPTO_HANDSHAKE_MESSAGE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_uint128.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

// An intermediate format of a handshake message that's convenient for a
// CryptoFramer to serialize from or parse into.
class QUIC_EXPORT_PRIVATE CryptoHandshakeMessage {
 public:
  CryptoHandshakeMessage();
  CryptoHandshakeMessage(const CryptoHandshakeMessage& other);
  CryptoHandshakeMessage(CryptoHandshakeMessage&& other);
  ~CryptoHandshakeMessage();

  CryptoHandshakeMessage& operator=(const CryptoHandshakeMessage& other);
  CryptoHandshakeMessage& operator=(CryptoHandshakeMessage&& other);

  // Clears state.
  void Clear();

  // GetSerialized returns the serialized form of this message and caches the
  // result. Subsequently altering the message does not invalidate the cache.
  const QuicData& GetSerialized() const;

  // MarkDirty invalidates the cache created by |GetSerialized|.
  void MarkDirty();

  // SetValue sets an element with the given tag to the raw, memory contents of
  // |v|.
  template <class T>
  void SetValue(QuicTag tag, const T& v) {
    tag_value_map_[tag] =
        std::string(reinterpret_cast<const char*>(&v), sizeof(v));
  }

  // SetVector sets an element with the given tag to the raw contents of an
  // array of elements in |v|.
  template <class T>
  void SetVector(QuicTag tag, const std::vector<T>& v) {
    if (v.empty()) {
      tag_value_map_[tag] = std::string();
    } else {
      tag_value_map_[tag] = std::string(reinterpret_cast<const char*>(&v[0]),
                                        v.size() * sizeof(T));
    }
  }

  // Sets an element with the given tag to the on-the-wire representation of
  // |version|.
  void SetVersion(QuicTag tag, ParsedQuicVersion version);

  // Sets an element with the given tag to the on-the-wire representation of
  // the elements in |versions|.
  void SetVersionVector(QuicTag tag, ParsedQuicVersionVector versions);

  // Returns the message tag.
  QuicTag tag() const { return tag_; }
  // Sets the message tag.
  void set_tag(QuicTag tag) { tag_ = tag; }

  const QuicTagValueMap& tag_value_map() const { return tag_value_map_; }

  void SetStringPiece(QuicTag tag, quiche::QuicheStringPiece value);

  // Erase removes a tag/value, if present, from the message.
  void Erase(QuicTag tag);

  // GetTaglist finds an element with the given tag containing zero or more
  // tags. If such a tag doesn't exist, it returns an error code. Otherwise it
  // populates |out_tags| with the tags and returns QUIC_NO_ERROR.
  QuicErrorCode GetTaglist(QuicTag tag, QuicTagVector* out_tags) const;

  // GetVersionLabelList finds an element with the given tag containing zero or
  // more version labels. If such a tag doesn't exist, it returns an error code.
  // Otherwise it populates |out| with the labels and returns QUIC_NO_ERROR.
  QuicErrorCode GetVersionLabelList(QuicTag tag,
                                    QuicVersionLabelVector* out) const;

  // GetVersionLabel finds an element with the given tag containing a single
  // version label. If such a tag doesn't exist, it returns an error code.
  // Otherwise it populates |out| with the label and returns QUIC_NO_ERROR.
  QuicErrorCode GetVersionLabel(QuicTag tag, QuicVersionLabel* out) const;

  bool GetStringPiece(QuicTag tag, quiche::QuicheStringPiece* out) const;
  bool HasStringPiece(QuicTag tag) const;

  // GetNthValue24 interprets the value with the given tag to be a series of
  // 24-bit, length prefixed values and it returns the subvalue with the given
  // index.
  QuicErrorCode GetNthValue24(QuicTag tag,
                              unsigned index,
                              quiche::QuicheStringPiece* out) const;
  QuicErrorCode GetUint32(QuicTag tag, uint32_t* out) const;
  QuicErrorCode GetUint64(QuicTag tag, uint64_t* out) const;
  QuicErrorCode GetUint128(QuicTag tag, QuicUint128* out) const;

  // size returns 4 (message tag) + 2 (uint16_t, number of entries) +
  // (4 (tag) + 4 (end offset))*tag_value_map_.size() + ∑ value sizes.
  size_t size() const;

  // set_minimum_size sets the minimum number of bytes that the message should
  // consume. The CryptoFramer will add a PAD tag as needed when serializing in
  // order to ensure this. Setting a value of 0 disables padding.
  //
  // Padding is useful in order to ensure that messages are a minimum size. A
  // QUIC server can require a minimum size in order to reduce the
  // amplification factor of any mirror DoS attack.
  void set_minimum_size(size_t min_bytes);

  size_t minimum_size() const;

  // DebugString returns a multi-line, string representation of the message
  // suitable for including in debug output.
  std::string DebugString() const;

 private:
  // GetPOD is a utility function for extracting a plain-old-data value. If
  // |tag| exists in the message, and has a value of exactly |len| bytes then
  // it copies |len| bytes of data into |out|. Otherwise |len| bytes at |out|
  // are zeroed out.
  //
  // If used to copy integers then this assumes that the machine is
  // little-endian.
  QuicErrorCode GetPOD(QuicTag tag, void* out, size_t len) const;

  std::string DebugStringInternal(size_t indent) const;

  QuicTag tag_;
  QuicTagValueMap tag_value_map_;

  size_t minimum_size_;

  // The serialized form of the handshake message. This member is constructed
  // lazily.
  mutable std::unique_ptr<QuicData> serialized_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_CRYPTO_HANDSHAKE_MESSAGE_H_
