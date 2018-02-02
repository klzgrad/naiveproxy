// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/url_request/url_request_simple_job.h"

#include <vector>

#include "base/bind.h"
#include "base/compiler_specific.h"
#include "base/location.h"
#include "base/memory/ref_counted_memory.h"
#include "base/single_thread_task_runner.h"
#include "base/task_scheduler/post_task.h"
#include "base/threading/thread_task_runner_handle.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_util.h"
#include "net/url_request/url_request_status.h"

namespace net {

namespace {

void CopyData(const scoped_refptr<IOBuffer>& buf,
              int buf_size,
              const scoped_refptr<base::RefCountedMemory>& data,
              int64_t data_offset) {
  memcpy(buf->data(), data->front() + data_offset, buf_size);
}

}  // namespace

URLRequestSimpleJob::URLRequestSimpleJob(URLRequest* request,
                                         NetworkDelegate* network_delegate)
    : URLRangeRequestJob(request, network_delegate),
      next_data_offset_(0),
      weak_factory_(this) {
}

void URLRequestSimpleJob::Start() {
  // Start reading asynchronously so that all error reporting and data
  // callbacks happen as they would for network requests.
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&URLRequestSimpleJob::StartAsync, weak_factory_.GetWeakPtr()));
}

void URLRequestSimpleJob::Kill() {
  weak_factory_.InvalidateWeakPtrs();
  URLRangeRequestJob::Kill();
}

bool URLRequestSimpleJob::GetMimeType(std::string* mime_type) const {
  *mime_type = mime_type_;
  return true;
}

bool URLRequestSimpleJob::GetCharset(std::string* charset) {
  *charset = charset_;
  return true;
}

URLRequestSimpleJob::~URLRequestSimpleJob() = default;

int URLRequestSimpleJob::ReadRawData(IOBuffer* buf, int buf_size) {
  buf_size = std::min(static_cast<int64_t>(buf_size),
                      byte_range_.last_byte_position() - next_data_offset_ + 1);
  if (buf_size == 0)
    return 0;

  // Do memory copy asynchronously on a thread that is not the network thread.
  // See crbug.com/422489.
  base::PostTaskWithTraitsAndReply(
      FROM_HERE, {base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::Bind(&CopyData, base::WrapRefCounted(buf), buf_size, data_,
                 next_data_offset_),
      base::Bind(&URLRequestSimpleJob::ReadRawDataComplete,
                 weak_factory_.GetWeakPtr(), buf_size));
  next_data_offset_ += buf_size;
  return ERR_IO_PENDING;
}

int URLRequestSimpleJob::GetData(std::string* mime_type,
                                 std::string* charset,
                                 std::string* data,
                                 const CompletionCallback& callback) const {
  NOTREACHED();
  return ERR_UNEXPECTED;
}

int URLRequestSimpleJob::GetRefCountedData(
    std::string* mime_type,
    std::string* charset,
    scoped_refptr<base::RefCountedMemory>* data,
    const CompletionCallback& callback) const {
  scoped_refptr<base::RefCountedString> str_data(new base::RefCountedString());
  int result = GetData(mime_type, charset, &str_data->data(), callback);
  *data = str_data;
  return result;
}

void URLRequestSimpleJob::StartAsync() {
  if (!request_)
    return;

  if (ranges().size() > 1) {
    NotifyStartError(URLRequestStatus(URLRequestStatus::FAILED,
                                      ERR_REQUEST_RANGE_NOT_SATISFIABLE));
    return;
  }

  if (!ranges().empty() && range_parse_result() == OK)
    byte_range_ = ranges().front();

  const int result =
      GetRefCountedData(&mime_type_, &charset_, &data_,
                        base::Bind(&URLRequestSimpleJob::OnGetDataCompleted,
                                   weak_factory_.GetWeakPtr()));

  if (result != ERR_IO_PENDING)
    OnGetDataCompleted(result);
}

void URLRequestSimpleJob::OnGetDataCompleted(int result) {
  if (result == OK) {
    // Notify that the headers are complete
    if (!byte_range_.ComputeBounds(data_->size())) {
      NotifyStartError(URLRequestStatus(URLRequestStatus::FAILED,
                                        ERR_REQUEST_RANGE_NOT_SATISFIABLE));
      return;
    }

    next_data_offset_ = byte_range_.first_byte_position();
    set_expected_content_size(byte_range_.last_byte_position() -
                              next_data_offset_ + 1);
    NotifyHeadersComplete();
  } else {
    NotifyStartError(URLRequestStatus(URLRequestStatus::FAILED, result));
  }
}

}  // namespace net
