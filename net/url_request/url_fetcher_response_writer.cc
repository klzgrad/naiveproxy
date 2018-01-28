// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/url_request/url_fetcher_response_writer.h"

#include "base/callback_helpers.h"
#include "base/files/file_util.h"
#include "base/location.h"
#include "base/sequenced_task_runner.h"
#include "base/task_runner_util.h"
#include "net/base/file_stream.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"

namespace net {

URLFetcherStringWriter* URLFetcherResponseWriter::AsStringWriter() {
  return NULL;
}

URLFetcherFileWriter* URLFetcherResponseWriter::AsFileWriter() {
  return NULL;
}

URLFetcherStringWriter::URLFetcherStringWriter() {
}

URLFetcherStringWriter::~URLFetcherStringWriter() {
}

int URLFetcherStringWriter::Initialize(const CompletionCallback& callback) {
  data_.clear();
  return OK;
}

int URLFetcherStringWriter::Write(IOBuffer* buffer,
                                  int num_bytes,
                                  const CompletionCallback& callback) {
  data_.append(buffer->data(), num_bytes);
  return num_bytes;
}

int URLFetcherStringWriter::Finish(int net_error,
                                   const CompletionCallback& callback) {
  // Do nothing.
  return OK;
}

URLFetcherStringWriter* URLFetcherStringWriter::AsStringWriter() {
  return this;
}

URLFetcherFileWriter::URLFetcherFileWriter(
    scoped_refptr<base::SequencedTaskRunner> file_task_runner,
    const base::FilePath& file_path)
    : file_task_runner_(file_task_runner),
      file_path_(file_path),
      owns_file_(false),
      weak_factory_(this) {
  DCHECK(file_task_runner_.get());
}

URLFetcherFileWriter::~URLFetcherFileWriter() {
  CloseAndDeleteFile();
}

int URLFetcherFileWriter::Initialize(const CompletionCallback& callback) {
  DCHECK(!callback_);

  file_stream_.reset(new FileStream(file_task_runner_));

  int result = ERR_IO_PENDING;
  owns_file_ = true;
  if (file_path_.empty()) {
    base::FilePath* temp_file_path = new base::FilePath;
    base::PostTaskAndReplyWithResult(
        file_task_runner_.get(),
        FROM_HERE,
        base::Bind(&base::CreateTemporaryFile, temp_file_path),
        base::Bind(&URLFetcherFileWriter::DidCreateTempFile,
                   weak_factory_.GetWeakPtr(),
                   base::Owned(temp_file_path)));
  } else {
    result = file_stream_->Open(file_path_, base::File::FLAG_WRITE |
                                                base::File::FLAG_ASYNC |
                                                base::File::FLAG_CREATE_ALWAYS,
                                base::Bind(&URLFetcherFileWriter::OnIOCompleted,
                                           weak_factory_.GetWeakPtr()));
    DCHECK_NE(OK, result);
  }

  if (result == ERR_IO_PENDING) {
    callback_ = callback;
    return result;
  }
  if (result < 0)
    CloseAndDeleteFile();
  return result;
}

int URLFetcherFileWriter::Write(IOBuffer* buffer,
                                int num_bytes,
                                const CompletionCallback& callback) {
  DCHECK(file_stream_);
  DCHECK(owns_file_);
  DCHECK(!callback_);

  int result = file_stream_->Write(
      buffer, num_bytes, base::Bind(&URLFetcherFileWriter::OnIOCompleted,
                                    weak_factory_.GetWeakPtr()));
  if (result == ERR_IO_PENDING) {
    callback_ = callback;
    return result;
  }
  if (result < 0)
    CloseAndDeleteFile();
  return result;
}

int URLFetcherFileWriter::Finish(int net_error,
                                 const CompletionCallback& callback) {
  DCHECK_NE(ERR_IO_PENDING, net_error);

  // If an error occurred, simply delete the file after any pending operation
  // is done. Do not call file_stream_->Close() because there might be an
  // operation pending. See crbug.com/487732.
  if (net_error < 0) {
    // Cancel callback and invalid weak ptrs so as to cancel any posted task.
    callback_.Reset();
    weak_factory_.InvalidateWeakPtrs();
    CloseAndDeleteFile();
    return OK;
  }
  DCHECK(!callback_);
  // If the file_stream_ still exists at this point, close it.
  if (file_stream_) {
    int result = file_stream_->Close(base::Bind(
        &URLFetcherFileWriter::CloseComplete, weak_factory_.GetWeakPtr()));
    if (result == ERR_IO_PENDING) {
      callback_ = callback;
      return result;
    }
    file_stream_.reset();
    return result;
  }
  return OK;
}

URLFetcherFileWriter* URLFetcherFileWriter::AsFileWriter() {
  return this;
}

void URLFetcherFileWriter::DisownFile() {
  // Disowning is done by the delegate's OnURLFetchComplete method.
  // The file should be closed by the time that method is called.
  DCHECK(!file_stream_);

  owns_file_ = false;
}

void URLFetcherFileWriter::CloseAndDeleteFile() {
  if (!owns_file_)
    return;

  file_stream_.reset();
  DisownFile();
  file_task_runner_->PostTask(FROM_HERE,
                              base::Bind(base::IgnoreResult(&base::DeleteFile),
                                         file_path_,
                                         false /* recursive */));
}

void URLFetcherFileWriter::DidCreateTempFile(base::FilePath* temp_file_path,
                                             bool success) {
  if (!success) {
    OnIOCompleted(ERR_FILE_NOT_FOUND);
    return;
  }
  file_path_ = *temp_file_path;
  const int result = file_stream_->Open(
      file_path_,
      base::File::FLAG_WRITE | base::File::FLAG_ASYNC | base::File::FLAG_OPEN,
      base::Bind(&URLFetcherFileWriter::OnIOCompleted,
                 weak_factory_.GetWeakPtr()));
  if (result != ERR_IO_PENDING)
    OnIOCompleted(result);
}

void URLFetcherFileWriter::OnIOCompleted(int result) {
  if (result < OK)
    CloseAndDeleteFile();

  if (!callback_.is_null())
    base::ResetAndReturn(&callback_).Run(result);
}

void URLFetcherFileWriter::CloseComplete(int result) {
  // Destroy |file_stream_| whether or not the close succeeded.
  file_stream_.reset();
  if (!callback_.is_null())
    base::ResetAndReturn(&callback_).Run(result);
}

}  // namespace net
