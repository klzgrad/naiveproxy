// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_HPACK_DECODER_HPACK_ENTRY_DECODER_LISTENER_H_
#define QUICHE_HTTP2_HPACK_DECODER_HPACK_ENTRY_DECODER_LISTENER_H_

// Defines HpackEntryDecoderListener, the base class of listeners that
// HpackEntryDecoder calls. Also defines HpackEntryDecoderVLoggingListener
// which logs before calling another HpackEntryDecoderListener implementation.

#include <stddef.h>

#include "net/third_party/quiche/src/http2/hpack/http2_hpack_constants.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_export.h"

namespace http2 {

class QUICHE_EXPORT_PRIVATE HpackEntryDecoderListener {
 public:
  virtual ~HpackEntryDecoderListener() {}

  // Called when an indexed header (i.e. one in the static or dynamic table) has
  // been decoded from an HPACK block. index is supposed to be non-zero, but
  // that has not been checked by the caller.
  virtual void OnIndexedHeader(size_t index) = 0;

  // Called when the start of a header with a literal value, and maybe a literal
  // name, has been decoded. maybe_name_index is zero if the header has a
  // literal name, else it is a reference into the static or dynamic table, from
  // which the name should be determined. When the name is literal, the next
  // call will be to OnNameStart; else it will be to OnValueStart. entry_type
  // indicates whether the peer has added the entry to its dynamic table, and
  // whether a proxy is permitted to do so when forwarding the entry.
  virtual void OnStartLiteralHeader(HpackEntryType entry_type,
                                    size_t maybe_name_index) = 0;

  // Called when the encoding (Huffman compressed or plain text) and the encoded
  // length of a literal name has been decoded. OnNameData will be called next,
  // and repeatedly until the sum of lengths passed to OnNameData is len.
  virtual void OnNameStart(bool huffman_encoded, size_t len) = 0;

  // Called when len bytes of an encoded header name have been decoded.
  virtual void OnNameData(const char* data, size_t len) = 0;

  // Called after the entire name has been passed to OnNameData.
  // OnValueStart will be called next.
  virtual void OnNameEnd() = 0;

  // Called when the encoding (Huffman compressed or plain text) and the encoded
  // length of a literal value has been decoded. OnValueData will be called
  // next, and repeatedly until the sum of lengths passed to OnValueData is len.
  virtual void OnValueStart(bool huffman_encoded, size_t len) = 0;

  // Called when len bytes of an encoded header value have been decoded.
  virtual void OnValueData(const char* data, size_t len) = 0;

  // Called after the entire value has been passed to OnValueData, marking the
  // end of a header entry with a literal value, and maybe a literal name.
  virtual void OnValueEnd() = 0;

  // Called when an update to the size of the peer's dynamic table has been
  // decoded.
  virtual void OnDynamicTableSizeUpdate(size_t size) = 0;
};

class QUICHE_EXPORT_PRIVATE HpackEntryDecoderVLoggingListener
    : public HpackEntryDecoderListener {
 public:
  HpackEntryDecoderVLoggingListener() : wrapped_(nullptr) {}
  explicit HpackEntryDecoderVLoggingListener(HpackEntryDecoderListener* wrapped)
      : wrapped_(wrapped) {}
  ~HpackEntryDecoderVLoggingListener() override {}

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
  HpackEntryDecoderListener* const wrapped_;
};

// A no-op implementation of HpackEntryDecoderListener.
class QUICHE_EXPORT_PRIVATE HpackEntryDecoderNoOpListener
    : public HpackEntryDecoderListener {
 public:
  ~HpackEntryDecoderNoOpListener() override {}

  void OnIndexedHeader(size_t /*index*/) override {}
  void OnStartLiteralHeader(HpackEntryType /*entry_type*/,
                            size_t /*maybe_name_index*/) override {}
  void OnNameStart(bool /*huffman_encoded*/, size_t /*len*/) override {}
  void OnNameData(const char* /*data*/, size_t /*len*/) override {}
  void OnNameEnd() override {}
  void OnValueStart(bool /*huffman_encoded*/, size_t /*len*/) override {}
  void OnValueData(const char* /*data*/, size_t /*len*/) override {}
  void OnValueEnd() override {}
  void OnDynamicTableSizeUpdate(size_t /*size*/) override {}
};

}  // namespace http2

#endif  // QUICHE_HTTP2_HPACK_DECODER_HPACK_ENTRY_DECODER_LISTENER_H_
