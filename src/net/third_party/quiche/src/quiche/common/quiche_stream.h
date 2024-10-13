// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// General-purpose abstractions for read/write streams.

#ifndef QUICHE_COMMON_QUICHE_STREAM_H_
#define QUICHE_COMMON_QUICHE_STREAM_H_

#include <cstddef>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_callbacks.h"

namespace quiche {

// A shared base class for read and write stream to support abrupt termination.
class QUICHE_EXPORT TerminableStream {
 public:
  virtual ~TerminableStream() = default;

  // Abruptly terminate the stream due to an error. If `error` is not OK, it may
  // carry the error information that could be potentially communicated to the
  // peer in case the stream is remote. If the stream is a duplex stream, both
  // ends of the stream are terminated.
  virtual void AbruptlyTerminate(absl::Status error) = 0;
};

// A general-purpose visitor API that gets notifications for ReadStream-related
// events.
class QUICHE_EXPORT ReadStreamVisitor {
 public:
  virtual ~ReadStreamVisitor() = default;

  // Called whenever the stream has new data available to read. Unless otherwise
  // specified, QUICHE stream reads are level-triggered, which means that the
  // callback will be called repeatedly as long as there is still data in the
  // buffer.
  virtual void OnCanRead() = 0;
};

// General purpose abstraction for a stream of data that can be read from the
// network. The class is designed around the idea that a network stream stores
// all of the received data in a sequence of contiguous buffers. Because of
// that, there are two ways to read from a stream:
//   - Read() will copy data into a user-provided buffer, reassembling it if it
//     is split across multiple buffers internally.
//   - PeekNextReadableRegion()/SkipBytes() let the caller access the underlying
//     buffers directly, potentially avoiding the copying at the cost of the
//     caller having to deal with discontinuities.
class QUICHE_EXPORT ReadStream {
 public:
  struct QUICHE_EXPORT ReadResult {
    // Number of bytes actually read.
    size_t bytes_read = 0;
    // Whether the FIN has been received; if true, no further data will arrive
    // on the stream, and the stream object can be soon potentially garbage
    // collected.
    bool fin = false;
  };

  struct PeekResult {
    // The next available chunk in the sequencer buffer.
    absl::string_view peeked_data;
    // True if all of the data up to the FIN has been read.
    bool fin_next = false;
    // True if all of the data up to the FIN has been received (but not
    // necessarily read).
    bool all_data_received = false;

    // Indicates that `SkipBytes()` will make progress if called.
    bool has_data() const { return !peeked_data.empty() || fin_next; }
  };

  virtual ~ReadStream() = default;

  // Reads at most `buffer.size()` bytes into `buffer`.
  [[nodiscard]] virtual ReadResult Read(absl::Span<char> buffer) = 0;

  // Reads all available data and appends it to the end of `output`.
  [[nodiscard]] virtual ReadResult Read(std::string* output) = 0;

  // Indicates the total number of bytes that can be read from the stream.
  virtual size_t ReadableBytes() const = 0;

  // Returns a contiguous buffer to read (or an empty buffer, if there is no
  // data to read). See `ProcessAllReadableRegions` below for an example of how
  // to use this method while handling FIN correctly.
  virtual PeekResult PeekNextReadableRegion() const = 0;

  // Equivalent to reading `bytes`, but does not perform any copying. `bytes`
  // must be less than or equal to `ReadableBytes()`. The return value indicates
  // if the FIN has been reached. `SkipBytes(0)` can be used to consume the FIN
  // if it's the only thing remaining on the stream.
  [[nodiscard]] virtual bool SkipBytes(size_t bytes) = 0;
};

// Calls `callback` for every contiguous chunk available inside the stream.
// Returns true if the FIN has been reached.
inline bool ProcessAllReadableRegions(
    ReadStream& stream, UnretainedCallback<void(absl::string_view)> callback) {
  for (;;) {
    ReadStream::PeekResult peek_result = stream.PeekNextReadableRegion();
    if (!peek_result.has_data()) {
      return false;
    }
    callback(peek_result.peeked_data);
    bool fin = stream.SkipBytes(peek_result.peeked_data.size());
    if (fin) {
      return true;
    }
  }
}

// A general-purpose visitor API that gets notifications for WriteStream-related
// events.
class QUICHE_EXPORT WriteStreamVisitor {
 public:
  virtual ~WriteStreamVisitor() {}

  // Called whenever the stream is not write-blocked and can accept new data.
  virtual void OnCanWrite() = 0;
};

// Options for writing data into a WriteStream.
class QUICHE_EXPORT StreamWriteOptions {
 public:
  StreamWriteOptions() = default;

  // If send_fin() is set to true, the write operation also sends a FIN on the
  // stream.
  bool send_fin() const { return send_fin_; }
  void set_send_fin(bool send_fin) { send_fin_ = send_fin; }

  // If buffer_unconditionally() is set to true, the write operation will buffer
  // data even if the internal buffer limit is exceeded.
  bool buffer_unconditionally() const { return buffer_unconditionally_; }
  void set_buffer_unconditionally(bool value) {
    buffer_unconditionally_ = value;
  }

 private:
  bool send_fin_ = false;
  bool buffer_unconditionally_ = false;
};

inline constexpr StreamWriteOptions kDefaultStreamWriteOptions =
    StreamWriteOptions();

// WriteStream is an object that can accept a stream of bytes.
//
// The writes into a WriteStream are all-or-nothing.  A WriteStream object has
// to either accept all data written into it by returning absl::OkStatus, or ask
// the caller to try again once via OnCanWrite() by returning
// absl::UnavailableError.
class QUICHE_EXPORT WriteStream {
 public:
  virtual ~WriteStream() {}

  // Writes |data| into the stream.
  virtual absl::Status Writev(absl::Span<const absl::string_view> data,
                              const StreamWriteOptions& options) = 0;

  // Indicates whether it is possible to write into stream right now.
  virtual bool CanWrite() const = 0;

  // Legacy convenience method for writing a single string_view.  New users
  // should use quiche::WriteIntoStream instead, since this method does not
  // return useful failure information.
  [[nodiscard]] bool SendFin() {
    StreamWriteOptions options;
    options.set_send_fin(true);
    return Writev(absl::Span<const absl::string_view>(), options).ok();
  }

  // Legacy convenience method for writing a single string_view.  New users
  // should use quiche::WriteIntoStream instead, since this method does not
  // return useful failure information.
  [[nodiscard]] bool Write(absl::string_view data) {
    return Writev(absl::MakeSpan(&data, 1), kDefaultStreamWriteOptions).ok();
  }
};

// Convenience methods to write a single chunk of data into the stream.
inline absl::Status WriteIntoStream(
    WriteStream& stream, absl::string_view data,
    const StreamWriteOptions& options = kDefaultStreamWriteOptions) {
  return stream.Writev(absl::MakeSpan(&data, 1), options);
}

// Convenience methods to send a FIN on the stream.
inline absl::Status SendFinOnStream(WriteStream& stream) {
  StreamWriteOptions options;
  options.set_send_fin(true);
  return stream.Writev(absl::Span<const absl::string_view>(), options);
}

inline size_t TotalStringViewSpanSize(
    absl::Span<const absl::string_view> span) {
  size_t total = 0;
  for (absl::string_view view : span) {
    total += view.size();
  }
  return total;
}

}  // namespace quiche

#endif  // QUICHE_COMMON_QUICHE_STREAM_H_
