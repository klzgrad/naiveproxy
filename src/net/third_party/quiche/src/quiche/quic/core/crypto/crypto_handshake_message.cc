// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/crypto/crypto_handshake_message.h"

#include <memory>
#include <string>

#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/crypto_framer.h"
#include "quiche/quic/core/crypto/crypto_protocol.h"
#include "quiche/quic/core/crypto/crypto_utils.h"
#include "quiche/quic/core/quic_socket_address_coder.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/common/quiche_endian.h"

namespace quic {

CryptoHandshakeMessage::CryptoHandshakeMessage() : tag_(0), minimum_size_(0) {}

CryptoHandshakeMessage::CryptoHandshakeMessage(
    const CryptoHandshakeMessage& other)
    : tag_(other.tag_),
      tag_value_map_(other.tag_value_map_),
      minimum_size_(other.minimum_size_) {
  // Don't copy serialized_. unique_ptr doesn't have a copy constructor.
  // The new object can lazily reconstruct serialized_.
}

CryptoHandshakeMessage::CryptoHandshakeMessage(CryptoHandshakeMessage&& other) =
    default;

CryptoHandshakeMessage::~CryptoHandshakeMessage() {}

CryptoHandshakeMessage& CryptoHandshakeMessage::operator=(
    const CryptoHandshakeMessage& other) {
  tag_ = other.tag_;
  tag_value_map_ = other.tag_value_map_;
  // Don't copy serialized_. unique_ptr doesn't have an assignment operator.
  // However, invalidate serialized_.
  serialized_.reset();
  minimum_size_ = other.minimum_size_;
  return *this;
}

CryptoHandshakeMessage& CryptoHandshakeMessage::operator=(
    CryptoHandshakeMessage&& other) = default;

bool CryptoHandshakeMessage::operator==(
    const CryptoHandshakeMessage& rhs) const {
  return tag_ == rhs.tag_ && tag_value_map_ == rhs.tag_value_map_ &&
         minimum_size_ == rhs.minimum_size_;
}

bool CryptoHandshakeMessage::operator!=(
    const CryptoHandshakeMessage& rhs) const {
  return !(*this == rhs);
}

void CryptoHandshakeMessage::Clear() {
  tag_ = 0;
  tag_value_map_.clear();
  minimum_size_ = 0;
  serialized_.reset();
}

const QuicData& CryptoHandshakeMessage::GetSerialized() const {
  if (!serialized_) {
    serialized_ = CryptoFramer::ConstructHandshakeMessage(*this);
  }
  return *serialized_;
}

void CryptoHandshakeMessage::MarkDirty() { serialized_.reset(); }

void CryptoHandshakeMessage::SetVersionVector(
    QuicTag tag, ParsedQuicVersionVector versions) {
  QuicVersionLabelVector version_labels;
  for (const ParsedQuicVersion& version : versions) {
    version_labels.push_back(
        quiche::QuicheEndian::HostToNet32(CreateQuicVersionLabel(version)));
  }
  SetVector(tag, version_labels);
}

void CryptoHandshakeMessage::SetVersion(QuicTag tag,
                                        ParsedQuicVersion version) {
  SetValue(tag,
           quiche::QuicheEndian::HostToNet32(CreateQuicVersionLabel(version)));
}

void CryptoHandshakeMessage::SetStringPiece(QuicTag tag,
                                            absl::string_view value) {
  tag_value_map_[tag] = std::string(value);
}

void CryptoHandshakeMessage::Erase(QuicTag tag) { tag_value_map_.erase(tag); }

QuicErrorCode CryptoHandshakeMessage::GetTaglist(
    QuicTag tag, QuicTagVector* out_tags) const {
  auto it = tag_value_map_.find(tag);
  QuicErrorCode ret = QUIC_NO_ERROR;

  if (it == tag_value_map_.end()) {
    ret = QUIC_CRYPTO_MESSAGE_PARAMETER_NOT_FOUND;
  } else if (it->second.size() % sizeof(QuicTag) != 0) {
    ret = QUIC_INVALID_CRYPTO_MESSAGE_PARAMETER;
  }

  if (ret != QUIC_NO_ERROR) {
    out_tags->clear();
    return ret;
  }

  size_t num_tags = it->second.size() / sizeof(QuicTag);
  out_tags->resize(num_tags);
  for (size_t i = 0; i < num_tags; ++i) {
    memcpy(&(*out_tags)[i], it->second.data() + i * sizeof(tag), sizeof(tag));
  }
  return ret;
}

QuicErrorCode CryptoHandshakeMessage::GetVersionLabelList(
    QuicTag tag, QuicVersionLabelVector* out) const {
  QuicErrorCode error = GetTaglist(tag, out);
  if (error != QUIC_NO_ERROR) {
    return error;
  }

  for (size_t i = 0; i < out->size(); ++i) {
    (*out)[i] = quiche::QuicheEndian::HostToNet32((*out)[i]);
  }

  return QUIC_NO_ERROR;
}

QuicErrorCode CryptoHandshakeMessage::GetVersionLabel(
    QuicTag tag, QuicVersionLabel* out) const {
  QuicErrorCode error = GetUint32(tag, out);
  if (error != QUIC_NO_ERROR) {
    return error;
  }

  *out = quiche::QuicheEndian::HostToNet32(*out);
  return QUIC_NO_ERROR;
}

bool CryptoHandshakeMessage::GetStringPiece(QuicTag tag,
                                            absl::string_view* out) const {
  auto it = tag_value_map_.find(tag);
  if (it == tag_value_map_.end()) {
    return false;
  }
  *out = it->second;
  return true;
}

bool CryptoHandshakeMessage::HasStringPiece(QuicTag tag) const {
  return tag_value_map_.find(tag) != tag_value_map_.end();
}

QuicErrorCode CryptoHandshakeMessage::GetNthValue24(
    QuicTag tag, unsigned index, absl::string_view* out) const {
  absl::string_view value;
  if (!GetStringPiece(tag, &value)) {
    return QUIC_CRYPTO_MESSAGE_PARAMETER_NOT_FOUND;
  }

  for (unsigned i = 0;; i++) {
    if (value.empty()) {
      return QUIC_CRYPTO_MESSAGE_INDEX_NOT_FOUND;
    }
    if (value.size() < 3) {
      return QUIC_INVALID_CRYPTO_MESSAGE_PARAMETER;
    }

    const unsigned char* data =
        reinterpret_cast<const unsigned char*>(value.data());
    size_t size = static_cast<size_t>(data[0]) |
                  (static_cast<size_t>(data[1]) << 8) |
                  (static_cast<size_t>(data[2]) << 16);
    value.remove_prefix(3);

    if (value.size() < size) {
      return QUIC_INVALID_CRYPTO_MESSAGE_PARAMETER;
    }

    if (i == index) {
      *out = absl::string_view(value.data(), size);
      return QUIC_NO_ERROR;
    }

    value.remove_prefix(size);
  }
}

QuicErrorCode CryptoHandshakeMessage::GetUint32(QuicTag tag,
                                                uint32_t* out) const {
  return GetPOD(tag, out, sizeof(uint32_t));
}

QuicErrorCode CryptoHandshakeMessage::GetUint64(QuicTag tag,
                                                uint64_t* out) const {
  return GetPOD(tag, out, sizeof(uint64_t));
}

QuicErrorCode CryptoHandshakeMessage::GetStatelessResetToken(
    QuicTag tag, StatelessResetToken* out) const {
  return GetPOD(tag, out, kStatelessResetTokenLength);
}

size_t CryptoHandshakeMessage::size() const {
  size_t ret = sizeof(QuicTag) + sizeof(uint16_t) /* number of entries */ +
               sizeof(uint16_t) /* padding */;
  ret += (sizeof(QuicTag) + sizeof(uint32_t) /* end offset */) *
         tag_value_map_.size();
  for (auto i = tag_value_map_.begin(); i != tag_value_map_.end(); ++i) {
    ret += i->second.size();
  }

  return ret;
}

void CryptoHandshakeMessage::set_minimum_size(size_t min_bytes) {
  if (min_bytes == minimum_size_) {
    return;
  }
  serialized_.reset();
  minimum_size_ = min_bytes;
}

size_t CryptoHandshakeMessage::minimum_size() const { return minimum_size_; }

std::string CryptoHandshakeMessage::DebugString() const {
  return DebugStringInternal(0);
}

QuicErrorCode CryptoHandshakeMessage::GetPOD(QuicTag tag, void* out,
                                             size_t len) const {
  auto it = tag_value_map_.find(tag);
  QuicErrorCode ret = QUIC_NO_ERROR;

  if (it == tag_value_map_.end()) {
    ret = QUIC_CRYPTO_MESSAGE_PARAMETER_NOT_FOUND;
  } else if (it->second.size() != len) {
    ret = QUIC_INVALID_CRYPTO_MESSAGE_PARAMETER;
  }

  if (ret != QUIC_NO_ERROR) {
    memset(out, 0, len);
    return ret;
  }

  memcpy(out, it->second.data(), len);
  return ret;
}

std::string CryptoHandshakeMessage::DebugStringInternal(size_t indent) const {
  std::string ret =
      std::string(2 * indent, ' ') + QuicTagToString(tag_) + "<\n";
  ++indent;
  for (auto it = tag_value_map_.begin(); it != tag_value_map_.end(); ++it) {
    ret += std::string(2 * indent, ' ') + QuicTagToString(it->first) + ": ";

    bool done = false;
    switch (it->first) {
      case kICSL:
      case kCFCW:
      case kSFCW:
      case kIRTT:
      case kMIUS:
      case kMIDS:
      case kTCID:
      case kMAD:
        // uint32_t value
        if (it->second.size() == 4) {
          uint32_t value;
          memcpy(&value, it->second.data(), sizeof(value));
          absl::StrAppend(&ret, value);
          done = true;
        }
        break;
      case kKEXS:
      case kAEAD:
      case kCOPT:
      case kPDMD:
      case kVER:
        // tag lists
        if (it->second.size() % sizeof(QuicTag) == 0) {
          for (size_t j = 0; j < it->second.size(); j += sizeof(QuicTag)) {
            QuicTag tag;
            memcpy(&tag, it->second.data() + j, sizeof(tag));
            if (j > 0) {
              ret += ",";
            }
            ret += "'" + QuicTagToString(tag) + "'";
          }
          done = true;
        }
        break;
      case kRREJ:
        // uint32_t lists
        if (it->second.size() % sizeof(uint32_t) == 0) {
          for (size_t j = 0; j < it->second.size(); j += sizeof(uint32_t)) {
            uint32_t value;
            memcpy(&value, it->second.data() + j, sizeof(value));
            if (j > 0) {
              ret += ",";
            }
            ret += CryptoUtils::HandshakeFailureReasonToString(
                static_cast<HandshakeFailureReason>(value));
          }
          done = true;
        }
        break;
      case kCADR:
        // IP address and port
        if (!it->second.empty()) {
          QuicSocketAddressCoder decoder;
          if (decoder.Decode(it->second.data(), it->second.size())) {
            ret += QuicSocketAddress(decoder.ip(), decoder.port()).ToString();
            done = true;
          }
        }
        break;
      case kSCFG:
        // nested messages.
        if (!it->second.empty()) {
          std::unique_ptr<CryptoHandshakeMessage> msg(
              CryptoFramer::ParseMessage(it->second));
          if (msg) {
            ret += "\n";
            ret += msg->DebugStringInternal(indent + 1);

            done = true;
          }
        }
        break;
      case kPAD:
        ret += absl::StrFormat("(%d bytes of padding)", it->second.size());
        done = true;
        break;
      case kSNI:
      case kUAID:
        ret += "\"" + it->second + "\"";
        done = true;
        break;
    }

    if (!done) {
      // If there's no specific format for this tag, or the value is invalid,
      // then just use hex.
      ret += "0x" + absl::BytesToHexString(it->second);
    }
    ret += "\n";
  }
  --indent;
  ret += std::string(2 * indent, ' ') + ">";
  return ret;
}

}  // namespace quic
