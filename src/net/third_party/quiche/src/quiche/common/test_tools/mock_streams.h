// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_TEST_TOOLS_MOCK_STREAMS_H_
#define QUICHE_COMMON_TEST_TOOLS_MOCK_STREAMS_H_

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/common/platform/api/quiche_test.h"
#include "quiche/common/quiche_stream.h"

namespace quiche::test {

// Mockable stream that stores all of the data into an std::string by default.
class MockWriteStream : public quiche::WriteStream {
 public:
  MockWriteStream() {
    ON_CALL(*this, CanWrite()).WillByDefault(testing::Return(true));
    ON_CALL(*this, Writev(testing::_, testing::_))
        .WillByDefault([&](absl::Span<const absl::string_view> data,
                           const StreamWriteOptions& options) {
          return AppendToData(data, options);
        });
  }

  MOCK_METHOD(absl::Status, Writev,
              (absl::Span<const absl::string_view> data,
               const StreamWriteOptions& options),
              (override));
  MOCK_METHOD(bool, CanWrite, (), (const, override));

  absl::Status AppendToData(absl::Span<const absl::string_view> data,
                            const StreamWriteOptions& options) {
    for (absl::string_view fragment : data) {
      data_.append(fragment.data(), fragment.size());
    }
    ProcessOptions(options);
    return absl::OkStatus();
  }
  void ProcessOptions(const StreamWriteOptions& options) {
    fin_written_ |= options.send_fin();
  }

  std::string& data() { return data_; }
  bool fin_written() { return fin_written_; }

 private:
  std::string data_;
  bool fin_written_ = false;
};

// Reads stream data from an std::string buffer.
class ReadStreamFromString : public ReadStream {
 public:
  explicit ReadStreamFromString(std::string* data) : data_(data) {}

  ReadResult Read(absl::Span<char> buffer) override {
    size_t data_to_copy = std::min(buffer.size(), data_->size());
    std::copy(data_->begin(), data_->begin() + data_to_copy, buffer.begin());
    *data_ = data_->substr(data_to_copy);
    return ReadResult{data_to_copy, data_->empty() && fin_};
  }
  ReadResult Read(std::string* output) override {
    size_t bytes = data_->size();
    output->append(std::move(*data_));
    data_->clear();
    return ReadResult{bytes, fin_};
  }

  size_t ReadableBytes() const override { return data_->size(); }

  virtual PeekResult PeekNextReadableRegion() const override {
    PeekResult result;
    result.peeked_data = *data_;
    result.fin_next = data_->empty() && fin_;
    result.all_data_received = fin_;
    return result;
  }

  bool SkipBytes(size_t bytes) override {
    *data_ = data_->substr(bytes);
    return data_->empty() && fin_;
  }

  void set_fin() { fin_ = true; }

 private:
  std::string* data_;
  bool fin_ = false;
};

}  // namespace quiche::test

#endif  // QUICHE_COMMON_TEST_TOOLS_MOCK_STREAMS_H_
