// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http2/hpack/decoder/hpack_entry_decoder.h"

#include <stddef.h>

#include "base/logging.h"
#include "base/macros.h"
#include "net/http2/tools/http2_bug_tracker.h"

namespace net {
namespace {
// Converts calls from HpackStringDecoder when decoding a header name into the
// appropriate HpackEntryDecoderListener::OnName* calls.
class NameDecoderListener {
 public:
  explicit NameDecoderListener(HpackEntryDecoderListener* listener)
      : listener_(listener) {}
  bool OnStringStart(bool huffman_encoded, size_t len) {
    listener_->OnNameStart(huffman_encoded, len);
    return true;
  }
  void OnStringData(const char* data, size_t len) {
    listener_->OnNameData(data, len);
  }
  void OnStringEnd() { listener_->OnNameEnd(); }

 private:
  HpackEntryDecoderListener* listener_;
};

// Converts calls from HpackStringDecoder when decoding a header value into
// the appropriate HpackEntryDecoderListener::OnValue* calls.
class ValueDecoderListener {
 public:
  explicit ValueDecoderListener(HpackEntryDecoderListener* listener)
      : listener_(listener) {}
  bool OnStringStart(bool huffman_encoded, size_t len) {
    listener_->OnValueStart(huffman_encoded, len);
    return true;
  }
  void OnStringData(const char* data, size_t len) {
    listener_->OnValueData(data, len);
  }
  void OnStringEnd() { listener_->OnValueEnd(); }

 private:
  HpackEntryDecoderListener* listener_;
};
}  // namespace

DecodeStatus HpackEntryDecoder::Start(DecodeBuffer* db,
                                      HpackEntryDecoderListener* listener) {
  DCHECK(db != nullptr);
  DCHECK(listener != nullptr);
  DCHECK(db->HasData());
  DecodeStatus status = entry_type_decoder_.Start(db);
  switch (status) {
    case DecodeStatus::kDecodeDone:
      // The type of the entry and its varint fit into the current decode
      // buffer.
      if (entry_type_decoder_.entry_type() == HpackEntryType::kIndexedHeader) {
        // The entry consists solely of the entry type and varint.
        // This is by far the most common case in practice.
        listener->OnIndexedHeader(entry_type_decoder_.varint());
        return DecodeStatus::kDecodeDone;
      }
      state_ = EntryDecoderState::kDecodedType;
      return Resume(db, listener);
    case DecodeStatus::kDecodeInProgress:
      // Hit the end of the decode buffer before fully decoding
      // the entry type and varint.
      DCHECK_EQ(0u, db->Remaining());
      state_ = EntryDecoderState::kResumeDecodingType;
      return status;
    case DecodeStatus::kDecodeError:
      // The varint must have been invalid (too long).
      return status;
  }

  HTTP2_BUG << "Unreachable";
  return DecodeStatus::kDecodeError;
}

DecodeStatus HpackEntryDecoder::Resume(DecodeBuffer* db,
                                       HpackEntryDecoderListener* listener) {
  DCHECK(db != nullptr);
  DCHECK(listener != nullptr);

  DecodeStatus status;

  do {
    switch (state_) {
      case EntryDecoderState::kResumeDecodingType:
        // entry_type_decoder_ returned kDecodeInProgress when last called.
        DVLOG(1) << "kResumeDecodingType: db->Remaining=" << db->Remaining();
        status = entry_type_decoder_.Resume(db);
        if (status != DecodeStatus::kDecodeDone) {
          return status;
        }
        state_ = EntryDecoderState::kDecodedType;
      // FALLTHROUGH_INTENDED

      case EntryDecoderState::kDecodedType:
        // entry_type_decoder_ returned kDecodeDone, now need to decide how
        // to proceed.
        DVLOG(1) << "kDecodedType: db->Remaining=" << db->Remaining();
        if (DispatchOnType(listener)) {
          // All done.
          return DecodeStatus::kDecodeDone;
        }
        continue;

      case EntryDecoderState::kStartDecodingName:
        DVLOG(1) << "kStartDecodingName: db->Remaining=" << db->Remaining();
        {
          NameDecoderListener ncb(listener);
          status = string_decoder_.Start(db, &ncb);
        }
        if (status != DecodeStatus::kDecodeDone) {
          // On the assumption that the status is kDecodeInProgress, set
          // state_ accordingly; unnecessary if status is kDecodeError, but
          // that will only happen if the varint encoding the name's length
          // is too long.
          state_ = EntryDecoderState::kResumeDecodingName;
          return status;
        }
        state_ = EntryDecoderState::kStartDecodingValue;
      // FALLTHROUGH_INTENDED

      case EntryDecoderState::kStartDecodingValue:
        DVLOG(1) << "kStartDecodingValue: db->Remaining=" << db->Remaining();
        {
          ValueDecoderListener vcb(listener);
          status = string_decoder_.Start(db, &vcb);
        }
        if (status == DecodeStatus::kDecodeDone) {
          // Done with decoding the literal value, so we've reached the
          // end of the header entry.
          return status;
        }
        // On the assumption that the status is kDecodeInProgress, set
        // state_ accordingly; unnecessary if status is kDecodeError, but
        // that will only happen if the varint encoding the value's length
        // is too long.
        state_ = EntryDecoderState::kResumeDecodingValue;
        return status;

      case EntryDecoderState::kResumeDecodingName:
        // The literal name was split across decode buffers.
        DVLOG(1) << "kResumeDecodingName: db->Remaining=" << db->Remaining();
        {
          NameDecoderListener ncb(listener);
          status = string_decoder_.Resume(db, &ncb);
        }
        if (status != DecodeStatus::kDecodeDone) {
          // On the assumption that the status is kDecodeInProgress, set
          // state_ accordingly; unnecessary if status is kDecodeError, but
          // that will only happen if the varint encoding the name's length
          // is too long.
          state_ = EntryDecoderState::kResumeDecodingName;
          return status;
        }
        state_ = EntryDecoderState::kStartDecodingValue;
        break;

      case EntryDecoderState::kResumeDecodingValue:
        // The literal value was split across decode buffers.
        DVLOG(1) << "kResumeDecodingValue: db->Remaining=" << db->Remaining();
        {
          ValueDecoderListener vcb(listener);
          status = string_decoder_.Resume(db, &vcb);
        }
        if (status == DecodeStatus::kDecodeDone) {
          // Done with decoding the value, therefore the entry as a whole.
          return status;
        }
        // On the assumption that the status is kDecodeInProgress, set
        // state_ accordingly; unnecessary if status is kDecodeError, but
        // that will only happen if the varint encoding the value's length
        // is too long.
        state_ = EntryDecoderState::kResumeDecodingValue;
        return status;
    }
  } while (true);
}

bool HpackEntryDecoder::DispatchOnType(HpackEntryDecoderListener* listener) {
  const HpackEntryType entry_type = entry_type_decoder_.entry_type();
  const uint32_t varint = entry_type_decoder_.varint();
  switch (entry_type) {
    case HpackEntryType::kIndexedHeader:
      // The entry consists solely of the entry type and varint. See:
      // http://httpwg.org/specs/rfc7541.html#indexed.header.representation
      listener->OnIndexedHeader(varint);
      return true;

    case HpackEntryType::kIndexedLiteralHeader:
    case HpackEntryType::kUnindexedLiteralHeader:
    case HpackEntryType::kNeverIndexedLiteralHeader:
      // The entry has a literal value, and if the varint is zero also has a
      // literal name preceding the value. See:
      // http://httpwg.org/specs/rfc7541.html#literal.header.representation
      listener->OnStartLiteralHeader(entry_type, varint);
      if (varint == 0) {
        state_ = EntryDecoderState::kStartDecodingName;
      } else {
        state_ = EntryDecoderState::kStartDecodingValue;
      }
      return false;

    case HpackEntryType::kDynamicTableSizeUpdate:
      // The entry consists solely of the entry type and varint. FWIW, I've
      // never seen this type of entry in production (primarily browser
      // traffic) so if you're designing an HPACK successor someday, consider
      // dropping it or giving it a much longer prefix. See:
      // http://httpwg.org/specs/rfc7541.html#encoding.context.update
      listener->OnDynamicTableSizeUpdate(varint);
      return true;
  }

  HTTP2_BUG << "Unreachable, entry_type=" << entry_type;
  return true;
}

void HpackEntryDecoder::OutputDebugString(std::ostream& out) const {
  out << "HpackEntryDecoder(state=" << state_ << ", " << entry_type_decoder_
      << ", " << string_decoder_ << ")";
}

Http2String HpackEntryDecoder::DebugString() const {
  std::stringstream s;
  s << *this;
  return s.str();
}

std::ostream& operator<<(std::ostream& out, const HpackEntryDecoder& v) {
  v.OutputDebugString(out);
  return out;
}

std::ostream& operator<<(std::ostream& out,
                         HpackEntryDecoder::EntryDecoderState state) {
  typedef HpackEntryDecoder::EntryDecoderState EntryDecoderState;
  switch (state) {
    case EntryDecoderState::kResumeDecodingType:
      return out << "kResumeDecodingType";
    case EntryDecoderState::kDecodedType:
      return out << "kDecodedType";
    case EntryDecoderState::kStartDecodingName:
      return out << "kStartDecodingName";
    case EntryDecoderState::kResumeDecodingName:
      return out << "kResumeDecodingName";
    case EntryDecoderState::kStartDecodingValue:
      return out << "kStartDecodingValue";
    case EntryDecoderState::kResumeDecodingValue:
      return out << "kResumeDecodingValue";
  }
  return out << static_cast<int>(state);
}

}  // namespace net
