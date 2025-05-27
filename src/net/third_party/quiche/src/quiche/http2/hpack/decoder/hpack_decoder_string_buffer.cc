// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/http2/hpack/decoder/hpack_decoder_string_buffer.h"

#include <ostream>
#include <string>
#include <utility>

#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace http2 {

std::ostream& operator<<(std::ostream& out,
                         const HpackDecoderStringBuffer::State v) {
  switch (v) {
    case HpackDecoderStringBuffer::State::RESET:
      return out << "RESET";
    case HpackDecoderStringBuffer::State::COLLECTING:
      return out << "COLLECTING";
    case HpackDecoderStringBuffer::State::COMPLETE:
      return out << "COMPLETE";
  }
  // Since the value doesn't come over the wire, only a programming bug should
  // result in reaching this point.
  int unknown = static_cast<int>(v);
  QUICHE_BUG(http2_bug_50_1)
      << "Invalid HpackDecoderStringBuffer::State: " << unknown;
  return out << "HpackDecoderStringBuffer::State(" << unknown << ")";
}

std::ostream& operator<<(std::ostream& out,
                         const HpackDecoderStringBuffer::Backing v) {
  switch (v) {
    case HpackDecoderStringBuffer::Backing::RESET:
      return out << "RESET";
    case HpackDecoderStringBuffer::Backing::UNBUFFERED:
      return out << "UNBUFFERED";
    case HpackDecoderStringBuffer::Backing::BUFFERED:
      return out << "BUFFERED";
  }
  // Since the value doesn't come over the wire, only a programming bug should
  // result in reaching this point.
  auto v2 = static_cast<int>(v);
  QUICHE_BUG(http2_bug_50_2)
      << "Invalid HpackDecoderStringBuffer::Backing: " << v2;
  return out << "HpackDecoderStringBuffer::Backing(" << v2 << ")";
}

HpackDecoderStringBuffer::HpackDecoderStringBuffer()
    : remaining_len_(0),
      is_huffman_encoded_(false),
      state_(State::RESET),
      backing_(Backing::RESET) {}
HpackDecoderStringBuffer::~HpackDecoderStringBuffer() = default;

void HpackDecoderStringBuffer::Reset() {
  QUICHE_DVLOG(3) << "HpackDecoderStringBuffer::Reset";
  state_ = State::RESET;
}

void HpackDecoderStringBuffer::OnStart(bool huffman_encoded, size_t len) {
  QUICHE_DVLOG(2) << "HpackDecoderStringBuffer::OnStart";
  QUICHE_DCHECK_EQ(state_, State::RESET);

  remaining_len_ = len;
  is_huffman_encoded_ = huffman_encoded;
  state_ = State::COLLECTING;

  if (huffman_encoded) {
    // We don't set, clear or use value_ for buffered strings until OnEnd.
    decoder_.Reset();
    buffer_.clear();
    backing_ = Backing::BUFFERED;

    // Reserve space in buffer_ for the uncompressed string, assuming the
    // maximum expansion. The shortest Huffman codes in the RFC are 5 bits long,
    // which then expand to 8 bits during decoding (i.e. each code is for one
    // plain text octet, aka byte), so the maximum size is 60% longer than the
    // encoded size.
    len = len * 8 / 5;
    if (buffer_.capacity() < len) {
      buffer_.reserve(len);
    }
  } else {
    // Assume for now that we won't need to use buffer_, so don't reserve space
    // in it.
    backing_ = Backing::RESET;
    // OnData is not called for empty (zero length) strings, so make sure that
    // value_ is cleared.
    value_ = absl::string_view();
  }
}

bool HpackDecoderStringBuffer::OnData(const char* data, size_t len) {
  QUICHE_DVLOG(2) << "HpackDecoderStringBuffer::OnData state=" << state_
                  << ", backing=" << backing_;
  QUICHE_DCHECK_EQ(state_, State::COLLECTING);
  QUICHE_DCHECK_LE(len, remaining_len_);
  remaining_len_ -= len;

  if (is_huffman_encoded_) {
    QUICHE_DCHECK_EQ(backing_, Backing::BUFFERED);
    return decoder_.Decode(absl::string_view(data, len), &buffer_);
  }

  if (backing_ == Backing::RESET) {
    // This is the first call to OnData. If data contains the entire string,
    // don't copy the string. If we later find that the HPACK entry is split
    // across input buffers, then we'll copy the string into buffer_.
    if (remaining_len_ == 0) {
      value_ = absl::string_view(data, len);
      backing_ = Backing::UNBUFFERED;
      return true;
    }

    // We need to buffer the string because it is split across input buffers.
    // Reserve space in buffer_ for the entire string.
    backing_ = Backing::BUFFERED;
    buffer_.reserve(remaining_len_ + len);
    buffer_.assign(data, len);
    return true;
  }

  // This is not the first call to OnData for this string, so it should be
  // buffered.
  QUICHE_DCHECK_EQ(backing_, Backing::BUFFERED);

  // Append to the current contents of the buffer.
  buffer_.append(data, len);
  return true;
}

bool HpackDecoderStringBuffer::OnEnd() {
  QUICHE_DVLOG(2) << "HpackDecoderStringBuffer::OnEnd";
  QUICHE_DCHECK_EQ(state_, State::COLLECTING);
  QUICHE_DCHECK_EQ(0u, remaining_len_);

  if (is_huffman_encoded_) {
    QUICHE_DCHECK_EQ(backing_, Backing::BUFFERED);
    // Did the Huffman encoding of the string end properly?
    if (!decoder_.InputProperlyTerminated()) {
      return false;  // No, it didn't.
    }
    value_ = buffer_;
  } else if (backing_ == Backing::BUFFERED) {
    value_ = buffer_;
  }
  state_ = State::COMPLETE;
  return true;
}

void HpackDecoderStringBuffer::BufferStringIfUnbuffered() {
  QUICHE_DVLOG(3) << "HpackDecoderStringBuffer::BufferStringIfUnbuffered state="
                  << state_ << ", backing=" << backing_;
  if (state_ != State::RESET && backing_ == Backing::UNBUFFERED) {
    QUICHE_DVLOG(2)
        << "HpackDecoderStringBuffer buffering std::string of length "
        << value_.size();
    buffer_.assign(value_.data(), value_.size());
    if (state_ == State::COMPLETE) {
      value_ = buffer_;
    }
    backing_ = Backing::BUFFERED;
  }
}

bool HpackDecoderStringBuffer::IsBuffered() const {
  QUICHE_DVLOG(3) << "HpackDecoderStringBuffer::IsBuffered";
  return state_ != State::RESET && backing_ == Backing::BUFFERED;
}

size_t HpackDecoderStringBuffer::BufferedLength() const {
  QUICHE_DVLOG(3) << "HpackDecoderStringBuffer::BufferedLength";
  return IsBuffered() ? buffer_.size() : 0;
}

absl::string_view HpackDecoderStringBuffer::str() const {
  QUICHE_DVLOG(3) << "HpackDecoderStringBuffer::str";
  QUICHE_DCHECK_EQ(state_, State::COMPLETE);
  return value_;
}

absl::string_view HpackDecoderStringBuffer::GetStringIfComplete() const {
  if (state_ != State::COMPLETE) {
    return {};
  }
  return str();
}

std::string HpackDecoderStringBuffer::ReleaseString() {
  QUICHE_DVLOG(3) << "HpackDecoderStringBuffer::ReleaseString";
  QUICHE_DCHECK_EQ(state_, State::COMPLETE);
  QUICHE_DCHECK_EQ(backing_, Backing::BUFFERED);
  if (state_ == State::COMPLETE) {
    state_ = State::RESET;
    if (backing_ == Backing::BUFFERED) {
      return std::move(buffer_);
    } else {
      return std::string(value_);
    }
  }
  return "";
}

void HpackDecoderStringBuffer::OutputDebugStringTo(std::ostream& out) const {
  out << "{state=" << state_;
  if (state_ != State::RESET) {
    out << ", backing=" << backing_;
    out << ", remaining_len=" << remaining_len_;
    out << ", is_huffman_encoded=" << is_huffman_encoded_;
    if (backing_ == Backing::BUFFERED) {
      out << ", buffer: " << buffer_;
    } else {
      out << ", value: " << value_;
    }
  }
  out << "}";
}

std::ostream& operator<<(std::ostream& out, const HpackDecoderStringBuffer& v) {
  v.OutputDebugStringTo(out);
  return out;
}

}  // namespace http2
