// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_HPACK_DECODER_HPACK_WHOLE_ENTRY_BUFFER_H_
#define QUICHE_HTTP2_HPACK_DECODER_HPACK_WHOLE_ENTRY_BUFFER_H_

// HpackWholeEntryBuffer isolates a listener from the fact that an entry may
// be split across multiple input buffers, providing one callback per entry.
// HpackWholeEntryBuffer requires that the HpackEntryDecoderListener be made in
// the correct order, which is tested by hpack_entry_decoder_test.cc.

#include <stddef.h>

#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_decoder_string_buffer.h"
#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_decoding_error.h"
#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_entry_decoder_listener.h"
#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_whole_entry_listener.h"
#include "net/third_party/quiche/src/http2/hpack/http2_hpack_constants.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace http2 {

// TODO(jamessynge): Consider renaming HpackEntryDecoderListener to
// HpackEntryPartsListener or HpackEntryFragmentsListener.
class QUICHE_EXPORT_PRIVATE HpackWholeEntryBuffer
    : public HpackEntryDecoderListener {
 public:
  // max_string_size specifies the maximum size of an on-the-wire string (name
  // or value, plain or Huffman encoded) that will be accepted. See sections
  // 5.1 and 5.2 of RFC 7541. This is a defense against OOM attacks; HTTP/2
  // allows a decoder to enforce any limit of the size of the header lists
  // that it is willing decode, including less than the MAX_HEADER_LIST_SIZE
  // setting, a setting that is initially unlimited. For example, we might
  // choose to send a MAX_HEADER_LIST_SIZE of 64KB, and to use that same value
  // as the upper bound for individual strings.
  HpackWholeEntryBuffer(HpackWholeEntryListener* listener,
                        size_t max_string_size);
  ~HpackWholeEntryBuffer() override;

  HpackWholeEntryBuffer(const HpackWholeEntryBuffer&) = delete;
  HpackWholeEntryBuffer& operator=(const HpackWholeEntryBuffer&) = delete;

  // Set the listener to be notified when a whole entry has been decoded.
  // The listener may be changed at any time.
  void set_listener(HpackWholeEntryListener* listener);

  // Set how much encoded data this decoder is willing to buffer.
  // TODO(jamessynge): Come up with consistent semantics for this protection
  // across the various decoders; e.g. should it be for a single string or
  // a single header entry?
  void set_max_string_size_bytes(size_t max_string_size_bytes);

  // Ensure that decoded strings pointed to by the HpackDecoderStringBuffer
  // instances name_ and value_ are buffered, which allows any underlying
  // transport buffer to be freed or reused without overwriting the decoded
  // strings. This is needed only when an HPACK entry is split across transport
  // buffers. See HpackDecoder::DecodeFragment.
  void BufferStringsIfUnbuffered();

  // Was an error detected? After an error has been detected and reported,
  // no further callbacks will be made to the listener.
  bool error_detected() const { return error_detected_; }

  // Returns the estimate of dynamically allocated memory in bytes.
  size_t EstimateMemoryUsage() const;

  // Implement the HpackEntryDecoderListener methods.

  void OnIndexedHeader(size_t index) override;
  void OnStartLiteralHeader(HpackEntryType entry_type,
                            size_t maybe_name_index) override;
  void OnNameStart(bool huffman_encoded, size_t len) override;
  void OnNameData(const char* data, size_t len) override;
  void OnNameEnd() override;
  void OnValueStart(bool huffman_encoded, size_t len) override;
  void OnValueData(const char* data, size_t len) override;
  void OnValueEnd() override;
  void OnDynamicTableSizeUpdate(size_t size) override;

 private:
  void ReportError(HpackDecodingError error);

  HpackWholeEntryListener* listener_;
  HpackDecoderStringBuffer name_, value_;

  // max_string_size_bytes_ specifies the maximum allowed size of an on-the-wire
  // string. Larger strings will be reported as errors to the listener; the
  // endpoint should treat these as COMPRESSION errors, which are CONNECTION
  // level errors.
  size_t max_string_size_bytes_;

  // The name index (or zero) of the current header entry with a literal value.
  size_t maybe_name_index_;

  // The type of the current header entry (with literals) that is being decoded.
  HpackEntryType entry_type_;

  bool error_detected_ = false;
};

}  // namespace http2

#endif  // QUICHE_HTTP2_HPACK_DECODER_HPACK_WHOLE_ENTRY_BUFFER_H_
